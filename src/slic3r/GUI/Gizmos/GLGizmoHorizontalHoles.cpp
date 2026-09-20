#include "GLGizmoHorizontalHoles.hpp"
#include "GLGizmoUtils.hpp"

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <glad/gl.h>

#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace Slic3r {
namespace GUI {

namespace {

// Name given to every negative volume this gizmo creates; the provenance marker used to find,
// replace and remove teardrops across sessions.
constexpr const char *TEARDROP_NAME = "Teardrop";
// |axis . up| below this marks a hole as horizontal (the top of the wall is an overhang).
constexpr double       HORIZONTAL_COS = 0.5;
constexpr float        ANGLE_MIN = 45.f;
constexpr float        ANGLE_MAX = 60.f;
// Candidate holes are a soft blue ghost; an applied teardrop is red; hover is bright green.
const ColorRGBA        ALL_COLOR{0.25f, 0.70f, 1.00f, 0.40f};
const ColorRGBA        TEARDROP_COLOR{1.00f, 0.15f, 0.15f, 0.80f};
const ColorRGBA        HOVER_COLOR{0.10f, 1.00f, 0.20f, 0.90f};

Vec3d mesh_centroid(const indexed_triangle_set &its)
{
    if (its.vertices.empty())
        return Vec3d::Zero();
    Vec3d c = Vec3d::Zero();
    for (const stl_vertex &v : its.vertices)
        c += Vec3d(v(0), v(1), v(2));
    return c / double(its.vertices.size());
}

void merge_into(indexed_triangle_set &dst, indexed_triangle_set &src)
{
    if (src.indices.empty())
        return;
    if (dst.indices.empty())
        dst = std::move(src);
    else
        its_merge(dst, src);
}

// An oversized cylinder wall around a hole, used only for mouse picking. Right-handed frame so
// the face normals point outward and SceneRaycaster accepts the hit.
indexed_triangle_set make_pick_cylinder(const DetectedHole &h, const Vec3d &up)
{
    const double         len = std::max(h.depth, 1.0);
    indexed_triangle_set its = its_make_cylinder(h.radius * 1.25, len, PI / 12.);

    const Vec3d a = h.axis.normalized();
    Vec3d       u = up - a * a.dot(up);
    u = (u.norm() < 1e-9) ? Vec3d::UnitY() : u.normalized();
    const Vec3d r = u.cross(a).normalized(); // r x u == a: proper rotation, normals outward

    Eigen::Matrix3d R;
    R.col(0) = Eigen::Vector3d(r(0), r(1), r(2));
    R.col(1) = Eigen::Vector3d(u(0), u(1), u(2));
    R.col(2) = Eigen::Vector3d(a(0), a(1), a(2));

    Transform3d tr = Transform3d::Identity();
    tr.linear()    = R;
    tr.translation() = h.center - R * Eigen::Vector3d(0., 0., 0.5 * len);
    for (stl_vertex &v : its.vertices)
        v = (tr * Eigen::Vector3d(v(0), v(1), v(2))).cast<float>();
    return its;
}

} // namespace

GLGizmoHorizontalHoles::GLGizmoHorizontalHoles(GLCanvas3D &parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

bool GLGizmoHorizontalHoles::on_init()
{
    m_shortcut_key = WXK_CONTROL_J;

    m_desc["apex"]             = _L("Apex angle");
    m_desc["holes"]            = _L("Horizontal holes");
    m_desc["all"]              = _L("Teardrop all");
    m_desc["clear"]            = _L("Clear");
    m_desc["clipping_of_view"] = _L("Section view");
    m_desc["reset_direction"]  = _L("Reset direction");
    return true;
}

std::string GLGizmoHorizontalHoles::on_get_name() const { return _u8L("Horizontal holes"); }

ModelObject *GLGizmoHorizontalHoles::model_object() const
{
    if (m_c == nullptr || m_c->selection_info() == nullptr)
        return nullptr;
    return m_c->selection_info()->model_object();
}

int GLGizmoHorizontalHoles::object_idx() const { return m_parent.get_selection().get_object_idx(); }

Transform3d GLGizmoHorizontalHoles::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

Vec3d GLGizmoHorizontalHoles::object_up() const
{
    Vec3d up = instance_matrix().linear().inverse() * Vec3d::UnitZ();
    if (!up.allFinite() || up.norm() < 1e-9)
        return Vec3d::UnitZ();
    return up.normalized();
}

double GLGizmoHorizontalHoles::teardrop_depth(const DetectedHole &hole) const
{
    const double margin = hole.through ? std::max(0.5, 0.25 * hole.radius) : 0.0;
    return hole.depth + 2.0 * margin;
}

indexed_triangle_set GLGizmoHorizontalHoles::teardrop_mesh(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    return its_make_teardrop_for_hole(m_holes[idx].hole, teardrop_depth(m_holes[idx].hole), m_angle_deg, 48, object_up());
}

bool GLGizmoHorizontalHoles::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

CommonGizmosDataID GLGizmoHorizontalHoles::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoHorizontalHoles::on_set_state()
{
    if (get_state() == On) {
        m_dirty         = true;
        m_preview_dirty = true;
        m_parent.set_as_dirty();
    } else {
        m_preview_all.reset();
        m_preview_teardrop.reset();
        m_preview_hover.reset();
    }
}

void GLGizmoHorizontalHoles::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_holes.clear();
        m_teardrop.clear();
        m_pick_its.clear();
        m_preview_all.reset();
        m_preview_teardrop.reset();
        m_preview_hover.reset();
        m_dirty = false;
        return;
    }

