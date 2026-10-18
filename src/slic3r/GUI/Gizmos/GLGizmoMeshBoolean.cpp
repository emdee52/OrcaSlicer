#include "GLGizmoMeshBoolean.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/NotificationManager.hpp"
#include <string>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"
#include "libslic3r/Point.hpp"
#include <vector>
#include "slic3r/GUI/GUI_App.hpp"
#include <cstddef>
#include <limits>
#include <cmath>
#include "libslic3r/BoundingBox.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "libslic3r/Color.hpp"
#include <optional>
#include "libslic3r/TriangleSelector.hpp"
#include <algorithm>
#include <imgui.h>
#include <cereal/archives/binary.hpp>
#include <utility>
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include <wx/dataview.h>
#ifndef IMGUI_DEFINE_MATH_OPERATORS
#define IMGUI_DEFINE_MATH_OPERATORS
#endif
#include <imgui/imgui_internal.h>
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "slic3r/GUI/Selection.hpp"
namespace Slic3r {
namespace GUI {

static const std::string warning_text = _u8L("Unable to perform boolean operation on selected parts");

GLGizmoMeshBoolean::GLGizmoMeshBoolean(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{
}

GLGizmoMeshBoolean::~GLGizmoMeshBoolean() 
{
}

bool GLGizmoMeshBoolean::gizmo_event(SLAGizmoEventType action, const Vec2d& mouse_position, bool shift_down, bool alt_down, bool control_down) 
{
    if (action == SLAGizmoEventType::LeftDown) {
        const ModelObject* mo = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
        if (mo == nullptr)
            return true;

        const int closest_hit_mesh_id = raycast_volume(mouse_position);
        if (closest_hit_mesh_id < 0 || closest_hit_mesh_id >= int(mo->volumes.size()))
            return true;

        ModelVolume *mv = mo->volumes[closest_hit_mesh_id];
        assign_pick(mv, mv->get_matrix(), closest_hit_mesh_id,
                    get_selecting_state() == MeshBooleanSelectingState::SelectTool);
        return true;
    }
    return true;
}

// [ORCAPORT:MB-1] Put a pick in the slot it belongs in. A NEGATIVE volume is a cutter by construction:
// Holes, Edge dress, Perforations and Extrude all author their tool as a negative volume. A click that
// lands on one is therefore a request to subtract it, never to subtract FROM it, so the cutter goes to
// the tool slot whichever slot was asked for. Letting it sit in the source slot is what produced "the
// boolean did nothing and the circle vanished": difference then replaces the cutter with the sliver of
// it outside the part, leaving the part untouched.
//
// The slot the caller asked for still decides where a MODEL PART goes, and a tool-slot pick is taken
// literally (a part may be the tool too - that is the "cut the wall with this block" case).
void GLGizmoMeshBoolean::assign_pick(ModelVolume *mv, const Transform3d &trafo, int volume_idx, bool as_tool)
{
    if (mv == nullptr)
        return;

    const bool cutter = mv->is_negative_volume();
    if (as_tool || cutter) {
        m_tool.trafo      = trafo;
        m_tool.volume_idx = volume_idx;
        set_tool_volume(mv);
        if (as_tool)
            m_selecting_state = MeshBooleanSelectingState::SelectTool;
        // A cutter aimed at the source slot is a redirect: the click did not fill that slot, so the
        // state stays where it was - the operation is still owed a source.
        //
        // ...and with a cutter in the tool slot the only operation that means anything is Difference.
        // Union is merely the mode the gizmo opens in, so follow the pick unless the user has chosen a
        // mode themselves.
        if (cutter && !m_operation_chosen)
            m_operation_mode = MeshBooleanOperation::Difference;
        return;
    }

    // Only a MODEL PART aimed at the source slot reaches here.
    m_src.trafo      = trafo;
    m_src.volume_idx = volume_idx;
    set_src_volume(mv);
    m_selecting_state = MeshBooleanSelectingState::SelectTool;
}

// [ORCAPORT:MCP-3] The closest volume under a screen point, by the same raycast gizmo_event() uses.
// The loop is bounded by the shorter of mo->volumes and the raycaster array: a pick after the volume
// list changed must never read past either. get_raycaster_count() is what makes a disagreement
// between the two visible instead of a silent miss.
int GLGizmoMeshBoolean::raycast_volume(const Vec2d &mouse_position) const
{
    const ModelObject *mo = (m_c != nullptr && m_c->selection_info() != nullptr)
                                ? m_c->selection_info()->model_object()
                                : nullptr;
    if (mo == nullptr || m_c->raycaster() == nullptr)
        return -1;

    const int inst_idx = m_parent.get_selection().get_instance_idx();
    if (inst_idx < 0 || inst_idx >= int(mo->instances.size()))
        return -1;
    const ModelInstance *mi = mo->instances[inst_idx];

    const std::vector<const MeshRaycaster *> raycasters = m_c->raycaster()->raycasters();
    const int                                n          = int(std::min(mo->volumes.size(), raycasters.size()));

    const Camera         &camera = wxGetApp().plater()->get_camera();
    const auto           *clipper = m_c->object_clipper();
    const ClippingPlane  *clipping_plane = clipper != nullptr ? clipper->get_clipping_plane() : nullptr;

    Vec3f  normal = Vec3f::Zero(), hit = Vec3f::Zero();
    size_t facet = 0;
    double closest_hit_squared_distance = std::numeric_limits<double>::max();
    int    closest_hit_mesh_id          = -1;
    for (int mesh_id = 0; mesh_id < n; ++mesh_id) {
        const Transform3d trafo = mi->get_transformation().get_matrix() * mo->volumes[mesh_id]->get_matrix();
        if (raycasters[mesh_id]->unproject_on_mesh(mouse_position, trafo, camera, hit, normal, clipping_plane, &facet)) {
            const double hit_squared_distance = (camera.get_position() - trafo * hit.cast<double>()).squaredNorm();
            if (hit_squared_distance < closest_hit_squared_distance) {
                closest_hit_squared_distance = hit_squared_distance;
                closest_hit_mesh_id          = mesh_id;
            }
        }
    }
    return closest_hit_mesh_id;
}

int GLGizmoMeshBoolean::get_raycaster_count() const
{
    if (m_c == nullptr || m_c->raycaster() == nullptr)
        return -1;
    return int(m_c->raycaster()->raycasters().size());
}

bool GLGizmoMeshBoolean::on_mouse(const wxMouseEvent &mouse_event)
{
    // wxCoord == int --> wx/types.h
    Vec2i32 mouse_coord(mouse_event.GetX(), mouse_event.GetY());
    Vec2d mouse_pos = mouse_coord.cast<double>();

    // when control is down we allow scene pan and rotation even when clicking
    // over some object
    bool control_down           = mouse_event.CmdDown();
    bool grabber_contains_mouse = (get_hover_id() != -1);
    if (mouse_event.LeftDown()) {
        if ((!control_down || grabber_contains_mouse) &&            
            gizmo_event(SLAGizmoEventType::LeftDown, mouse_pos, mouse_event.ShiftDown(), mouse_event.AltDown(), false))
            // the gizmo got the event and took some action, there is no need
            // to do anything more
            return true;
    }

    return false;
}

bool GLGizmoMeshBoolean::on_init()
{
    m_shortcut = Shortcut::GizmoMeshBoolean;
    return true;
}

std::string GLGizmoMeshBoolean::on_get_name() const
{
    return _u8L("Mesh Boolean");
}

bool GLGizmoMeshBoolean::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance() && m_parent.get_selection().get_volume_idxs().size() > 1;
}

void GLGizmoMeshBoolean::on_render()
{
    if (m_parent.get_selection().get_object_idx() < 0)
        return;
    static ModelObject* last_mo = nullptr;
    ModelObject* curr_mo = m_parent.get_selection().get_model()->objects[m_parent.get_selection().get_object_idx()];
    if (last_mo != curr_mo) {
        last_mo = curr_mo;
        m_src.reset();
        m_tool.reset();
        m_operation_mode = MeshBooleanOperation::Union;
        m_selecting_state = MeshBooleanSelectingState::SelectSource;
        m_operation_chosen = false;   // [ORCAPORT:MB-1] another object: its mode is not chosen yet
        m_outcome = Outcome();
        return;
    }

    BoundingBoxf3 src_bb;
    BoundingBoxf3 tool_bb;
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[m_parent.get_selection().get_instance_idx()];
    const Selection& selection = m_parent.get_selection();
    const Selection::IndicesList& idxs = selection.get_volume_idxs();
    for (unsigned int i : idxs) {
        const GLVolume* volume = selection.get_volume(i);
        if(volume->volume_idx() == m_src.volume_idx) {
            src_bb = volume->transformed_convex_hull_bounding_box();
        }
        if (volume->volume_idx() == m_tool.volume_idx) {
            tool_bb = volume->transformed_convex_hull_bounding_box();
        }
    }

    ColorRGB src_color = { 1.0f, 1.0f, 1.0f };
    ColorRGB tool_color = {0.0f, 150.0f / 255.0f, 136.0f / 255.0f};
    m_parent.get_selection().render_bounding_box(src_bb, src_color, m_parent.get_scale());
    m_parent.get_selection().render_bounding_box(tool_bb, tool_color, m_parent.get_scale());
}

void GLGizmoMeshBoolean::on_set_state()
{
    if (m_state == EState::On) {
        m_src.reset();
        m_tool.reset();
        // [ORCAPORT:MB-1] These were local copies shadowing the members, so the Delete input checkbox
        // never reset and carried over from the previous operation. Reset them, and the "the user
        // chose a mode" flag with them.
        m_diff_delete_input  = false;
        m_inter_delete_input = false;
        m_operation_chosen   = false;
        m_outcome            = Outcome();
        m_operation_mode = MeshBooleanOperation::Union;
        m_selecting_state = MeshBooleanSelectingState::SelectSource;
    }
    else if (m_state == EState::Off) {
        m_src.reset();
        m_tool.reset();
        m_diff_delete_input  = false;
        m_inter_delete_input = false;
        m_operation_chosen   = false;
        m_operation_mode = MeshBooleanOperation::Undef;
        m_selecting_state = MeshBooleanSelectingState::Undef;
        wxGetApp().notification_manager()->close_plater_warning_notification(warning_text);
    }
}

CommonGizmosDataID GLGizmoMeshBoolean::on_get_requirements() const
{
    if (m_c && m_c->raycaster_ptr()) {
        m_c->raycaster_ptr()->set_only_support_model_part_flag(false);
    }
    return CommonGizmosDataID(
        int(CommonGizmosDataID::SelectionInfo)
        | int(CommonGizmosDataID::InstancesHider)
        | int(CommonGizmosDataID::Raycaster)
        | int(CommonGizmosDataID::ObjectClipper));
}

std::optional<TriangleSelector::SavedPainting> VolumeInfo::save_painting() const
{
    if (wxGetApp().app_config->get_bool("keep_painting")) {
        std::optional<TriangleSelector::SavedPainting> saved_painting = mv->save_painting();
        if (saved_painting) {
            saved_painting->mesh.transform(trafo);
        }
        return saved_painting;
    }

    return {};
}

void GLGizmoMeshBoolean::on_render_input_window(float x, float y, float bottom_limit)
{
    y = std::min(y, bottom_limit - ImGui::GetWindowHeight());

    static float last_y = 0.0f;
    static float last_w = 0.0f;

    const float currt_scale = m_parent.get_scale();
    ImGuiWrapper::push_toolbar_style(currt_scale);
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
    GizmoImguiBegin("MeshBoolean", ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const int max_tab_length = 2 * ImGui::GetStyle().FramePadding.x + std::max(ImGui::CalcTextSize(_u8L("Union").c_str()).x,
        std::max(ImGui::CalcTextSize(_u8L("Difference").c_str()).x, ImGui::CalcTextSize(_u8L("Intersection").c_str()).x));
    const int max_cap_length = ImGui::GetStyle().WindowPadding.x + ImGui::GetStyle().ItemSpacing.x +
                               std::max({ImGui::CalcTextSize(_u8L("Source Volume").c_str()).x,
                                         ImGui::CalcTextSize(_u8L("Tool Volume").c_str()).x,
                                         ImGui::CalcTextSize(_u8L("Subtract from").c_str()).x,
                                         ImGui::CalcTextSize(_u8L("Subtract with").c_str()).x});

    const int select_btn_length = 2 * ImGui::GetStyle().FramePadding.x + std::max(ImGui::CalcTextSize(_u8L("1 selected").c_str()).x, ImGui::CalcTextSize(_u8L("Select").c_str()).x);

    auto selectable = [](const std::string& label, bool selected, const ImVec2& size_arg) {
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, { 0,0 });

        ImGuiWindow* window = ImGui::GetCurrentWindow();
        const ImVec2 label_size = ImGui::CalcTextSize(label.c_str(), NULL, true);
        ImVec2 pos = window->DC.CursorPos;
        ImVec2 size = ImGui::CalcItemSize(size_arg, label_size.x + ImGui::GetStyle().FramePadding.x * 2.0f, label_size.y + ImGui::GetStyle().FramePadding.y * 2.0f);
        bool hovered = ImGui::IsMouseHoveringRect(pos, pos + size);

        if (selected || hovered) {
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 1.f, 1.f, 1.0f));
            ImGui::PushStyleColor(ImGuiCol_Button, { 0, 150.0f / 255.0f, 136.0f / 255.0f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0, 150.0f / 255.0f, 136.0f / 255.0f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0, 150.0f / 255.0f, 136.0f / 255.0f, 1.0f });
        }
        else {
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, { 0, 150.0f / 255.0f, 136.0f / 255.0f, 1.0f });
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, { 0, 150.0f / 255.0f, 136.0f / 255.0f, 1.0f });
        }

        bool res = ImGui::Button(label.c_str(), size_arg);

        if (selected || hovered) {
            ImGui::PopStyleColor(4);
        }
        else {
            ImGui::PopStyleColor(2);
        }

        ImGui::PopStyleVar(1);
        return res;
    };

    auto operate_button = [this](const wxString &label, bool enable) {
        if (!enable) {
            ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
            if (m_is_dark_mode) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(39.0f / 255.0f, 39.0f / 255.0f, 39.0f / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(108.0f / 255.0f, 108.0f / 255.0f, 108.0f / 255.0f, 1.0f));
            }
            else {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(230.0f / 255.0f, 230.0f / 255.0f, 230.0f / 255.0f, 1.0f));
                ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(163.0f / 255.0f, 163.0f / 255.0f, 163.0f / 255.0f, 1.0f));
            }
        }

        bool res = m_imgui->button(label.c_str());

        if (!enable) {
            ImGui::PopItemFlag();
            ImGui::PopStyleColor(2);
        }
        return res;
    };

    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 0);
    if (selectable(_u8L("Union"), m_operation_mode == MeshBooleanOperation::Union, ImVec2(max_tab_length, 0.0f))) {
        // [ORCAPORT:MB-1] Through the setter, so choosing a tab marks the mode as the user's: the
        // picks only steer it while they have not chosen one.
        set_operation(MeshBooleanOperation::Union);
    }
    ImGui::SameLine(0, 0);
    if (selectable(_u8L("Difference"), m_operation_mode == MeshBooleanOperation::Difference, ImVec2(max_tab_length, 0.0f))) {
        set_operation(MeshBooleanOperation::Difference);
    }
    ImGui::SameLine(0, 0);
    if (selectable(_u8L("Intersection"), m_operation_mode == MeshBooleanOperation::Intersection, ImVec2(max_tab_length, 0.0f))) {
        set_operation(MeshBooleanOperation::Intersection);
    }
    ImGui::PopStyleVar();

    ImGui::AlignTextToFramePadding();
    std::string cap_str1 = m_operation_mode != MeshBooleanOperation::Difference ? _u8L("Part 1") : _u8L("Subtract from");
    m_imgui->text(cap_str1);
    ImGui::SameLine(max_cap_length);
    // TRN Mesh Boolean button label once one volume is picked.
    std::string select_src_str = m_src.mv ? _u8L("1 selected") : _u8L("Select");
    select_src_str += "##select_source_volume";
    ImGui::PushItemWidth(select_btn_length);
    if (selectable(select_src_str, m_selecting_state == MeshBooleanSelectingState::SelectSource, ImVec2(select_btn_length, 0)))
        m_selecting_state = MeshBooleanSelectingState::SelectSource;
    ImGui::PopItemWidth();
    if (m_src.mv) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_src.mv->name);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_Border, { 0, 0, 0, 0 });
        if (ImGui::Button((into_u8(ImGui::TextSearchCloseIcon) + "##src").c_str(), {18, 18}))
        {
            m_src.reset();
        }
        ImGui::PopStyleColor(5);
    }

    ImGui::AlignTextToFramePadding();
    std::string cap_str2 = m_operation_mode != MeshBooleanOperation::Difference ? _u8L("Part 2") : _u8L("Subtract with");
    m_imgui->text(cap_str2);
    ImGui::SameLine(max_cap_length);
    std::string select_tool_str = m_tool.mv ? _u8L("1 selected") : _u8L("Select");
    select_tool_str += "##select_tool_volume";
    ImGui::PushItemWidth(select_btn_length);
    if (selectable(select_tool_str, m_selecting_state == MeshBooleanSelectingState::SelectTool, ImVec2(select_btn_length, 0)))
        m_selecting_state = MeshBooleanSelectingState::SelectTool;
    ImGui::PopItemWidth();
    if (m_tool.mv) {
        ImGui::SameLine();
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_tool.mv->name);

        ImGui::SameLine();
        ImGui::PushStyleColor(ImGuiCol_Button, { 0, 0, 0, 0 });
        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImGui::GetStyleColorVec4(ImGuiCol_Button));
        ImGui::PushStyleColor(ImGuiCol_Border, { 0, 0, 0, 0 });
        if (ImGui::Button((into_u8(ImGui::TextSearchCloseIcon) + "tool").c_str(), {18, 18}))
        {
            m_tool.reset();
        }
        ImGui::PopStyleColor(5);
    }

    bool enable_button = m_src.mv && m_tool.mv;
    if (m_operation_mode == MeshBooleanOperation::Union)
    {
        if (operate_button(_L("Union") + "##btn", enable_button))
            perform_operation();
    }
    else if (m_operation_mode == MeshBooleanOperation::Difference) {
        m_imgui->bbl_checkbox(_L("Delete input"), m_diff_delete_input);
        if (operate_button(_L("Difference") + "##btn", enable_button))
            perform_operation();
    }
    else if (m_operation_mode == MeshBooleanOperation::Intersection){
        m_imgui->bbl_checkbox(_L("Delete input"), m_inter_delete_input);
        if (operate_button(_L("Intersection") + "##btn", enable_button))
            perform_operation();
    }

    float win_w = ImGui::GetWindowWidth();
    if (last_w != win_w || last_y != y) {
        // ask canvas for another frame to render the window in the correct position
        m_imgui->set_requires_extra_frame();
        m_parent.set_as_dirty();
        m_parent.request_extra_frame();
        if (last_w != win_w)
            last_w = win_w;
        if (last_y != y)
            last_y = y;
    }

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoMeshBoolean::on_load(cereal::BinaryInputArchive &ar)
{
    ar(m_enable, m_operation_mode, m_selecting_state, m_diff_delete_input, m_inter_delete_input, m_src, m_tool);
    ModelObject *curr_model_object = m_c->selection_info() ? m_c->selection_info()->model_object() : nullptr;
    m_src.mv = curr_model_object == nullptr ? nullptr : m_src.volume_idx < 0 ? nullptr : curr_model_object->volumes[m_src.volume_idx];
    m_tool.mv = curr_model_object == nullptr ? nullptr : m_tool.volume_idx < 0 ? nullptr : curr_model_object->volumes[m_tool.volume_idx];
}

