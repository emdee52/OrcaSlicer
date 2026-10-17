// [ORCAPORT:PF-2b] Paint-on counterbore bridge gizmo. Adapted from Orca's GLGizmoFuzzySkin
// (same painter base) and preFlight's GLGizmoCounterboreBridge.
#include "GLGizmoCounterboreBridge.hpp"
#include "GizmoStyle.hpp" // [ORCAPORT:PF-2b] fork panel style (docs/handoff.md section 28)

#include "libslic3r/Model.hpp"
#include "libslic3r/Print.hpp"
// [ORCAPORT:RF-1] hole detection + co-axial sleeve for the strengthen mode.
#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include <algorithm>
#include <cstdlib>
#include <cstring>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
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
    m_shortcut = Shortcut::GizmoCounterboreBridge; // [ORCAPORT:PF-2b] registered in Shortcuts.cpp

    m_desc["mode"]           = _L("Bridge type");
    m_desc["smart"]          = _L("Smart (stepped)");
    m_desc["partial"]        = _L("Partial");
    m_desc["clipping_of_view"] = _L("Section view");
    m_desc["reset_direction"]  = _L("Reset direction");
    m_desc["cursor_size"]      = _L("Brush size");
    m_desc["erase_all"]        = _L("Erase all");
    // [ORCAPORT:RF-1] strengthen-hole mode.
    m_desc["feature"]          = _L("Feature");
    m_desc["feat_bridge"]      = _L("Bridging");
    m_desc["feat_strengthen"]  = _L("Strengthen hole");
    m_desc["strength"]         = _L("Wall thickness");
    m_desc["extra_loops"]      = _L("Extra loops");
    m_desc["reinforce_all"]    = _L("Strengthen all vertical holes");
    m_desc["clear_holes"]      = _L("Clear strengthening");
    return true;
}

std::string GLGizmoCounterboreBridge::on_get_name() const
{
    return _u8L("Hole walls"); // [ORCAPORT:UI-1] was "Vertical holes" - the tool also bridges counterbores
}

void GLGizmoCounterboreBridge::on_shutdown()
{
    m_parent.use_slope(false);
    m_parent.toggle_model_objects_visibility(true);
    on_unregister_raycasters_for_picking();
}

void GLGizmoCounterboreBridge::render_painter_gizmo()
{
    // [ORCAPORT:RF-1] GLCanvas3D skips the regular volume pass while a painter gizmo is open, so
    // this pass owns the object's visibility: it must also draw while strengthening, only the
    // brush cursor is bridge-specific.
    const Selection &selection = m_parent.get_selection();

    glsafe(::glEnable(GL_BLEND));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_triangles(selection);
    m_c->object_clipper()->render_cut();
    m_c->instances_hider()->render_cut();
    if (!m_strengthen)
        render_cursor();

    glsafe(::glDisable(GL_BLEND));
}

// [ORCAPORT:PF-2b] The widest caption this panel puts in the label column, measured on the strings
// actually in use so the column stays exact in every language.
static float cb_label_w(const std::map<std::string, wxString> &desc)
{
    float w = 0.f;
    for (const char *key : {"feature", "mode", "cursor_size", "clipping_of_view", "strength", "extra_loops"})
        w = std::max(w, ImGui::CalcTextSize(desc.at(key).utf8_str().data()).x);
    return w + ImGui::GetStyle().ItemSpacing.x * 0.5f;
}

void GLGizmoCounterboreBridge::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = m_c->selection_info()->model_object();
    if (!mo)
        return;

    // [ORCAPORT:RF-1] Keep the detected holes in sync with the model while strengthening.
    if (m_strengthen && m_dirty)
        detect();

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
#if BBS_TOOLBAR_ON_TOP
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif

    // [ORCAPORT:PF-2b] Fork panel style (docs/handoff.md section 28): the shared palette, one fixed
    // narrow width with wrapped hints, and the shared widget layer.
    ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f), ImGuiCond_Always);
    // El orden importa: push_toolbar_style empuja un WindowBg OPACO (COL_WINDOW_BG(_DARK) tiene
    // alfa 1.0) y el ultimo push es el que gana en el Begin. Va PRIMERO, y gizmo_push_window_style()
    // despues, o el panel pierde la transparencia de PanelBg que es parte del estilo: los paneles que
    // nunca empujaron el toolbar style (Align & Stack, Support Zones) si la tenian.
    ImGuiWrapper::push_toolbar_style(scale);
    gizmo_push_window_style();
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    gizmo_push_panel_style();

    // The Feature selector belongs to both modes, so it is drawn once, outside the bodies.
    render_feature_selector();
    ImGui::Separator();
    if (m_strengthen)
        render_strengthen_body();
    else
        render_bridge_body();

    // The way out of the tool, right-aligned like every other primary action in the fork's panels.
    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (m_imgui->button(_L("Done")))
        m_parent.reset_all_gizmos();

    gizmo_pop_panel_style();
    GizmoImguiEnd();
    gizmo_pop_window_style();
    ImGuiWrapper::pop_toolbar_style();
}

