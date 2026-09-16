#include "GLGizmoFdmSupports.hpp"

#include "libslic3r/Model.hpp"
//BBS
#include "libslic3r/Layer.hpp"
#include "libslic3r/Thread.hpp"
#include "libslic3r/OrcaExt/SupportPaintTypes.hpp"
#include "libslic3r/OrcaExt/SupportAutoPaint.hpp" // [ORCAPORT:PF-10-auto]

//#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/Utils/UndoRedo.hpp"
#include "GLGizmoUtils.hpp"

#include <glad/gl.h>

#include <algorithm>

#include <boost/log/trivial.hpp>

namespace Slic3r::GUI {

namespace {
// [ORCAPORT:PF-10-paint] Localize a registry label using the existing translated terms.
wxString localized_support_label(const Slic3r::OrcaExt::SupportPaintType &t)
{
    const std::string l = t.label != nullptr ? t.label : "";
    if (l == "Default") return _L("Default");
    if (l == "Snug")    return _L("Snug");
    if (l == "Grid")    return _L("Grid");
    if (l == "Organic") return _L("Organic");
    if (l == "NeoWave") return _L("NeoWave");
    return wxString::FromUTF8(l);
}
} // namespace

GLGizmoFdmSupports::GLGizmoFdmSupports(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoPainterBase(parent, icon_filename, sprite_id), m_current_tool(ImGui::CircleButtonIcon)
{
    m_tool_type = ToolType::BRUSH;
    m_cursor_type = TriangleSelector::CursorType::CIRCLE;
}

void GLGizmoFdmSupports::on_shutdown()
{
    //BBS
    //wait the thread
    if (m_thread.joinable()) {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
        //join the thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 2000 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(2000));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }

    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;

    m_highlight_by_angle_threshold_deg = 0.f;
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
}

//BBS: add on_open
void GLGizmoFdmSupports::on_opening()
{
    m_angle_threshold_deg = 40;
    m_highlight_by_angle_threshold_deg = 30.f; // [ORCAPORT:PF-10-paint] default overhang highlight
    m_paint_on_overhangs_only = true;          // [ORCAPORT:PF-10-paint] paint overhangs only by default
    m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
    if (! m_parent.is_using_slope()) {
        m_parent.use_slope(true);
        m_parent.set_as_dirty();
    }
    m_print_instance.print_object = NULL;
    m_print_instance.model_instance = NULL;
    m_edit_state = state_idle;

    m_volume_ready = false;
    m_volume_valid = false;
}

std::string GLGizmoFdmSupports::on_get_name() const
{
    return _u8L("Support Painting");
}

bool GLGizmoFdmSupports::on_init()
{
    // BBS
    m_shortcut_key = WXK_CONTROL_L;

    m_desc["perform"]            = _L("Apply");
    m_desc["on_overhangs_only"]  = _L("On highlighted overhangs only");
    m_desc["remove_all"]         = _L("Erase all");
    m_desc["highlight_by_angle"] = _L("Highlight overhangs");
    m_desc["tool_type"]          = _L("Tool type");
    m_desc["support_type"]       = _L("Support type"); // [ORCAPORT:PF-10-paint]
    m_desc["autopaint"]          = _L("Auto paint");         // [ORCAPORT:PF-10-auto]
    m_desc["autopaint_types"]    = _L("Automatic types");    // [ORCAPORT:PF-10-auto]
    m_desc["autopaint_min_area"] = _L("Min overhang area");  // [ORCAPORT:PF-10-auto]
    m_desc["perform_remap"]      = _L("Remap support types"); // [ORCAPORT:PF-10-paint]
    m_desc["remap"]              = _L("Remap");               // [ORCAPORT:PF-10-paint]
    m_desc["remap_from"]         = _L("From");                // [ORCAPORT:PF-10-paint]
    m_desc["remap_to"]           = _L("To");                  // [ORCAPORT:PF-10-paint]
    m_desc["gap_fill"]           = _L("Gap fill");
    m_desc["reset_direction"]    = _L("Reset direction");
    m_desc["clipping_of_view"]   = _L("Section view");
    m_desc["cursor_size"]        = _L("Brush size");
    m_desc["smart_fill_angle"]   = _L("Smart fill angle");
    m_desc["gap_area"]           = _L("Gap area");


    const wxString ctrl  = GUI::shortkey_ctrl_prefix();
    const wxString alt   = GUI::shortkey_alt_prefix();
    const wxString shift = GUI::shortkey_shift_prefix();

    std::pair<wxString, wxString> enforce_shortcut  = {_L("Left mouse button"),         _L("Enforce supports")};
    std::pair<wxString, wxString> block_shortcut    = {_L("Right mouse button"),        _L("Block supports")};
    std::pair<wxString, wxString> remove_shortcut   = {shift + _L("Left mouse button"), _L("Erase")};
    std::pair<wxString, wxString> clipping_shortcut = {alt + _L("Mouse wheel"),         m_desc["clipping_of_view"]};

    m_shortcuts_brush = {
        enforce_shortcut, 
        block_shortcut, 
        remove_shortcut,
        {ctrl + _L("Mouse wheel"), m_desc["cursor_size"]},
        clipping_shortcut
    };

    m_shortcuts_bucket_fill = {
        enforce_shortcut, 
        block_shortcut, 
        remove_shortcut,
        {ctrl + _L("Mouse wheel"),  m_desc["smart_fill_angle"]},
        clipping_shortcut
    };

    m_shortcuts_gap_fill = {
        {ctrl + _L("Mouse wheel"), _L("Gap area")}
    };

    m_print_instance = PrintInstance();
    return true;
}

void GLGizmoFdmSupports::render_painter_gizmo()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    //BBS: draw support volumes
    if (m_volume_ready && m_support_volume && (m_edit_state != state_generating))
    {
        // TODO: FIXME
        m_support_volume->set_render_color({0.f, 0.7f, 0.f, 0.7f});
        m_support_volume->render();
    }

    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

// BBS
bool GLGizmoFdmSupports::on_key_down_select_tool_type(int keyCode) {
    switch (keyCode)
    {
    case 'F':
        m_current_tool = ImGui::FillButtonIcon;
        break;
    case 'S':
        m_current_tool = ImGui::SphereButtonIcon;
        break;
    case 'C':
        m_current_tool = ImGui::CircleButtonIcon;
        break;
    case 'G':
        m_current_tool = ImGui::GapFillIcon;
        break;
    default:
        return false;
        break;
    }
    return true;
}

void GLGizmoFdmSupports::on_set_state()
{
    GLGizmoPainterBase::on_set_state();

    if (get_state() == On) {
        m_support_threshold_angle = -1;
    }
    else if (get_state() == Off) {
        ModelObject* mo = m_c->selection_info()->model_object();
        if (mo) Slic3r::save_object_mesh(*mo);
    }
}

void GLGizmoFdmSupports::on_render_input_window(float x, float y, float bottom_limit)
{
    init_print_instance();
    if (! m_c->selection_info()->model_object())
        return;

    float  scale       = m_parent.get_scale();
    #ifdef WIN32
        int dpi = get_dpi_for_window(wxGetApp().GetTopWindow());
        scale *= (float) dpi / (float) DPI_DEFAULT;
    #endif // WIN32

    // BBS
    wchar_t old_tool = m_current_tool;

    int support_threshold_angle = get_selection_support_threshold_angle();
    // when support painting tool is on, reset highlight threshold angle
    if (m_support_threshold_angle == -1) {
        // [ORCAPORT:PF-10-paint] Default the overhang highlight to 30 deg when the gizmo opens.
        m_highlight_by_angle_threshold_deg = 30.f;
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
    }
    m_support_threshold_angle = support_threshold_angle;

    const float approx_height = m_imgui->scaled(23.f);
    y = std::min(y, bottom_limit - approx_height);

    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);

    //BBS
    ImGuiWrapper::push_toolbar_style(m_parent.get_scale());
    float f_scale = m_parent.get_gizmos_manager().get_layout_scale();
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(6.0f, 4.0f * f_scale));

    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // First calculate width of all the texts that are could possibly be shown. We will decide set the dialog width based on that:
    const float space_size = m_imgui->get_style_scaling() * 8;
    const float clipping_slider_left    = m_imgui->calc_text_size(m_desc.at("clipping_of_view")).x + m_imgui->scaled(1.5f);
    const float cursor_slider_left      = m_imgui->calc_text_size(m_desc.at("cursor_size")).x + m_imgui->scaled(1.5f);
    const float gap_fill_slider_left    = m_imgui->calc_text_size(m_desc.at("gap_fill")).x + m_imgui->scaled(1.5f);
    const float highlight_slider_left   = m_imgui->calc_text_size(m_desc.at("highlight_by_angle")).x + m_imgui->scaled(1.5f);
    const float reset_button_slider_left = m_imgui->calc_text_size(m_desc.at("reset_direction")).x + m_imgui->scaled(1.5f) + ImGui::GetStyle().FramePadding.x * 2;
    const float on_overhangs_only_width  = m_imgui->calc_text_size(m_desc["on_overhangs_only"]).x + m_imgui->scaled(1.5f);
    const float filter_btn_width        = m_imgui->calc_text_size(m_desc.at("perform")).x + m_imgui->scaled(1.5f);
    const float gap_area_txt_width = m_imgui->calc_text_size(m_desc.at("gap_area")).x + m_imgui->scaled(1.5f);
    const float smart_fill_angle_txt_width = m_imgui->calc_text_size(m_desc.at("smart_fill_angle")).x + m_imgui->scaled(1.5f);
    const float buttons_width           = filter_btn_width + m_imgui->scaled(1.5f);
    const float empty_button_width      = m_imgui->calc_button_size("").x;

    const float tips_width           = m_imgui->calc_text_size(_L("Auto support threshold angle: ") + " 90 ").x + m_imgui->scaled(1.5f);
    const float minimal_slider_width = m_imgui->scaled(4.f);

    float caption_max    = 0.f;
    float total_text_max = 0.f;
    for (const auto &t : std::array<std::string, 5>{"enforce", "block", "remove", "cursor_size", "clipping_of_view"}) {
        caption_max    = std::max(caption_max, m_imgui->calc_text_size(m_desc[t + "_caption"]).x);
        total_text_max = std::max(total_text_max, m_imgui->calc_text_size(m_desc[t]).x);
    }
    total_text_max += caption_max + m_imgui->scaled(1.f);
    caption_max += m_imgui->scaled(1.f);

    const float sliders_left_width = std::max(gap_area_txt_width, std::max(smart_fill_angle_txt_width, std::max(reset_button_slider_left, std::max(std::max(cursor_slider_left, clipping_slider_left), std::max(highlight_slider_left, gap_fill_slider_left)))));
    const float slider_icon_width  = m_imgui->get_slider_icon_size().x;
    const float max_tooltip_width = ImGui::GetFontSize() * 20.0f;

    const float sliders_width = m_imgui->scaled(7.0f);
    const float drag_left_width = ImGui::GetStyle().WindowPadding.x + sliders_width - space_size;

    float drag_pos_times     = 0.7;

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("tool_type"));
    std::array<wchar_t, 4> tool_ids = { ImGui::CircleButtonIcon, ImGui::SphereButtonIcon, ImGui::FillButtonIcon, ImGui::GapFillIcon };
    std::array<wchar_t, 4> icons;
    if (m_is_dark_mode)
        icons = { ImGui::CircleButtonDarkIcon, ImGui::SphereButtonDarkIcon, ImGui::FillButtonDarkIcon, ImGui::GapFillDarkIcon };
    else
        icons = { ImGui::CircleButtonIcon, ImGui::SphereButtonIcon, ImGui::FillButtonIcon, ImGui::GapFillIcon };