    const int         n  = int(mo->volumes.size());
    const Transform3d im = instance_matrix();
    if (mo != m_old_model_object || n != m_old_volume_count || !im.isApprox(m_old_instance_matrix)) {
        m_dirty = true;
        m_parent.set_as_dirty();
    }
}

void GLGizmoHorizontalHoles::on_set_hover_id()
{
    if (m_hover_id < -1 || m_hover_id >= int(m_holes.size()))
        m_hover_id = -1;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHorizontalHoles::on_register_raycasters_for_picking() { register_pickers(); }

void GLGizmoHorizontalHoles::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
    m_pick_items.clear();
    m_pick_raycasters.clear();
}

void GLGizmoHorizontalHoles::register_pickers()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_pick_items.clear();
    m_pick_raycasters.clear();
    if (get_state() != On || m_pick_its.empty())
        return;

    m_parent.set_raycaster_gizmos_on_top(true);
    const Transform3d trafo = instance_matrix();
    for (size_t i = 0; i < m_pick_its.size(); ++i) {
        auto raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(m_pick_its[i]));
        m_pick_items.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, int(i), *raycaster, trafo));
        m_pick_raycasters.emplace_back(std::move(raycaster));
    }
}

void GLGizmoHorizontalHoles::detect()
{
    m_dirty = false;
    m_holes.clear();
    m_teardrop.clear();
    m_pick_its.clear();
    m_preview_all.reset();
    m_preview_teardrop.reset();
    m_preview_hover.reset();
    m_old_model_object = nullptr;
    m_old_volume_count = -1;

    ModelObject *mo = model_object();
    if (mo == nullptr) {
        register_pickers();
        m_preview_dirty = true;
        return;
    }

    wxBusyCursor wait;
    const Vec3d  up   = object_up();
    TriangleMesh mesh = mo->raw_mesh();
    std::vector<DetectedHole> holes = detect_holes(mesh.its);

    for (DetectedHole &h : holes) {
        if (std::abs(h.axis.dot(up)) >= HORIZONTAL_COS)
            continue; // vertical: a teardrop has no meaning
        HoleView v;
        v.hole = std::move(h);
        m_holes.push_back(std::move(v));
    }

    m_pick_its.reserve(m_holes.size());
    for (const HoleView &v : m_holes)
        m_pick_its.push_back(make_pick_cylinder(v.hole, up));

    refresh_applied();
    m_old_model_object    = mo;
    m_old_volume_count    = int(mo->volumes.size());
    m_old_instance_matrix = instance_matrix();

    register_pickers();
    m_preview_dirty = true;
}

void GLGizmoHorizontalHoles::refresh_applied()
{
    m_teardrop.assign(m_holes.size(), 0);
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    std::vector<Vec3d> centroids;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || !v->is_negative_volume() || v->name.rfind(TEARDROP_NAME, 0) != 0)
            continue;
        centroids.push_back(mesh_centroid(v->mesh().its));
    }
    for (size_t i = 0; i < m_holes.size(); ++i) {
        const double tol = std::max(0.5, m_holes[i].hole.radius);
        for (const Vec3d &c : centroids)
            if ((c - m_holes[i].hole.center).norm() < tol) {
                m_teardrop[i] = 1;
                break;
            }
    }
}