// [ORCAPORT:PF-2b] Feature: Bridging | Strengthen hole, as a segmented choice in the label column, so
// the current mode is readable without reading the controls below it.
void GLGizmoCounterboreBridge::render_feature_selector()
{
    const std::string bridge  = m_desc.at("feat_bridge").utf8_str().data();
    const std::string streng  = m_desc.at("feat_strengthen").utf8_str().data();
    const char       *labels[] = {bridge.c_str(), streng.c_str()};
    const int         sel = gizmo_row_segment("##feature", m_desc.at("feature").utf8_str().data(), labels, 2,
                                              m_strengthen ? 1 : 0, cb_label_w(m_desc));
    if (sel >= 0)
        set_strengthen_mode(sel == 1);
}

// [ORCAPORT:PF-2b] Bridging: choose how the painted counterbore regions are bridged, then paint them.
void GLGizmoCounterboreBridge::render_bridge_body()
{
    ModelObject *mo      = model_object();
    const float  label_w = cb_label_w(m_desc);

    // Bridge type: smart stepped vs partial. The filled button carries the choice, so no trailing
    // "(smart: stepped ramp)" text is needed - that row was the widest item in the panel once.
    {
        const std::string smart   = m_desc.at("smart").utf8_str().data();
        const std::string partial = m_desc.at("partial").utf8_str().data();
        const char       *labels[] = {smart.c_str(), partial.c_str()};
        const int         sel = gizmo_row_segment("##mode", m_desc.at("mode").utf8_str().data(), labels, 2,
                                                  m_partial ? 1 : 0, label_w,
                                                  _u8L("Smart steps the bridge up over several layers; Partial bridges with a single layer, which suits small horizontal holes.").c_str());
        if (sel >= 0)
            m_partial = (sel == 1);
    }

    ImGui::Separator();

    // Brush size: a slider with a typed field beside it, and no step buttons (section 28).
    float radius = m_cursor_radius;
    if (gizmo_row_slider("##cursor_radius", m_desc.at("cursor_size").utf8_str().data(), &radius,
                         CursorRadiusMin, CursorRadiusMax, "%.2f", label_w, nullptr))
        m_cursor_radius = radius;

    ImGui::Separator();

    // Section view: the same row, plus Reset direction once the cut has been moved off the face.
    float clp_dist = float(m_c->object_clipper()->get_position());
    if (gizmo_row_slider("##clp_dist", m_desc.at("clipping_of_view").utf8_str().data(), &clp_dist,
                         0.f, 1.f, "%.2f", label_w, nullptr))
        m_parent.set_section_view_ratio(clp_dist);
    if (m_c->object_clipper()->get_position() > 0.f && m_imgui->button(m_desc.at("reset_direction")))
        wxGetApp().CallAfter([this]() { m_parent.align_section_view_to_camera(); });

    ImGui::Separator();

    m_imgui->disabled_begin(mo == nullptr || ! mo->is_counterbore_bridge_painted());
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
}