    std::array<wxString, 4> tool_tips = { _L("Circle"), _L("Sphere"), _L("Fill"), _L("Gap Fill") };
    for (int i = 0; i < tool_ids.size(); i++) {
        //std::string  str_label = std::string("##");
        //std::wstring btn_name = icons[i] + boost::nowide::widen(str_label);

        if (i != 0) ImGui::SameLine((empty_button_width + m_imgui->scaled(1.75f)) * i + m_imgui->scaled(1.3f));

        bool is_active = m_current_tool == tool_ids[i];
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 1.f);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding  , 3.f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FramePadding   , ImVec2(4.f * scale, 4.f * scale));
        ImGui::PushStyleColor(ImGuiCol_Text         , ImVec4(1,1,1,1)); // ORCA Fixes icon rendered without colors while using Light theme
        ImGui::PushStyleColor(ImGuiCol_Button       , is_active ? ImVec4(0.f, .59f, .53f, .25f) : ImVec4(0,0,0,0));         // ORCA
        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, is_active ? ImVec4(0.f, .59f, .53f, .25f) : ImVec4(.6f,.6f,.6f,.2f)); // ORCA
        ImGui::PushStyleColor(ImGuiCol_ButtonActive , is_active ? ImVec4(0.f, .59f, .53f, .30f) : ImVec4(0,0,0,0));         // ORCA
        ImGui::PushStyleColor(ImGuiCol_Border       , is_active ? ImGuiWrapper::COL_ORCA        : ImVec4(0,0,0,0));         // ORCA
        ImGui::PushStyleColor(ImGuiCol_BorderActive , is_active ? ImGuiWrapper::COL_ORCA        : ImVec4(0,0,0,0));         // ORCA matched color for fixing flicker on click
        bool btn_clicked = m_imgui->glyph_button(icons[i], ImVec2(16.f  * scale, 16.f  * scale)); // ORCA glyph_button for fixing unequal paddings
        ImGui::PopStyleColor(6);
        ImGui::PopStyleVar(3);

        if (btn_clicked && m_current_tool != tool_ids[i]) {
            m_current_tool = tool_ids[i];
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        }

        if (ImGui::IsItemHovered()) {
            m_imgui->tooltip(tool_tips[i], max_tooltip_width);
        }
    }

    if (m_current_tool != old_tool)
        this->tool_changed(old_tool, m_current_tool);

    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));

    // Brush / fill settings for the selected tool, grouped directly under the tool selector.
    if (m_current_tool == ImGui::CircleButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::CIRCLE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(drag_left_width + sliders_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");

        if (m_imgui->bbl_checkbox(_L("Vertical"), m_vertical_only)) {
            if (m_vertical_only) {
                m_horizontal_only = false;
            }
        }
        if (m_imgui->bbl_checkbox(_L("Horizontal"), m_horizontal_only)) {
            if (m_horizontal_only) {
                m_vertical_only = false;
            }
        }
    } else if (m_current_tool == ImGui::SphereButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::SPHERE;
        m_tool_type = ToolType::BRUSH;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("cursor_size"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        m_imgui->bbl_slider_float_style("##cursor_radius", &m_cursor_radius, CursorRadiusMin, CursorRadiusMax, "%.2f", 1.0f, true);
        ImGui::SameLine(drag_left_width + sliders_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##cursor_radius_input", &m_cursor_radius, 0.05f, 0.0f, 0.0f, "%.2f");

        if (m_imgui->bbl_checkbox(_L("Vertical"), m_vertical_only)) {
            if (m_vertical_only) {
                m_horizontal_only = false;
            }
        }
        if (m_imgui->bbl_checkbox(_L("Horizontal"), m_horizontal_only)) {
            if (m_horizontal_only) {
                m_vertical_only = false;
            }
        }
    } else if (m_current_tool == ImGui::FillButtonIcon) {
        m_cursor_type = TriangleSelector::CursorType::POINTER;
        m_tool_type = ToolType::SMART_FILL;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["smart_fill_angle"]);
        std::string format_str = std::string("%.f") + I18N::translate_utf8("°",
                                                                            "Face angle threshold,"
                                                                            "placed after the number with no whitespace in between.");
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        if (m_imgui->bbl_slider_float_style("##smart_fill_angle", &m_smart_fill_angle, SmartFillAngleMin, SmartFillAngleMax,
                                            format_str.data(), 1.0f, true))
            for (auto& triangle_selector : m_triangle_selectors) {
                triangle_selector->seed_fill_unselect_all_triangles();
                triangle_selector->request_update_render_data();
            }
        ImGui::SameLine(drag_left_width + sliders_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##smart_fill_angle_input", &m_smart_fill_angle, 0.05f, 0.0f, 0.0f, "%.2f");
    } else if (m_current_tool == ImGui::GapFillIcon) {
        m_tool_type = ToolType::GAP_FILL;
        m_cursor_type = TriangleSelector::CursorType::POINTER;

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc["gap_area"]);
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        std::string format_str = std::string("%.2f") + I18N::translate_utf8("", "Triangle patch area threshold,""triangle patch will be merged to neighbor if its area is less than threshold");
        m_imgui->bbl_slider_float_style("##gap_area", &TriangleSelectorPatch::gap_area, TriangleSelectorPatch::GapAreaMin, TriangleSelectorPatch::GapAreaMax, format_str.data(), 1.0f, true);
        ImGui::SameLine(drag_left_width + sliders_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##gap_area_input", &TriangleSelectorPatch::gap_area, 0.05f, 0.0f, 0.0f, "%.2f");

        // Apply gap fill button
        if (m_imgui->button(m_desc.at("perform"))) {
            Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset selection", UndoRedo::SnapshotType::GizmoAction);

            for (int i = 0; i < m_triangle_selectors.size(); i++) {
                TriangleSelectorPatch* ts_mm = dynamic_cast<TriangleSelectorPatch*>(m_triangle_selectors[i].get());
                ts_mm->update_selector_triangles();
                ts_mm->request_update_render_data(true);
            }
            update_model_object();
            m_parent.set_as_dirty();
        }
    }

    ImGui::Separator();

    // [ORCAPORT:PF-10-paint] Paintable support type, driven by the OrcaExt registry so a new
    // support type only needs a table row (see OrcaExt/SupportPaintTypes.hpp). Each type is shown
    // with its prepare-view colour; right mouse button always paints a blocker.
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("support_type"));
    {
        const std::vector<Slic3r::OrcaExt::SupportPaintType> &types = Slic3r::OrcaExt::support_paint_types();
        // The radio circle is drawn with diameter GetFrameHeight() (text + frame padding), centred
        // on the row, so a swatch of the same size on the same row lines up with it exactly.
        const float swatch = ImGui::GetFrameHeight();
        for (size_t i = 0; i < types.size(); ++i) {
            if (i != 0)
                ImGui::SameLine();
            ImGui::PushID(int(i));
            ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f * scale);
            ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
            ImGui::ColorButton("##swatch",
                               ImVec4(types[i].color[0], types[i].color[1], types[i].color[2], types[i].color[3]),
                               ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoAlpha,
                               ImVec2(swatch, swatch));
            ImGui::PopStyleVar(2);
            ImGui::SameLine();
            if (m_imgui->radio_button(localized_support_label(types[i]), m_enforcer_type == types[i].state) &&
                m_enforcer_type != types[i].state) {
                m_enforcer_type = types[i].state;
                for (auto &triangle_selector : m_triangle_selectors) {
                    triangle_selector->seed_fill_unselect_all_triangles();
                    triangle_selector->request_update_render_data();
                }
            }
            ImGui::PopID();
        }

        ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));

        // [ORCAPORT:PF-10-paint] Swap one painted support type for another across the object.
        if (ImGui::TreeNodeEx(m_desc.at("perform_remap").c_str(),
                              ImGuiTreeNodeFlags_SpanAvailWidth | ImGuiTreeNodeFlags_FramePadding)) {
            render_support_remap_ui(scale);
            ImGui::TreePop();
        }
    }

    ImGui::Separator();

    // [ORCAPORT:PF-10-auto] Automatic painting: a synchronous mesh analysis of the object's
    // overhang regions, classified by the registry's scoring rules. The checkbox row chooses which
    // support types automatic painting may select.
    ImGui::Dummy(ImVec2(0.0f, ImGui::GetFontSize() * 0.1));
    m_imgui->disabled_begin(m_c->selection_info()->model_object() == nullptr);
    if (m_imgui->button(m_desc.at("autopaint")))
        run_auto_paint();
    m_imgui->disabled_end();

    {
        const std::vector<Slic3r::OrcaExt::SupportPaintType> &types = Slic3r::OrcaExt::support_paint_types();
        if (!m_auto_paint_types_initialized) {
            for (const Slic3r::OrcaExt::SupportPaintType &t : types)
                if (!t.rules.empty() && t.auto_enabled_by_default)
                    m_auto_paint_types.push_back(t.state);
            m_auto_paint_types_initialized = true;
        }
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("autopaint_types"));
        bool first = true;
        for (const Slic3r::OrcaExt::SupportPaintType &t : types) {
            if (t.rules.empty())
                continue;
            if (!first)
                ImGui::SameLine();
            first = false;
            auto it    = std::find(m_auto_paint_types.begin(), m_auto_paint_types.end(), t.state);
            bool enabled = it != m_auto_paint_types.end();
            if (m_imgui->bbl_checkbox(localized_support_label(t), enabled)) {
                if (enabled)
                    m_auto_paint_types.push_back(t.state);
                else
                    m_auto_paint_types.erase(std::remove(m_auto_paint_types.begin(), m_auto_paint_types.end(), t.state),
                                             m_auto_paint_types.end());
            }
        }
    }

    {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("autopaint_min_area"));
        ImGui::SameLine(sliders_left_width);
        ImGui::PushItemWidth(sliders_width);
        m_imgui->bbl_slider_float_style("##autopaint_min_area", &m_auto_paint_min_area, 0.f, 20.f, "%.1f", 1.0f, true);
        ImGui::SameLine(drag_left_width + sliders_left_width);
        ImGui::PushItemWidth(1.5 * slider_icon_width);
        ImGui::BBLDragFloat("##autopaint_min_area_input", &m_auto_paint_min_area, 0.1f, 0.0f, 20.0f, "%.1f");
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(_L("Overhang regions smaller than this area, in square millimeters, are left "
                                "unpainted; a tiny sliver cannot hold a support tip."),
                             max_tooltip_width);
    }

    ImGui::Separator();
    float position_before_text_y = ImGui::GetCursorPos().y;
    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc["highlight_by_angle"]);
    ImGui::AlignTextToFramePadding();
    float position_after_text_y = ImGui::GetCursorPos().y;
    ImGui::SameLine(sliders_left_width);

    float slider_height = m_imgui->get_slider_float_height();
    // Makes slider to be aligned to bottom of the multi-line text.
    float slider_start_position_y = std::max(position_before_text_y, position_after_text_y - slider_height);
    ImGui::SetCursorPosY(slider_start_position_y);

    std::string format_str = std::string("%.f");
    ImGui::PushItemWidth(sliders_width);
    wxString tooltip = _L("Highlight faces according to overhang angle.");
    if (m_imgui->bbl_slider_float_style("##angle_threshold_deg", &m_highlight_by_angle_threshold_deg, 0.f, 90.f, format_str.data(), 1.0f, true, tooltip)) {
        m_parent.set_slope_normal_angle(90.f - m_highlight_by_angle_threshold_deg);
        if (!m_parent.is_using_slope()) {
            m_parent.use_slope(true);
            m_parent.set_as_dirty();
        }
    }
    if (ImGui::IsItemHovered()) {
        ImGui::BeginTooltip();
        if (m_support_threshold_angle != 0) {
            wxString str_tooltip = (_L("Auto support threshold angle: ") + std::to_string(m_support_threshold_angle));
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, str_tooltip);
        } else {
            wxString s_tooltip = (_L("No auto support"));
            m_imgui->text_colored(ImGuiWrapper::COL_WINDOW_BG, s_tooltip);
        }
        ImGui::EndTooltip();
    }
    ImGui::SameLine(drag_left_width + sliders_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    ImGui::BBLDragFloat("##angle_threshold_deg_input", &m_highlight_by_angle_threshold_deg, 0.05f, 0.0f, 0.0f, "%.2f");

    if (m_current_tool != ImGui::GapFillIcon) {
        m_imgui->bbl_checkbox(m_desc["on_overhangs_only"], m_paint_on_overhangs_only);
        if (ImGui::IsItemHovered())
            m_imgui->tooltip(format_wxstr(_L("Allows painting only on facets selected by: \"%1%\""), m_desc["highlight_by_angle"]),
                             max_tooltip_width);
    }

    ImGui::Separator();
    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    }
    else {
        if (m_imgui->button(m_desc.at("reset_direction"))) {
            wxGetApp().CallAfter([this]() {
                    m_c->object_clipper()->set_position_by_ratio(-1., false);
                });
        }
    }
    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(sliders_left_width);
    ImGui::PushItemWidth(sliders_width);
    bool b_bbl_slider_float = m_imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true);

    ImGui::SameLine(drag_left_width + sliders_left_width);
    ImGui::PushItemWidth(1.5 * slider_icon_width);
    bool b_drag_input = ImGui::BBLDragFloat("##clp_dist_input", &clp_dist, 0.05f, 0.0f, 0.0f, "%.2f");

    if (b_bbl_slider_float || b_drag_input) m_c->object_clipper()->set_position_by_ratio(clp_dist, true);
    
    ImGui::Separator();

    render_tooltip_button(x, y);

    ImGui::SameLine();
    m_imgui->disabled_begin(m_c->selection_info()->model_object()->is_fdm_support_painted() == false);
    if (m_imgui->button(m_desc.at("remove_all"))) {
        Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Reset selection", UndoRedo::SnapshotType::GizmoAction);
        ModelObject *        mo  = m_c->selection_info()->model_object();
        int                  idx = -1;
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
    if (m_imgui->button(_L("Done"))) {
        m_parent.reset_all_gizmos();
    }

    GizmoImguiEnd();

    // BBS
    ImGui::PopStyleVar(1); // ImGuiStyleVar_FramePadding
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoFdmSupports::tool_changed(wchar_t old_tool, wchar_t new_tool)
{
    if ((old_tool == ImGui::GapFillIcon && new_tool == ImGui::GapFillIcon) ||
        (old_tool != ImGui::GapFillIcon && new_tool != ImGui::GapFillIcon))
        return;

    for (auto& selector_ptr : m_triangle_selectors) {
        TriangleSelectorPatch* tsp = dynamic_cast<TriangleSelectorPatch*>(selector_ptr.get());
        tsp->set_filter_state(new_tool == ImGui::GapFillIcon);
    }
}

void GLGizmoFdmSupports::render_tooltip_button(float x, float y) {
    auto get_shortcuts = [this]() -> std::vector<std::pair<wxString, wxString>> {
        switch (m_current_tool) {
            case ImGui::CircleButtonIcon:
            case ImGui::SphereButtonIcon:
                return m_shortcuts_brush;
            case ImGui::FillButtonIcon:
                return m_shortcuts_bucket_fill;
            case ImGui::GapFillIcon:
                return m_shortcuts_gap_fill;
            default:
                return {};
        }
    };
    
    GLGizmoUtils::render_tooltip_button(m_imgui, m_parent, get_shortcuts(), x, y);
}

// BBS
int GLGizmoFdmSupports::get_selection_support_threshold_angle()
{
    auto sel_info = m_c->selection_info();
    if (sel_info == nullptr)
        return -1;

    const DynamicPrintConfig& obj_cfg = sel_info->model_object()->config.get();
    const DynamicPrintConfig& glb_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
    bool enable_support = obj_cfg.option("enable_support") ? obj_cfg.opt_bool("enable_support") : glb_cfg.opt_bool("enable_support");
    SupportType support_type = obj_cfg.option("support_type") ? obj_cfg.opt_enum<SupportType>("support_type") : glb_cfg.opt_enum<SupportType>("support_type");
    int support_threshold_angle = obj_cfg.option("support_threshold_angle") ? obj_cfg.opt_int("support_threshold_angle") : glb_cfg.opt_int("support_threshold_angle");

    bool auto_support = enable_support && is_auto(support_type);
    return auto_support ? support_threshold_angle : 0;
}

void GLGizmoFdmSupports::select_facets_by_angle(float threshold_deg, bool block)
{
    float threshold = (float(M_PI)/180.f)*threshold_deg;
    const Selection& selection = m_parent.get_selection();
    const ModelObject* mo = m_c->selection_info()->model_object();
    const ModelInstance* mi = mo->instances[selection.get_instance_idx()];

    int mesh_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++mesh_id;

        const Transform3d trafo_matrix = mi->get_matrix_no_offset() * mv->get_matrix_no_offset();
        Vec3f down  = (trafo_matrix.inverse() * (-Vec3d::UnitZ())).cast<float>().normalized();
        Vec3f limit = (trafo_matrix.inverse() * Vec3d(std::sin(threshold), 0, -std::cos(threshold))).cast<float>().normalized();

        float dot_limit = limit.dot(down);

        // Now calculate dot product of vert_direction and facets' normals.
        int idx = 0;
        const indexed_triangle_set &its = mv->mesh().its;
        for (const stl_triangle_vertex_indices &face : its.indices) {
            if (its_face_normal(its, face).dot(down) > dot_limit) {
                m_triangle_selectors[mesh_id]->set_facet(idx, block ? EnforcerBlockerType::BLOCKER : m_enforcer_type); // [ORCAPORT:PF-10-paint]
                m_triangle_selectors.back()->request_update_render_data();
            }
            ++ idx;
        }
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), block ? "Block supports by angle"
                                                    : "Add supports by angle");
    update_model_object();
    m_parent.set_as_dirty();
}

