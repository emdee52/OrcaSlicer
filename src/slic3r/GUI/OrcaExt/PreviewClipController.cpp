// [ORCAPORT FILE] OrcaExtGuiPreviewClipController - interactive clipping plane for the G-code preview
// Source: preFlight v1.3.0 (preFlight.PreviewClipController.*); re-implemented on OrcaSlicer 2.5 APIs
#include "PreviewClipController.hpp"

#include "slic3r/GUI/GCodeViewer.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"

#include <imgui/imgui.h>

#include <algorithm>
#include <float.h>

using Slic3r::GLVolume;
using Slic3r::GLVolumeCollection;
using Slic3r::GUI::Camera;
using Slic3r::GUI::GCodeViewer;
using Slic3r::GUI::GLCanvas3D;
using Slic3r::GUI::ImGuiWrapper;
using Slic3r::GUI::Plater;
using Slic3r::GUI::wxGetApp;

namespace Slic3r {
namespace OrcaExt {
namespace Gui {

// [ORCAPORT:PF-6] helper to get the GCodeViewer of the active preview canvas
static GCodeViewer* get_current_gcode_viewer()
{
    Plater* plater = wxGetApp().plater();
    if (plater == nullptr)
        return nullptr;
    GLCanvas3D* canvas = plater->get_current_canvas3D();
    if (canvas == nullptr)
        return nullptr;
    return &canvas->get_gcode_viewer();
}

// [ORCAPORT:PF-6] The Preview canvas has picking disabled and no volume raycasters, so pick the
// object by raycasting the preview shell volumes (which do carry a MeshRaycaster) directly.
int PreviewClipController::pick_object(const Vec2d& screen_pos) const
{
    GCodeViewer* viewer = get_current_gcode_viewer();
    if (viewer == nullptr)
        return -1;

    const Camera& camera          = wxGetApp().plater()->get_camera();
    const Vec3d   camera_position = camera.get_position();

    int    best_id   = -1;
    double best_dist = DBL_MAX;
    for (GLVolume* volume : viewer->get_shells_volumes().volumes)
    {
        if (volume == nullptr || volume->composite_id.object_id < 0 || volume->mesh_raycaster == nullptr)
            continue;

        Vec3f hit_position, hit_normal;
        if (!volume->mesh_raycaster->closest_hit(screen_pos, volume->world_matrix(), camera, hit_position, hit_normal))
            continue;

        // closest_hit reports the hit in the mesh's local frame; bring it back to world space
        const Vec3d  world_hit = volume->world_matrix() * hit_position.cast<double>();
        const double dist      = (world_hit - camera_position).squaredNorm();
        if (dist < best_dist)
        {
            best_dist = dist;
            best_id   = volume->composite_id.object_id;
        }
    }

    return best_id;
}

void PreviewClipController::activate(int object_id)
{
    if (m_active)
        deactivate();

    GCodeViewer* viewer = get_current_gcode_viewer();
    if (viewer == nullptr)
        return;

    GLVolumeCollection& shells = viewer->get_shells_volumes();
    if (shells.volumes.empty())
        return;

    m_object_id = object_id;
    m_active    = true;

    // Save current shell visibility state
    SavedState state;
    state.shells_visible = viewer->are_shells_visible();
    for (const GLVolume* v : shells.volumes)
        state.shell_visibility.push_back(v->is_active);
    m_saved_state = state;

    // Bounding box of the selected object's shell volumes in world space
    m_object_bbox = BoundingBoxf3();
    for (GLVolume* v : shells.volumes)
    {
        if (v->composite_id.object_id == object_id)
            m_object_bbox.merge(v->transformed_bounding_box());
    }

    // Capture the camera forward direction as the initial clip normal and start at 50%
    reset_direction();
    m_clip_ratio = 0.5;
    apply_clipping_plane();
}

void PreviewClipController::deactivate()
{
    if (!m_active)
        return;

    GCodeViewer* viewer = get_current_gcode_viewer();
    if (viewer != nullptr)
    {
        GLVolumeCollection& shells = viewer->get_shells_volumes();

        if (m_saved_state.has_value())
        {
            viewer->set_shells_visible(m_saved_state->shells_visible);
            for (size_t i = 0; i < shells.volumes.size() && i < m_saved_state->shell_visibility.size(); ++i)
                shells.volumes[i]->is_active = m_saved_state->shell_visibility[i];
        }

        viewer->reset_preview_clipping_plane();
        viewer->get_libvgcode_viewer().reset_clipping_plane();
    }

    m_active    = false;
    m_object_id = -1;
    m_saved_state.reset();
}

void PreviewClipController::set_position(double ratio)
{
    m_clip_ratio = std::clamp(ratio, 0.0, 1.0);
    apply_clipping_plane();
}

void PreviewClipController::reset_direction()
{
    const Camera& camera = wxGetApp().plater()->get_camera();
    m_clip_normal        = camera.get_dir_forward();

    const double len = m_clip_normal.norm();
    if (len > 1e-6)
        m_clip_normal /= len;
    else
        m_clip_normal = Vec3d(0.0, 0.0, 1.0);

    apply_clipping_plane();
}

void PreviewClipController::apply_clipping_plane()
{
    if (!m_active || !m_object_bbox.defined)
        return;

    GCodeViewer* viewer = get_current_gcode_viewer();
    if (viewer == nullptr)
        return;

    // Project the object bounding box corners onto the clip normal to find the range
    double min_proj = DBL_MAX;
    double max_proj = -DBL_MAX;
    for (int i = 0; i < 8; ++i)
    {
        Vec3d corner;
        corner.x() = (i & 1) ? m_object_bbox.max.x() : m_object_bbox.min.x();
        corner.y() = (i & 2) ? m_object_bbox.max.y() : m_object_bbox.min.y();
        corner.z() = (i & 4) ? m_object_bbox.max.z() : m_object_bbox.min.z();
        const double proj = m_clip_normal.dot(corner);
        min_proj = std::min(min_proj, proj);
        max_proj = std::max(max_proj, proj);
    }

    // ratio 0.0 = min_proj (nothing clipped), ratio 1.0 = max_proj (everything clipped)
    const double offset = min_proj + m_clip_ratio * (max_proj - min_proj);

    // plane = (n, -offset): fragments with dot(pos, n) - offset < 0 are discarded
    const std::array<double, 4> clip_plane = { m_clip_normal.x(), m_clip_normal.y(), m_clip_normal.z(), -offset };
    viewer->set_preview_clipping_plane(clip_plane);

    viewer->get_libvgcode_viewer().set_clipping_plane(static_cast<float>(m_clip_normal.x()),
                                                      static_cast<float>(m_clip_normal.y()),
                                                      static_cast<float>(m_clip_normal.z()),
                                                      static_cast<float>(-offset));
}

std::string PreviewClipController::get_object_name() const
{
    if (m_object_id < 0)
        return "Object";

    const Model& model = wxGetApp().plater()->model();
    const ModelObjectPtrs& objects = model.objects;
    if (m_object_id < static_cast<int>(objects.size()))
        return objects[m_object_id]->name;

    return "Object";
}

void PreviewClipController::render_imgui()
{
    ImGuiWrapper& imgui = *wxGetApp().imgui();

    const auto cnv_size = wxGetApp().plater()->get_current_canvas3D()->get_canvas_size();
    imgui.set_next_window_pos(static_cast<float>(cnv_size.get_width()) * 0.5f, 10.0f, ImGuiCond_Once, 0.5f, 0.0f);

    const ImGuiWindowFlags flags = ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse;

    // [ORCAPORT:PF-6] always-visible toggle in the preview; right-click is unreliable there
    if (!m_active)
    {
        const std::string title = "Clipping Plane###PreviewClipControllerToggle";
        ImGui::Begin(title.c_str(), nullptr, flags);
        if (ImGui::Button("Clipping Plane"))
        {
            // Clip the selected object, else the first object that has shells
            GCodeViewer* viewer = get_current_gcode_viewer();
            if (viewer != nullptr)
            {
                GLVolumeCollection& shells = viewer->get_shells_volumes();
                int object_id = -1;
                Plater* plater = wxGetApp().plater();
                if (plater != nullptr)
                {
                    const int selected = plater->get_selected_object_idx();
                    for (GLVolume* v : shells.volumes)
                    {
                        if (v != nullptr && v->composite_id.object_id == selected)
                        {
                            object_id = selected;
                            break;
                        }
                    }
                }
                if (object_id < 0)
                {
                    for (GLVolume* v : shells.volumes)
                    {
                        if (v != nullptr && v->composite_id.object_id >= 0)
                        {
                            object_id = v->composite_id.object_id;
                            break;
                        }
                    }
                }
                if (object_id >= 0)
                    activate(object_id);
            }
        }
        ImGui::End();
        return;
    }

    const std::string title = "Clipping Plane - " + get_object_name() + "###PreviewClipController";
    ImGui::Begin(title.c_str(), nullptr, flags);

    float ratio_f = static_cast<float>(m_clip_ratio);
    ImGui::TextUnformatted("Position");
    ImGui::SameLine();
    if (imgui.slider_float("##clip_pos", &ratio_f, 0.0f, 1.0f, "%.2f"))
        set_position(static_cast<double>(ratio_f));

    if (ImGui::Button("Reset Direction"))
        reset_direction();

    ImGui::SameLine();

    if (ImGui::Button("Close"))
        deactivate();

    ImGui::End();
}

} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r