// [ORCAPORT:RF-1] Strengthen hole: a sleeve per detected hole, as a per-hole parameter modifier.
void GLGizmoCounterboreBridge::render_strengthen_body()
{
    const float label_w = cb_label_w(m_desc);

    // Wall thickness of the strengthening sleeve (mm), and the extra wall loops inside its
    // footprint. Both are typed fields: there is no range the geometry refuses, so a slider would
    // only invent one (section 28 keeps the step buttons off, which is what these rows are for).
    float thickness = float(m_reinforce_thickness);
    if (gizmo_row_input("##strength", m_desc.at("strength").utf8_str().data(), &thickness, "%.2f", label_w))
        set_reinforce_thickness(thickness);

    int loops = m_reinforce_loops;
    if (gizmo_row_input_int("##extra_loops", m_desc.at("extra_loops").utf8_str().data(), &loops, label_w))
        set_reinforce_loops(loops);

    ImGui::Separator();
    const int applied = int(std::count(m_reinforce.begin(), m_reinforce.end(), char(1)));
    m_imgui->text(wxString::Format("%s: %d (%d strengthened)", _u8L("Detected holes"), int(m_holes.size()), applied));
    m_imgui->disabled_begin(m_holes.empty());
    if (m_imgui->button(m_desc.at("reinforce_all")))
        gizmo_reinforce_all();
    m_imgui->disabled_end();
    // One button per line: the two labels together are wider than a standard panel.
    m_imgui->disabled_begin(applied == 0);
    if (m_imgui->button(m_desc.at("clear_holes")))
        gizmo_clear_reinforce();
    m_imgui->disabled_end();
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

// ---------------------------------------------------------------------------------------------
// [ORCAPORT:RF-1] Strengthen-hole mode.
// ---------------------------------------------------------------------------------------------
namespace {

constexpr const char *REINFORCE_NAME = "HoleReinforce";
// A hole whose axis is within 60 deg of the build direction can be strengthened.
constexpr double HORIZONTAL_COS = 0.5;

// Hole highlight colours, mirroring the holes gizmo.
// [ORCAPORT:PP-2] The 3D overlays use the shared scheme (GizmoStyle.hpp). The AVAL colour is the set of
// counterbores you have neither hovered nor picked, so it stays blue; the hovered one is the counterbore
// you would act on (yellow), and the "applied" overlay here holds the STRENGTHEN rings - a
// PARAMETER_MODIFIER, i.e. material ADDED, not removed - so it takes the scheme's green, not its red.
const ColorRGBA AVAIL_COLOR = ColorRGBA(0.25f, 0.70f, 1.00f, 0.40f);
const ColorRGBA APPLIED_COLOR = gizmo_added_colour();
const ColorRGBA HOVER_COLOR   = gizmo_proposed_colour();

void merge_into(indexed_triangle_set &dst, indexed_triangle_set &src)
{
    if (src.indices.empty())
        return;
    if (dst.indices.empty())
        dst = std::move(src);
    else
        its_merge(dst, src);
}

std::string feature_name(const char *prefix, int idx)
{
    return std::string(prefix) + "#" + std::to_string(idx);
}

int parsed_hole_index(const std::string &name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (name.compare(0, n, prefix) != 0 || name.size() <= n || name[n] != '#')
        return -1;
    try {
        return std::stoi(name.substr(n + 1));
    } catch (...) {
        return -1;
    }
}

} // namespace

ModelObject *GLGizmoCounterboreBridge::model_object() const
{
    if (m_c == nullptr || m_c->selection_info() == nullptr)
        return nullptr;
    return m_c->selection_info()->model_object();
}

int GLGizmoCounterboreBridge::object_idx() const
{
    const int sel = m_parent.get_selection().get_object_idx();
    if (sel >= 0)
        return sel;
    const ModelObject *mo     = model_object();
    Plater            *plater = wxGetApp().plater();
    if (mo == nullptr || plater == nullptr)
        return -1;
    const Model &model = plater->model();
    for (size_t i = 0; i < model.objects.size(); ++i)
        if (model.objects[i] == mo)
            return int(i);
    return -1;
}

Transform3d GLGizmoCounterboreBridge::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

Vec3d GLGizmoCounterboreBridge::object_up() const
{
    Vec3d up = instance_matrix().linear().inverse() * Vec3d::UnitZ();
    if (!up.allFinite() || up.norm() < 1e-9)
        return Vec3d::UnitZ();
    return up.normalized();
}

double GLGizmoCounterboreBridge::object_scale() const
{
    const Matrix3d linear = instance_matrix().linear();
    const double   s      = (linear.col(0).norm() + linear.col(1).norm() + linear.col(2).norm()) / 3.0;
    return (std::isfinite(s) && s > 1e-9) ? s : 1.0;
}

int GLGizmoCounterboreBridge::effective_wall_loops() const
{
    if (const ModelObject *mo = model_object(); mo != nullptr && mo->config.has("wall_loops"))
        return mo->config.opt_int("wall_loops");
    if (wxGetApp().preset_bundle != nullptr) {
        const DynamicPrintConfig &cfg = wxGetApp().preset_bundle->prints.get_edited_preset().config;
        if (cfg.has("wall_loops"))
            return cfg.opt_int("wall_loops");
    }
    return 2;
}

indexed_triangle_set GLGizmoCounterboreBridge::reinforce_mesh(const DetectedHole &h) const
{
    // A positive, co-axial ring overlapping the hole wall; as a PARAMETER_MODIFIER it raises the
    // local wall count so a screw bites into solid plastic instead of splitting the part.
    const double thickness = std::max(0.05, m_reinforce_thickness / object_scale());
    const double inner_d   = 2.0 * h.radius;
    const double outer_d   = inner_d + 2.0 * thickness;
    const double margin    = h.through ? std::max(0.5, 0.25 * h.radius) : 0.0;
    const double depth     = h.depth + 2.0 * margin;
    const Vec3d  axis      = h.axis.normalized();
    const Vec3d  entry     = h.center - axis * (0.5 * h.depth + margin);
    return its_make_tube(outer_d, inner_d, depth, axis, entry);
}

indexed_triangle_set GLGizmoCounterboreBridge::make_pick_cylinder(const DetectedHole &h, const Vec3d &up) const
{
    const double         len = std::max(h.depth, 1.0);
    indexed_triangle_set its = its_make_cylinder(h.radius * 1.25, len, PI / 12.);
    const Vec3d          a   = h.axis.normalized();
    Vec3d                u   = up - a * a.dot(up);
    u = (u.norm() < 1e-9) ? Vec3d::UnitY() : u.normalized();
    const Vec3d     r = u.cross(a).normalized();
    Eigen::Matrix3d R;
    R.col(0) = r;
    R.col(1) = u;
    R.col(2) = a;
    Transform3d tr   = Transform3d::Identity();
    tr.linear()      = R;
    tr.translation() = h.center - R * Eigen::Vector3d(0., 0., 0.5 * len);
    for (stl_vertex &v : its.vertices)
        v = (tr * Eigen::Vector3d(v(0), v(1), v(2))).cast<float>();
    return its;
}

void GLGizmoCounterboreBridge::detect()
{
    m_dirty = false;
    m_holes.clear();
    m_reinforce.clear();
    m_pick_its.clear();

    ModelObject *mo = model_object();
    if (mo == nullptr) {
        register_pickers();
        m_preview_dirty = true;
        return;
    }

    wxBusyCursor wait;
    const Vec3d  up = object_up();

    TriangleMesh mesh;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || !v->is_model_part())
            continue;
        TriangleMesh part = v->mesh();
        part.transform(v->get_matrix());
        mesh.merge(part);
    }

    std::vector<DetectedHole> holes = detect_holes(mesh.its);
    m_holes.reserve(holes.size());
    for (DetectedHole &h : holes) {
        HoleView v;
        v.vertical = std::abs(h.axis.dot(up)) >= HORIZONTAL_COS;
        v.hole     = std::move(h);
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

void GLGizmoCounterboreBridge::refresh_applied()
{
    m_reinforce.assign(m_holes.size(), 0);
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || !v->is_modifier())
            continue;
        const int i = parsed_hole_index(v->name, REINFORCE_NAME);
        if (i >= 0 && i < int(m_holes.size()))
            m_reinforce[i] = 1;
    }
    m_preview_dirty = true;
}