//BBS: remove const
void GLGizmoFdmSupports::update_model_object()
{
    bool updated = false;
    ModelObject* mo = m_c->selection_info()->model_object();
    int idx = -1;
    for (ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;
        ++idx;
        updated |= mv->supported_facets.set(*m_triangle_selectors[idx].get());
    }

    if (updated) {
        const ModelObjectPtrs& mos = wxGetApp().model().objects;
        wxGetApp().obj_list()->update_info_items(std::find(mos.begin(), mos.end(), mo) - mos.begin());

        m_parent.post_event(SimpleEvent(EVT_GLCANVAS_SCHEDULE_BACKGROUND_PROCESS));
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

//BBS
void GLGizmoFdmSupports::update_from_model_object(bool first_update)
{
    wxBusyCursor wait;

    const ModelObject* mo = m_c->selection_info()->model_object();
    m_triangle_selectors.clear();
    //BBS: add timestamp logic
    m_volume_timestamps.clear();

    int volume_id = -1;
    // [ORCAPORT:PF-10-paint] Colour table indexed by paint state, covering every registered
    // support type (the patch renderer indexes by state), plus the legacy enforcer/blocker.
    std::vector<ColorRGBA> ebt_colors;
    {
        size_t max_state = (size_t) EnforcerBlockerType::BLOCKER;
        for (const auto &t : Slic3r::OrcaExt::support_paint_types())
            max_state = std::max(max_state, (size_t) t.state);
        ebt_colors.assign(max_state + 1, GLVolume::NEUTRAL_COLOR);
        ebt_colors[(size_t) EnforcerBlockerType::ENFORCER] = TriangleSelectorGUI::enforcers_color;
        ebt_colors[(size_t) EnforcerBlockerType::BLOCKER]  = TriangleSelectorGUI::blockers_color;
        for (const auto &t : Slic3r::OrcaExt::support_paint_types())
            ebt_colors[(size_t) t.state] = ColorRGBA(t.color[0], t.color[1], t.color[2], t.color[3]);
    }
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;

        // This mesh does not account for the possible Z up SLA offset.
        const TriangleMesh* mesh = &mv->mesh();
        m_triangle_selectors.emplace_back(std::make_unique<TriangleSelectorPatch>(*mesh, ebt_colors));
        // Reset of TriangleSelector is done inside TriangleSelectorGUI's constructor, so we don't need it to perform it again in deserialize().
        m_triangle_selectors.back()->deserialize(mv->supported_facets.get_data(), false);
        m_triangle_selectors.back()->request_update_render_data();

        //BBS: add timestamp logic
        m_volume_timestamps.emplace_back(mv->supported_facets.timestamp());
    }

    //BBS: invalid volume_support status
    invalid_support_volumes(true);
}

PainterGizmoType GLGizmoFdmSupports::get_painter_type() const
{
    return PainterGizmoType::FDM_SUPPORTS;
}

wxString GLGizmoFdmSupports::handle_snapshot_action_name(bool shift_down, GLGizmoPainterBase::Button button_down) const
{
    // BBS remove _L()
    wxString action_name;
    if (shift_down)
        action_name = ("Unselect all");
    else {
        if (button_down == Button::Left)
            action_name = ("Enforce supports");
        else
            action_name = ("Block supports");
    }
    return action_name;
}

//BBS
void GLGizmoFdmSupports::init_print_instance()
{
    const PrintObject* print_object = NULL;
    PrintInstance print_instance = { 0 };
    const Print *print = m_parent.fff_print();

    if (!m_c->selection_info() || (m_print_instance.print_object))
    {
        //no selection or already got a print instance before
        return;
    }
    const ModelObject* model_object = m_c->selection_info()->model_object();
    int instance_index = m_c->selection_info()->get_active_instance();
    const ModelInstance* model_instance = model_object->instances[instance_index];

    //check the print
    if (!print)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",print invalid\n";
        return;
    }

    for (const PrintObject* object : print->objects())
    {
        if (object->model_object()->id() == model_object->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintObject, id is" << model_object->id().id;
            print_object = object;
            break;
        }
    }

    //check the pring object
    if (!print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintObject\n";
        return;
    }

    //find the print instance
    for (const PrintInstance &instance : print_object->instances())
    {
        if (instance.model_instance->id() == model_instance->id())
        {
            BOOST_LOG_TRIVIAL(trace) << __FUNCTION__ << ",found a PrintInstance, id is" << model_instance->id().id;
            m_print_instance = instance;
            break;
        }
    }

    //check the pring object
    if (!m_print_instance.print_object)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",can not find a PrintInstance\n";
        return;
    }

    const PrintObjectConfig& config = m_print_instance.print_object->config();
    m_angle_threshold_deg = config.support_angle;
    m_is_tree_support = config.enable_support.value && is_tree(config.support_type.value);
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",get support_angle "<< m_angle_threshold_deg<<", is_tree "<<m_is_tree_support;

    return;
}