void GLGizmoHorizontalHoles::rebuild_previews()
{
    m_preview_dirty = false;
    m_preview_all.reset();
    m_preview_teardrop.reset();
    m_preview_hover.reset();

    indexed_triangle_set all_its, teardrop_its, hover_its;
    for (size_t i = 0; i < m_holes.size(); ++i) {
        indexed_triangle_set ghost = teardrop_mesh(int(i));
        if (ghost.indices.empty())
            continue;
        merge_into(all_its, ghost);

        if (m_teardrop[i]) {
            indexed_triangle_set td = teardrop_mesh(int(i));
            merge_into(teardrop_its, td);
        }
        if (int(i) == m_hover_id && !m_teardrop[i]) {
            indexed_triangle_set hint = teardrop_mesh(int(i));
            merge_into(hover_its, hint);
        }
    }

    if (!all_its.indices.empty()) {
        m_preview_all.model.init_from(all_its);
        m_preview_all.model.set_color(ALL_COLOR);
    }
    if (!teardrop_its.indices.empty()) {
        m_preview_teardrop.model.init_from(teardrop_its);
        m_preview_teardrop.model.set_color(TEARDROP_COLOR);
    }
    if (!hover_its.indices.empty()) {
        m_preview_hover.model.init_from(hover_its);
        m_preview_hover.model.set_color(HOVER_COLOR);
    }
}

void GLGizmoHorizontalHoles::on_render()
{
    if (m_dirty)
        detect();
    if (m_preview_dirty)
        rebuild_previews();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();
    // The previews sit inside the hole, so they must not be depth-occluded by the solid. Clear
    // the depth buffer with the writemask forced on (glClear honours the depth mask) and then
    // draw the overlay without depth testing so the ghosts are always visible.
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));

    const Camera   &camera = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * instance_matrix();
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    // Pass the shader explicitly: the ghost/teardrop/hover colors must be the ones set here.
    m_preview_all.model.render(shader);
    m_preview_teardrop.model.render(shader);
    m_preview_hover.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

// ---------------------------------------------------------------------------------------------
// Model edits
// ---------------------------------------------------------------------------------------------

void GLGizmoHorizontalHoles::add_teardrop_volume(int idx, const std::string &snapshot_name)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;
    indexed_triangle_set its = teardrop_mesh(idx);
    if (its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, snapshot_name, UndoRedo::SnapshotType::GizmoAction);
    ModelVolume *v = mo->add_volume(TriangleMesh(std::move(its)), ModelVolumeType::NEGATIVE_VOLUME, false);
    v->name        = TEARDROP_NAME;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

void GLGizmoHorizontalHoles::remove_teardrop_volume(int idx, const std::string &snapshot_name)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0 || idx < 0 || idx >= int(m_holes.size()))
        return;

    const double               tol = std::max(0.5, m_holes[idx].hole.radius);
    std::vector<ItemForDelete> items;
    std::vector<char>          marked(mo->volumes.size(), 0);
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if (!v->is_negative_volume() || v->name.rfind(TEARDROP_NAME, 0) != 0)
            continue;
        if (marked[vi] || (mesh_centroid(v->mesh().its) - m_holes[idx].hole.center).norm() >= tol)
            continue;
        marked[vi] = 1;
        items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, snapshot_name, UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
}

void GLGizmoHorizontalHoles::toggle_teardrop(int idx)
{
    if (m_teardrop[idx])
        remove_teardrop_volume(idx, _u8L("Remove teardrop"));
    else
        add_teardrop_volume(idx, _u8L("Add teardrop"));
}

void GLGizmoHorizontalHoles::clear_all()
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if (v->is_negative_volume() && v->name.rfind(TEARDROP_NAME, 0) == 0)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Remove teardrops"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
}