void GLGizmoCounterboreBridge::register_pickers()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_pick_items.clear();
    m_pick_raycasters.clear();

    if (get_state() != On || !m_strengthen || m_pick_its.empty())
        return;

    m_parent.set_raycaster_gizmos_on_top(true);
    const Transform3d trafo = instance_matrix();
    for (size_t i = 0; i < m_pick_its.size(); ++i) {
        auto raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(m_pick_its[i]));
        m_pick_items.emplace_back(
            m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, int(i), *raycaster, trafo));
        m_pick_raycasters.emplace_back(std::move(raycaster));
    }
}

void GLGizmoCounterboreBridge::on_register_raycasters_for_picking() { register_pickers(); }

void GLGizmoCounterboreBridge::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
    m_pick_items.clear();
    m_pick_raycasters.clear();
}

void GLGizmoCounterboreBridge::on_set_hover_id()
{
    if (m_hover_id < -1)
        m_hover_id = -1;
    if (m_hover_id >= int(m_holes.size()))
        m_hover_id = -1;
    m_preview_dirty = true;
}

void GLGizmoCounterboreBridge::on_set_state()
{
    GLGizmoPainterBase::on_set_state();
    if (get_state() != On) {
        m_holes.clear();
        m_reinforce.clear();
        m_pick_its.clear();
        m_preview_dirty = true;
        m_dirty = true;
    } else if (m_strengthen) {
        // [ORCAPORT:RF-1] the base turned picking off; strengthen mode needs the hover raycasters.
        m_parent.enable_picking(true);
        register_pickers();
        m_preview_dirty = true;
    }
}