void GLGizmoFdmSupports::invalid_support_volumes(bool invalid_step)
{
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_valid = false;

    if ((invalid_step) && (m_edit_state == state_generating) && m_print_instance.print_object)
    {
        Print *print = m_print_instance.print_object->print();
        if (print) {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "cancel the print";
            print->cancel();
        }
    }
    m_edit_state = state_idle;
    lck.unlock();

    return;
}

bool GLGizmoFdmSupports::need_regenerate_support_volumes()
{
    if (!m_support_volume)
        return true;

    const ModelObject* mo = m_c->selection_info()->model_object();

    if (m_object_id != m_print_instance.print_object->id().id)
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",object_id changed from " << m_object_id << " to " << m_print_instance.print_object->id().id << ", need to regenerate";
        return true;
    }

    int volume_id = -1;
    for (const ModelVolume* mv : mo->volumes) {
        if (! mv->is_model_part())
            continue;

        ++volume_id;
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",volume_id "<<volume_id<<", record_timestamp "<< m_volume_timestamps[volume_id]
                <<", current_timestamp "<<mv->supported_facets.timestamp();
        if (m_volume_timestamps[volume_id] != mv->supported_facets.timestamp())
        {
            return true;
        }
    }

    return false;
}

void GLGizmoFdmSupports::update_support_volumes()
{
    //PrintInstance m_print_instance = get_current_print_instance();

    if ((!m_print_instance.print_object))
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",invalid param, m_volume_ready="<< m_volume_ready;
        return;
    }

    if (m_volume_valid || !need_regenerate_support_volumes())
    {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no need to regenerate support volume, return directly";

        std::unique_lock<std::mutex> lck(m_mutex);
        m_volume_ready = true;
        m_volume_valid = true;
        m_edit_state = state_ready;
        lck.unlock();
        return;
    }
    //generate_support_preview in async mode
    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = false;
    //destroy previous support volume
    if (m_support_volume)
    {
        delete m_support_volume;
        m_support_volume = NULL;
    }
    lck.unlock();

    if (m_thread.joinable()) {
        //join the thread in ui thread
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "try to join thread for 100 ms";
        auto ret = m_thread.try_join_for(boost::chrono::milliseconds(100));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << "join thread returns "<<ret;
    }
    m_cancel = false;
    m_thread = create_thread([this]{this->run_thread();});
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",created thread to generate support volumes";
    return;
}