void GLGizmoMeshBoolean::on_save(cereal::BinaryOutputArchive &ar) const
{
    ar(m_enable, m_operation_mode, m_selecting_state, m_diff_delete_input, m_inter_delete_input, m_src, m_tool);
}

// [ORCAPORT:MCP-3] The boolean operation itself, extracted from the button handlers so the MCP tool
// runs the exact same code. Returns false when nothing was produced; the interactive path shows the
// "Unable to perform boolean operation" warning in that case.
bool GLGizmoMeshBoolean::perform_operation()
{
    // [ORCAPORT:MB-1] Record what this attempt did, for the panel/MCP to report. Cleared first, so a
    // refusal never leaves the previous operation's numbers behind.
    m_outcome = Outcome();
    const ModelObject *mo = (m_c != nullptr && m_c->selection_info() != nullptr)
                                ? m_c->selection_info()->model_object()
                                : nullptr;
    m_outcome.volumes_before = mo != nullptr ? int(mo->volumes.size()) : 0;
    // A refusal changes nothing, so the "after" count starts as a copy: reporting 0 would read as the
    // object having lost its volumes.
    m_outcome.volumes_after  = m_outcome.volumes_before;

    if (m_src.mv == nullptr || m_tool.mv == nullptr) {
        m_outcome.message = "Pick a source volume and a tool volume";
        return false;
    }

    const char *boolean_opts =
        m_operation_mode == MeshBooleanOperation::Union        ? "UNION" :
        m_operation_mode == MeshBooleanOperation::Difference   ? "A_NOT_B" :
        m_operation_mode == MeshBooleanOperation::Intersection ? "INTERSECTION" : nullptr;
    if (boolean_opts == nullptr) {
        m_outcome.message = "Pick an operation (Union, Difference or Intersection)";
        return false;
    }

    TriangleMesh temp_src_mesh = m_src.mv->mesh();
    temp_src_mesh.transform(m_src.trafo);
    TriangleMesh temp_tool_mesh = m_tool.mv->mesh();
    temp_tool_mesh.transform(m_tool.trafo);
    m_outcome.triangles_in  = int(temp_src_mesh.its.indices.size());
    const double volume_in  = its_volume(temp_src_mesh.its);
    std::vector<TriangleMesh> temp_mesh_resuls;
    Slic3r::MeshBoolean::mcut::make_boolean(temp_src_mesh, temp_tool_mesh, temp_mesh_resuls, boolean_opts);
    if (temp_mesh_resuls.empty()) {
        m_outcome.message = warning_text;
        wxGetApp().notification_manager()->push_plater_warning_notification(warning_text);
        return false;
    }

    // [ORCAPORT:MB-1] The kernel hands the source back unchanged when a boolean has no resulting
    // fragment (do_boolean keeps the part it could not cut), and that arrives here as a perfectly good
    // result: the volume is replaced by a copy of itself and the tool is consumed, so nothing is cut
    // and something disappears. Say so. The kept part is the source's own mesh copied through mcut, so
    // the comparison is exact.
    m_outcome.triangles_out = int(temp_mesh_resuls.begin()->its.indices.size());
    const indexed_triangle_set &result_its = temp_mesh_resuls.begin()->its;
    if (m_outcome.triangles_out == m_outcome.triangles_in &&
        std::abs(its_volume(result_its) - volume_in) < 1e-6) {
        m_outcome.no_op   = true;
        m_outcome.message = "The boolean produced no change: this source was returned as it was, so "
                            "nothing was cut (the tool may not overlap it)";
    }

    // Union and intersection keep paint from both meshes; difference only from the source.
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings{m_src.save_painting()};
    if (m_operation_mode != MeshBooleanOperation::Difference)
        saved_paintings.push_back(m_tool.save_painting());

    const bool delete_input = m_operation_mode == MeshBooleanOperation::Union        ? true :
                              m_operation_mode == MeshBooleanOperation::Difference   ? m_diff_delete_input :
                                                                                       m_inter_delete_input;
    generate_new_volume(delete_input, *temp_mesh_resuls.begin(), saved_paintings);
    if (m_outcome.no_op)
        wxGetApp().notification_manager()->push_plater_warning_notification(warning_text);
    else
        wxGetApp().notification_manager()->close_plater_warning_notification(warning_text);
    m_outcome.performed     = true;
    m_outcome.volumes_after = mo != nullptr ? int(mo->volumes.size()) : 0;
    return true;
}