ModelVolume *GLGizmoCounterboreBridge::add_named_volume(const indexed_triangle_set &its, const std::string &name,
                                                        bool snapshot)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return nullptr;

    Plater *plater = wxGetApp().plater();
    if (snapshot)
        plater->take_snapshot(_u8L("Strengthen hole"));

    indexed_triangle_set copy = its;
    // false = keep the object-space coordinates the ring was built in.
    ModelVolume *v = mo->add_volume(TriangleMesh(std::move(copy)), ModelVolumeType::PARAMETER_MODIFIER, false);
    v->name        = name;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
    return v;
}

void GLGizmoCounterboreBridge::remove_named_volumes(const std::string &prefix, const std::string &snapshot_name)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi)
        if (mo->volumes[vi] != nullptr && mo->volumes[vi]->name.rfind(prefix, 0) == 0)
            items.push_back({ItemType::itVolume, oi, int(vi)});
    if (items.empty())
        return;

    Plater                  *plater = wxGetApp().plater();
    Plater::TakeSnapshot     snapshot(plater, snapshot_name, UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
}

void GLGizmoCounterboreBridge::toggle_reinforce(int idx)
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return;

    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    if (m_reinforce[idx] != 0) {
        std::vector<ItemForDelete> items;
        for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
            const ModelVolume *v = mo->volumes[vi];
            if (v != nullptr && parsed_hole_index(v->name, REINFORCE_NAME) == idx)
                items.push_back({ItemType::itVolume, oi, int(vi)});
        }
        if (!items.empty()) {
            Plater              *plater = wxGetApp().plater();
            Plater::TakeSnapshot snapshot(plater, _u8L("Remove hole strengthening"), UndoRedo::SnapshotType::GizmoAction);
            if (ObjectList *ol = wxGetApp().obj_list())
                ol->delete_from_model_and_list(items);
        }
        refresh_applied();
        m_parent.set_as_dirty();
        return;
    }

    const indexed_triangle_set ring = reinforce_mesh(m_holes[idx].hole);
    if (ring.indices.empty())
        return;
    ModelVolume *v = add_named_volume(ring, feature_name(REINFORCE_NAME, idx), true);
    if (v != nullptr)
        v->config.set_key_value("wall_loops", new ConfigOptionInt(effective_wall_loops() + m_reinforce_loops));
    refresh_applied();
    m_parent.set_as_dirty();
}

void GLGizmoCounterboreBridge::clear_all_reinforce()
{
    remove_named_volumes(REINFORCE_NAME, _u8L("Clear hole strengthening"));
    refresh_applied();
    m_parent.set_as_dirty();
}

void GLGizmoCounterboreBridge::set_strengthen_mode(bool on)
{
    if (m_strengthen == on)
        return;
    m_strengthen = on;
    if (on) {
        // [ORCAPORT:RF-1] The painter base disables the canvas picking pass on activation, and that
        // pass is what feeds the gizmo hover id. Strengthen mode needs it for the pick raycasters.
        m_parent.enable_picking(true);
        m_dirty = true;
        detect();
    } else {
        m_parent.enable_picking(false);
        m_holes.clear();
        m_reinforce.clear();
        m_pick_its.clear();
        register_pickers();
        m_preview_dirty = true;
    }
    m_parent.set_as_dirty();
}

void GLGizmoCounterboreBridge::set_reinforce_thickness(double mm)
{
    m_reinforce_thickness = std::clamp(mm, 0.1, 20.0);
    m_parent.set_as_dirty();
}

void GLGizmoCounterboreBridge::set_reinforce_loops(int n)
{
    m_reinforce_loops = std::clamp(n, 1, 20);
    m_parent.set_as_dirty();
}