void GLGizmoFdmSupports::run_thread()
{
    try {
        Print *print = m_print_instance.print_object->print();

        print->restart();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before generate_support_preview";
        m_print_instance.print_object->generate_support_preview();
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",after generate_support_preview";

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        std::unique_lock<std::mutex> lck(m_mutex);
        m_support_volume = new GLVolume(0.5f, 0.5f, 0.5f, 0.5f);
        //m_support_volume->is_support_part = true;
        m_support_volume->force_native_color = true;
        m_support_volume->set_render_color();
        lck.unlock();

        auto record_timestamp = [this]()
        {
            const ModelObject* mo = m_c->selection_info()->model_object();

            int volume_id = -1;
            for (const ModelVolume* mv : mo->volumes) {
                if (!mv->is_model_part())
                    continue;

                ++volume_id;
                m_volume_timestamps[volume_id] = mv->supported_facets.timestamp();
            }
        };

        if (m_cancel)
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", cancelled";
            goto _finished;
        }

        if (!m_print_instance.print_object->support_layers().size())
        {
            BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",no support layer found, update status to 100%\n";
            print->set_status(100, L("Support generated"));
            goto _finished;
        }
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3N3 };
        for (const SupportLayer *support_layer : m_print_instance.print_object->support_layers())
        {
            for (const ExtrusionEntity *extrusion_entity : support_layer->support_fills.entities)
            {
                _3DScene::extrusionentity_to_verts(extrusion_entity, float(support_layer->print_z), m_print_instance.shift, init_data);
            }
        }
        m_support_volume->model.init_from(std::move(init_data));
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished extrusionentity_to_verts, update status to 100%";
        print->set_status(100, L("Support generated"));
        
        record_timestamp();
    }
    catch (...) {
        BOOST_LOG_TRIVIAL(error) << __FUNCTION__ << ",exception catched, mostly cancelling from gui!";
        //wxTheApp->OnUnhandledException();
    }