// [ORCAPORT:MCP-3] Pick at a screen point through the interactive path: gizmo_event() does the same
// raycast a real click on the canvas does, and this returns whether the requested slot got a volume.
bool GLGizmoMeshBoolean::pick_at(const Vec2d &mouse_position, bool as_tool)
{
    m_selecting_state = as_tool ? MeshBooleanSelectingState::SelectTool : MeshBooleanSelectingState::SelectSource;
    gizmo_event(SLAGizmoEventType::LeftDown, mouse_position, false, false, false);
    return as_tool ? m_tool.mv != nullptr : m_src.mv != nullptr;
}

// [ORCAPORT:MCP-3] Pick a volume by its index in mo->volumes, with no raycast. This is the only way
// to pick a cutter the camera cannot see (one buried inside the part); it also isolates "the pick is
// wrong" from "the boolean is wrong" when a real click misses.
bool GLGizmoMeshBoolean::pick_volume_index(int volume_idx, bool as_tool)
{
    const ModelObject *mo = (m_c != nullptr && m_c->selection_info() != nullptr)
                                ? m_c->selection_info()->model_object()
                                : nullptr;
    if (mo == nullptr || volume_idx < 0 || volume_idx >= int(mo->volumes.size()))
        return false;
    ModelVolume *mv = mo->volumes[volume_idx];
    // [ORCAPORT:MB-1] The same slot rule a click gets: a cutter is a tool, whatever slot was named.
    assign_pick(mv, mv->get_matrix(), volume_idx, as_tool);
    return true;
}

