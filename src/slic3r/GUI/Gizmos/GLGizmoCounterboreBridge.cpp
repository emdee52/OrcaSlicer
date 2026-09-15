// [ORCAPORT:PF-2b] Paint-on counterbore bridge gizmo. Adapted from Orca's GLGizmoFuzzySkin
// (same painter base) and preFlight's GLGizmoCounterboreBridge.
#include "GLGizmoCounterboreBridge.hpp"

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "GLGizmoUtils.hpp"

#include <glad/gl.h>

namespace Slic3r::GUI {

GLGizmoCounterboreBridge::GLGizmoCounterboreBridge(GLCanvas3D &parent, const std::string &icon_filename,
                                                   unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id)
{
}

bool GLGizmoCounterboreBridge::on_init()
{
    m_shortcut_key = WXK_CONTROL_K;

    m_desc["mode"]           = _L("Bridge type");
    m_desc["smart"]          = _L("Smart (stepped)");
    m_desc["partial"]        = _L("Partial");
    m_desc["clipping_of_view"] = _L("Section view");
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Brush size");
    m_desc["erase_all"]        = _L("Erase all");
    return true;
}

std::string GLGizmoCounterboreBridge::on_get_name() const
{
    return _u8L("Counterbore bridge");
}

void GLGizmoCounterboreBridge::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

void GLGizmoCounterboreBridge::render_painter_gizmo()
{
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoCounterboreBridge::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
#if BBS_TOOLBAR_ON_TOP
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(7.0f);
    const float left_width    = m_imgui->calc_text_size(m_desc.at("mode")).x + m_imgui->scaled(1.5f);

    // Bridge type: smart stepped vs partial.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("mode"));
    ImGui::SameLine(left_width);
    if (m_imgui->button(m_desc.at("smart")))
        m_partial = false;
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("partial")))
        m_partial = true;
    ImGui::SameLine();
    m_imgui->text(m_partial ? _L("(partial: single bridge layer)") : _L("(smart: stepped ramp)"));

    ImGui::Separator();

    // Brush size.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("cursor_size"));
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax,
                                    "%.2f", 1.0f, true);

    ImGui::Separator();

    // Section view.
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

    ImGui::Separator();

    m_imgui->disabled_begin(mo->is_counterbore_bridge_painted() == false);
    if (m_imgui->button(m_desc.at("erase_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), _u8L("Reset selection"), UndoRedo::SnapshotType::GizmoAction);
        int idx = -1;
        for (ModelVolume *mv : mo->volumes)
            if (mv->is_model_part()) {
                ++idx;
                m_triangle_selectors[idx]->reset();
                m_triangle_selectors[idx]->request_update_render_data(true);
            }
        update_model_object();
        m_parent.set_as_dirty();
    }
    m_imgui->disabled_end();

    ImGui::SameLine();
    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (m_imgui->button(_L("Done")))
        m_parent.reset_all_gizmos();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoCounterboreBridge::update_model_object()
{
    bool         updated = false;
    ModelObject *mo      = m_c->selection_info()->model_object();
    int          idx     = -1;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->counterbore_bridge_facets.set(*m_triangle_selectors[idx]);
    }
    if (updated) {
        const ModelObjectPtrs &mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());
        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }
}

void GLGizmoCounterboreBridge::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject *mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();

    std::vector<ColorRGBA> ebt_colors;
    ebt_colors.push_back(GLVolume::NEUTRAL_COLOR);
    ebt_colors.push_back(TriangleSelectorGUI::enforcers_color); // smart
    ebt_colors.push_back(TriangleSelectorGUI::blockers_color);  // partial

    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        const TriangleMesh *mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        m_triangle_selectors.back()->deserialize(mv->counterbore_bridge_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();
    }
}

PainterGizmoType GLGizmoCounterboreBridge::get_painter_type() const
{
    return PainterGizmoType::COUNTERBORE_BRIDGE;
}

wxString GLGizmoCounterboreBridge::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    return shift_down ? _L("Remove counterbore bridge") : _L("Add counterbore bridge");
}

} // namespace Slic3r::GUI