_finished:
    std::unique_lock<std::mutex> lck(m_mutex);
    if (m_edit_state == state_generating)
        m_edit_state = state_ready;

    lck.unlock();
    m_parent.set_as_dirty();
    m_parent.post_event(SimpleEvent(wxEVT_PAINT));
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", finished all";
    return;
}

// [ORCAPORT:PF-10-auto] Mesh-driven automatic painting. Runs synchronously on the UI thread: the
// object's overhang regions are measured (SupportAutoPaint) and scored against the registry's
// rules, then the winning support type is painted on each region. Already painted facets and
// blockers are excluded before clustering, so existing painting is never overwritten. Unclassified
// regions are left unpainted and fall back to the object's own support style.
void GLGizmoFdmSupports::run_auto_paint()
{
    if (!m_auto_paint_types_initialized) {
        for (const Slic3r::OrcaExt::SupportPaintType &t : Slic3r::OrcaExt::support_paint_types())
            if (!t.rules.empty() && t.auto_enabled_by_default)
                m_auto_paint_types.push_back(t.state);
        m_auto_paint_types_initialized = true;
    }

    const ModelObject *mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;

    const int inst_idx = m_c->selection_info()->get_active_instance();
    if (inst_idx < 0 || size_t(inst_idx) >= mo->instances.size())
        return;
    const Transform3d trafo = mo->instances[size_t(inst_idx)]->get_matrix();

    // The selectors mirror the object's model-part volumes; keep them in sync if the object changed.
    size_t model_parts = 0;
    for (const ModelVolume *mv : mo->volumes)
        if (mv->is_model_part())
            ++model_parts;
    if (m_triangle_selectors.size() != model_parts)
        update_from_model_object(false);
    if (m_triangle_selectors.size() != model_parts)
        return;

    std::vector<std::vector<uint8_t>> painted;
    painted.reserve(m_triangle_selectors.size());
    for (const std::unique_ptr<TriangleSelectorGUI> &sel : m_triangle_selectors) {
        const TriangleSelectorPatch *tsp = dynamic_cast<const TriangleSelectorPatch *>(sel.get());
        painted.push_back(tsp != nullptr ? tsp->painted_facet_mask() : std::vector<uint8_t>{});
    }

    Slic3r::OrcaExt::SupportAutoPaintParams params;
    params.overhang_angle_deg = m_highlight_by_angle_threshold_deg;
    params.overhangs_only     = m_paint_on_overhangs_only;
    params.enabled_types      = m_auto_paint_types;
    params.min_region_area_mm2 = m_auto_paint_min_area;
    {
        // With "supports on build plate only", a normal/grid column that would rest on the model is
        // dropped; classify such regions as tree instead.
        const DynamicPrintConfig &obj_cfg = mo->config.get();
        const DynamicPrintConfig &glb_cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        params.build_plate_only = obj_cfg.option("support_on_build_plate_only")
                                      ? obj_cfg.opt_bool("support_on_build_plate_only")
                                      : glb_cfg.opt_bool("support_on_build_plate_only");
    }

    const std::vector<Slic3r::OrcaExt::SupportAutoPaintHit> hits =
        Slic3r::OrcaExt::classify_support_paint(*mo, trafo, painted, params);
    if (hits.empty()) {
        BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ", no overhang region matched a support type";
        return;
    }

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Automatic support painting", UndoRedo::SnapshotType::GizmoAction);

    bool painted_any = false;
    for (const Slic3r::OrcaExt::SupportAutoPaintHit &hit : hits) {
        if (hit.volume_index >= m_triangle_selectors.size())
            continue;
        TriangleSelectorPatch *sel = dynamic_cast<TriangleSelectorPatch *>(m_triangle_selectors[hit.volume_index].get());
        if (sel == nullptr)
            continue;
        if (sel->get_facet_state(int(hit.facet_index)) != EnforcerBlockerType::NONE)
            continue;
        sel->set_facet(int(hit.facet_index), hit.state);
        sel->request_update_render_data(true); // true = paint changed, forces the geometry rebuild
        painted_any = true;
    }

    if (painted_any) {
        update_model_object();
        m_parent.set_as_dirty();
    }
}