void GLGizmoMeshBoolean::generate_new_volume(const bool delete_input, TriangleMesh& mesh_result, const std::vector<std::optional<TriangleSelector::SavedPainting>>& saved_paintings) {

    wxGetApp().plater()->take_snapshot("Mesh Boolean");

    ModelObject* curr_model_object = m_c->selection_info()->model_object();

    // generate new volume
    ModelVolume* new_volume = curr_model_object->add_volume(std::move(mesh_result));

    // Remap paintings
    for (const auto& saved_painting : saved_paintings) {
        new_volume->restore_painting(saved_painting, true);
    }

    // assign to new_volume from old_volume
    ModelVolume* old_volume = m_src.mv;
    std::string suffix;
    switch (m_operation_mode)
    {
    case MeshBooleanOperation::Union:
        suffix = "union";
        break;
    case MeshBooleanOperation::Difference:
        suffix = "difference";
        break;
    case MeshBooleanOperation::Intersection:
        suffix = "intersection";
        break;
    }
    new_volume->name = old_volume->name + " - " + suffix;
    new_volume->set_new_unique_id();
    new_volume->config.apply(old_volume->config);
    new_volume->set_type(old_volume->type());
    new_volume->set_material_id(old_volume->material_id());
    new_volume->set_offset(old_volume->get_transformation().get_offset());
    //Vec3d translate_z = { 0,0, (new_volume->source.mesh_offset - old_volume->source.mesh_offset).z() };
    //new_volume->translate(new_volume->get_transformation().get_matrix_no_offset() * translate_z);
    //new_volume->supported_facets.assign(old_volume->supported_facets);
    //new_volume->seam_facets.assign(old_volume->seam_facets);
    //new_volume->mmu_segmentation_facets.assign(old_volume->mmu_segmentation_facets);

    // delete old_volume
    std::swap(curr_model_object->volumes[m_src.volume_idx], curr_model_object->volumes.back());
    curr_model_object->delete_volume(curr_model_object->volumes.size() - 1);

    // [ORCAPORT:PP-5] The tool volume is deleted by IDENTITY, not by the recorded index. The swap above
    // moved the last volume into the source's slot, so every index at or past it shifted and
    // `m_tool.volume_idx` - recorded when the tool was picked - can now name a different volume, or the
    // result itself. That is how a Mesh boolean of a perforation came to do nothing once an earlier
    // boolean had already reshuffled the list: the wrong volume was removed, or the result was.
    if (delete_input && m_tool.mv != nullptr) {
        ModelVolume *tool     = m_tool.mv;
        int          tool_idx = -1;
        for (size_t i = 0; i < curr_model_object->volumes.size(); ++i)
            if (curr_model_object->volumes[i] == tool) {
                tool_idx = int(i);
                break;
            }
        if (tool_idx >= 0) {
            std::vector<ItemForDelete> items;
            auto                       obj_idx = m_parent.get_selection().get_object_idx();
            items.emplace_back(ItemType::itVolume, obj_idx, tool_idx);
            wxGetApp().obj_list()->delete_from_model_and_list(items);
        }
    }

    // Both picks named volumes that have been consumed or moved: drop them rather than leave a stale
    // index behind, so the next operation picks again. (The selection-visibility path compares against
    // these indices too, and a stale one hides the wrong volume.)
    m_src.reset();
    m_tool.reset();

    //bool sinking = curr_model_object->bounding_box().min.z() < SINKING_Z_THRESHOLD;
    //if (!sinking)
    //    curr_model_object->ensure_on_bed();
    //curr_model_object->sort_volumes(true);

    wxGetApp().plater()->update();
    wxGetApp().obj_list()->select_item([this, new_volume]() {
        wxDataViewItem sel_item;

        wxDataViewItemArray items = wxGetApp().obj_list()->reorder_volumes_and_get_selection(m_parent.get_selection().get_object_idx(), [new_volume](const ModelVolume* volume) { return volume == new_volume; });
        if (!items.IsEmpty())
            sel_item = items.front();

        return sel_item;
        });

    m_src.reset();
    m_tool.reset();
    m_selecting_state = MeshBooleanSelectingState::SelectSource;
}


}}