int GLGizmoCounterboreBridge::hole_count()
{
    if (!m_strengthen)
        return 0;
    if (m_dirty)
        detect();
    return int(m_holes.size());
}

DetectedHole GLGizmoCounterboreBridge::hole(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return DetectedHole();
    return m_holes[idx].hole;
}

bool GLGizmoCounterboreBridge::hole_vertical(int idx) const
{
    return idx >= 0 && idx < int(m_holes.size()) && m_holes[idx].vertical;
}

bool GLGizmoCounterboreBridge::hole_has_reinforce(int idx) const
{
    return idx >= 0 && idx < int(m_reinforce.size()) && m_reinforce[idx] != 0;
}

void GLGizmoCounterboreBridge::gizmo_toggle_hole(int idx)
{
    if (!m_strengthen)
        return;
    if (m_dirty)
        detect();
    if (idx < 0 || idx >= int(m_holes.size()))
        return;
    if (!m_holes[idx].vertical)
        return; // only vertical holes are strengthened
    toggle_reinforce(idx);
}

void GLGizmoCounterboreBridge::gizmo_reinforce_all()
{
    if (!m_strengthen)
        return;
    if (m_dirty)
        detect();
    for (size_t i = 0; i < m_holes.size(); ++i)
        if (m_holes[i].vertical && m_reinforce[i] == 0)
            toggle_reinforce(int(i));
    refresh_applied();
    m_parent.set_as_dirty();
}

void GLGizmoCounterboreBridge::gizmo_clear_reinforce() { clear_all_reinforce(); }

void GLGizmoCounterboreBridge::gizmo_refresh()
{
    m_dirty = true;
    if (m_strengthen)
        detect();
    m_parent.set_as_dirty();
}

bool GLGizmoCounterboreBridge::on_mouse(const wxMouseEvent &mouse_event)
{
    if (m_strengthen) {
        const bool on_hole = m_hover_id >= 0 && m_hover_id < int(m_holes.size());
        if (mouse_event.LeftDown() && on_hole) {
            gizmo_toggle_hole(m_hover_id);
            return true;
        }
        if (mouse_event.LeftUp() && on_hole)
            return true;
        return false;
    }
    return GLGizmoPainterBase::on_mouse(mouse_event);
}

// [ORCAPORT:RF-1] Highlight the detected holes so it is obvious which ones can be clicked:
// available (blue), hovered (green) and already strengthened (red).
void GLGizmoCounterboreBridge::rebuild_hole_previews()
{
    m_preview_dirty = false;
    m_preview_avail.reset();
    m_preview_hover.reset();
    m_preview_applied.reset();

    if (!m_strengthen)
        return;

    const Vec3d          up = object_up();
    indexed_triangle_set avail_its, hover_its, applied_its;
    for (size_t i = 0; i < m_holes.size(); ++i) {
        const HoleView &hv = m_holes[i];
        if (hv.hole.radius <= 0.)
            continue;
        if (i < m_reinforce.size() && m_reinforce[i] != 0) {
            indexed_triangle_set ring = reinforce_mesh(hv.hole);
            merge_into(applied_its, ring);
            continue;
        }
        if (!hv.vertical)
            continue;
        indexed_triangle_set ghost = make_pick_cylinder(hv.hole, up);
        merge_into(avail_its, ghost);
        if (int(i) == m_hover_id) {
            indexed_triangle_set hover = make_pick_cylinder(hv.hole, up);
            merge_into(hover_its, hover);
        }
    }

    if (!avail_its.indices.empty()) {
        m_preview_avail.model.init_from(avail_its);
        m_preview_avail.model.set_color(AVAIL_COLOR);
    }
    if (!hover_its.indices.empty()) {
        m_preview_hover.model.init_from(hover_its);
        m_preview_hover.model.set_color(HOVER_COLOR);
    }
    if (!applied_its.indices.empty()) {
        m_preview_applied.model.init_from(applied_its);
        m_preview_applied.model.set_color(APPLIED_COLOR);
    }
}

void GLGizmoCounterboreBridge::on_render()
{
    if (!m_strengthen)
        return;
    if (m_dirty)
        detect();
    if (m_preview_dirty)
        rebuild_hole_previews();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera &camera = wxGetApp().plater()->get_camera();

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * instance_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));

    m_preview_avail.model.render(shader);
    m_preview_hover.model.render(shader);
    m_preview_applied.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

} // namespace Slic3r::GUI