// [ORCAPORT:PF-10-paint] Compact source/destination remap for painted support types, mirroring the
// colour-painting gizmo's "Remap filaments" but on the support states (registry types + blocker).
void GLGizmoFdmSupports::render_support_remap_ui(float scale)
{
    struct Entry
    {
        EnforcerBlockerType state;
        wxString            label;
        ColorRGBA           color;
    };
    std::vector<Entry> entries;
    for (const Slic3r::OrcaExt::SupportPaintType &t : Slic3r::OrcaExt::support_paint_types())
        entries.push_back({t.state, localized_support_label(t),
                           ColorRGBA(t.color[0], t.color[1], t.color[2], t.color[3])});
    entries.push_back({EnforcerBlockerType::BLOCKER, _L("Blocker"), TriangleSelectorGUI::blockers_color});

    std::vector<std::string> labels;
    labels.reserve(entries.size());
    for (const Entry &e : entries)
        labels.emplace_back(e.label.ToUTF8().data());

    auto clamp_idx = [&entries](int &idx) {
        if (idx < 0 || idx >= int(entries.size()))
            idx = 0;
    };
    clamp_idx(m_remap_from_idx);
    clamp_idx(m_remap_to_idx);

    auto swatch = [&](const ColorRGBA &c, const char *id) {
        ImGui::PushID(id);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding, 3.f * scale);
        ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize, 0.f);
        ImGui::ColorButton("##s", ImGuiWrapper::to_ImVec4(c),
                           ImGuiColorEditFlags_NoTooltip | ImGuiColorEditFlags_NoDragDrop | ImGuiColorEditFlags_NoAlpha,
                           ImVec2(14.f * scale, 14.f * scale));
        ImGui::PopStyleVar(2);
        ImGui::PopID();
    };

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("remap_from"));
    ImGui::SameLine();
    swatch(entries[m_remap_from_idx].color, "from");
    ImGui::SameLine();
    m_imgui->combo(std::string("##remap_from"), labels, m_remap_from_idx);

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("remap_to"));
    ImGui::SameLine();
    swatch(entries[m_remap_to_idx].color, "to");
    ImGui::SameLine();
    m_imgui->combo(std::string("##remap_to"), labels, m_remap_to_idx);

    ImGui::Dummy(ImVec2(0.0f, 2.f * scale));
    if (m_imgui->button(m_desc.at("remap")))
        remap_support_types();
}