void GLGizmoHorizontalHoles::reapply_teardrops()
{
    std::vector<int> applied;
    for (size_t i = 0; i < m_holes.size(); ++i)
        if (m_teardrop[i])
            applied.push_back(int(i));

    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if (v->is_negative_volume() && v->name.rfind(TEARDROP_NAME, 0) == 0)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (applied.empty() && items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Update teardrops"), UndoRedo::SnapshotType::GizmoAction);
    if (!items.empty())
        if (ObjectList *ol = wxGetApp().obj_list())
            ol->delete_from_model_and_list(items);
    for (int i : applied) {
        indexed_triangle_set its = teardrop_mesh(i);
        if (its.indices.empty())
            continue;
        ModelVolume *v = mo->add_volume(TriangleMesh(std::move(its)), ModelVolumeType::NEGATIVE_VOLUME, false);
        v->name        = TEARDROP_NAME;
    }
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

// ---------------------------------------------------------------------------------------------
// MCP control surface
// ---------------------------------------------------------------------------------------------

int GLGizmoHorizontalHoles::hole_count()
{
    if (m_dirty)
        detect();
    return int(m_holes.size());
}

DetectedHole GLGizmoHorizontalHoles::hole(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    return m_holes[idx].hole;
}

bool GLGizmoHorizontalHoles::hole_has_teardrop(int idx) const
{
    return idx >= 0 && idx < int(m_teardrop.size()) && m_teardrop[idx] != 0;
}

void GLGizmoHorizontalHoles::set_angle(float deg)
{
    m_angle_deg     = std::clamp(deg, ANGLE_MIN, ANGLE_MAX);
    m_angle_changed = true;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHorizontalHoles::gizmo_toggle_hole(int idx)
{
    if (m_dirty)
        detect();
    if (idx < 0 || idx >= int(m_holes.size()))
        return;
    toggle_teardrop(idx);
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHorizontalHoles::gizmo_apply_all()
{
    if (m_dirty)
        detect();
    for (size_t i = 0; i < m_holes.size(); ++i) {
        if (m_teardrop[i])
            continue;
        toggle_teardrop(int(i));
    }
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHorizontalHoles::gizmo_clear_all()
{
    if (m_dirty)
        detect();
    clear_all();
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHorizontalHoles::gizmo_refresh()
{
    m_dirty         = true;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

bool GLGizmoHorizontalHoles::on_mouse(const wxMouseEvent &mouse_event)
{
    const bool on_hole = m_hover_id >= 0 && m_hover_id < int(m_holes.size());
    if (mouse_event.LeftDown() && on_hole) {
        gizmo_toggle_hole(m_hover_id);
        return true;
    }
    if (mouse_event.LeftUp() && on_hole)
        return true;
    return false;
}

void GLGizmoHorizontalHoles::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    if (m_dirty)
        detect();

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(7.0f);
    const float left_width    = m_imgui->scaled(9.0f);

    // Apex angle: slider plus a typed value, clamped to the sane range.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("apex"));
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    float angle = m_angle_deg;
    if (m_imgui->bbl_slider_float_style("##apex", &angle, ANGLE_MIN, ANGLE_MAX, "%.0f", 1.0f, true))
        set_angle(angle);
    ImGui::PopItemWidth();
    ImGui::SameLine();
    ImGui::PushItemWidth(m_imgui->scaled(4.0f));
    if (ImGui::InputFloat("##apex_in", &angle, 1.f, 5.f, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue))
        set_angle(angle);
    ImGui::PopItemWidth();

    ImGui::Separator();

    const int applied = int(std::count_if(m_teardrop.begin(), m_teardrop.end(), [](char a) { return a != 0; }));
    m_imgui->text(wxString::Format("%s: %d (%d teardropped)", m_desc.at("holes").c_str(), int(m_holes.size()), applied));

    m_imgui->disabled_begin(m_holes.empty());
    if (m_imgui->button(m_desc.at("all")))
        gizmo_apply_all();
    m_imgui->disabled_end();
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("clear")))
        gizmo_clear_all();

    ImGui::Separator();

    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    } else if (m_imgui->button(m_desc.at("reset_direction"))) {
        wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
    }
    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    if (m_imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);
    ImGui::PopItemWidth();

    ImGui::Separator();

    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (m_imgui->button(_L("Done")))
        m_parent.reset_all_gizmos();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();

    // Changing the angle re-cuts the applied teardrops.
    if (m_angle_changed) {
        m_angle_changed = false;
        reapply_teardrops();
        refresh_applied();
        m_preview_dirty = true;
        m_parent.set_as_dirty();
    }
}

} // namespace GUI
} // namespace Slic3r