void GLGizmoFdmSupports::remap_support_types()
{
    std::vector<EnforcerBlockerType> states;
    for (const Slic3r::OrcaExt::SupportPaintType &t : Slic3r::OrcaExt::support_paint_types())
        states.push_back(t.state);
    states.push_back(EnforcerBlockerType::BLOCKER);

    if (m_remap_from_idx < 0 || m_remap_from_idx >= int(states.size()) ||
        m_remap_to_idx < 0 || m_remap_to_idx >= int(states.size()))
        return;
    const EnforcerBlockerType src = states[m_remap_from_idx];
    const EnforcerBlockerType dst = states[m_remap_to_idx];
    if (src == dst)
        return;

    // Identity map, then the single src -> dst swap; StateMap covers every serialisable state.
    EnforcerBlockerStateMap state_map;
    for (size_t i = 0; i < state_map.size(); ++i)
        state_map[i] = static_cast<EnforcerBlockerType>(i);
    state_map[size_t(src)] = dst;

    ModelObject *mo = m_c->selection_info()->model_object();
    if (mo == nullptr)
        return;

    Plater::TakeSnapshot snapshot(wxGetApp().plater(), "Remap support types", UndoRedo::SnapshotType::GizmoAction);

    bool updated = false;
    int  idx     = -1;
    for (ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        ++idx;
        if (idx >= int(m_triangle_selectors.size()))
            break;
        TriangleSelectorGUI *ts = m_triangle_selectors[idx].get();
        if (ts == nullptr)
            continue;
        ts->remap_triangle_state(state_map);
        ts->request_update_render_data(true);
        updated = true;
    }

    if (updated) {
        update_model_object();
        m_parent.set_as_dirty();
    }
}

void GLGizmoFdmSupports::generate_support_volume()
{
    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",before finalize_geometry";

    std::unique_lock<std::mutex> lck(m_mutex);
    m_volume_ready = true;
    m_volume_valid = true;
    m_object_id = m_print_instance.print_object->id().id;
    lck.unlock();

    BOOST_LOG_TRIVIAL(info) << __FUNCTION__ << ",finished finalize_geometry";
}

} // namespace Slic3r::GUI
