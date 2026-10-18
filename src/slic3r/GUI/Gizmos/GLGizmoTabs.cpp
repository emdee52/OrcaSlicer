#include "GLGizmoTabs.hpp"

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/Model.hpp"

#include <Eigen/Geometry>

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/format.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/I18N.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include "GizmoStyle.hpp"

#include "glad/gl.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

// [ORCAPORT:TB-1] One tab is up to three volumes with the same id: the plate, its boss, and the screw
// the first two are cut by. Named like the other fork features ("<prefix>#<id>") so the object list
// reads and a later session can find them by prefix.
constexpr const char *TAB_NAME      = "Tab";
constexpr const char *TAB_BOSS_NAME = "TabBoss";
constexpr const char *TAB_HOLE_NAME = "TabHole";

// Preview overlays, in the PP-2 scheme: the surface under the cursor, what the current values would
// add or cut, and what has already been added / removed. A tab has no separate "picked face" colour -
// the anchor is a point, not a face, and what it picks is the tab the orange mesh already shows.
const ColorRGBA FACE_COLOR     = gizmo_surface_colour();
const ColorRGBA PROPOSED_COLOR = gizmo_proposed_colour();
const ColorRGBA ADDED_COLOR    = gizmo_added_colour();
const ColorRGBA CUT_COLOR      = gizmo_cut_colour();

// The panel's ranges, in world mm (degrees for the two angles).
constexpr double ROOT_W_MIN = 3., ROOT_W_MAX = 80.;
constexpr double LENGTH_MIN = 3., LENGTH_MAX = 80.;
constexpr double TAPER_MIN = 0., TAPER_MAX = 60.;
constexpr double HEAD_FIT_MIN = -0.4, HEAD_FIT_MAX = 0.;
constexpr double BOSS_D_MIN = 2., BOSS_D_MAX = 60.;
constexpr double BOSS_H_MIN = 0., BOSS_H_MAX = 40.;
constexpr double THICK_MIN = 0.4, THICK_MAX = 12.;
constexpr double ANGLE_MIN = 0., ANGLE_MAX = 360.;

// Material kept around the passage, and the wall a boss keeps around the seat. World mm.
constexpr double MIN_WALL = 2.0;
constexpr double BOSS_WALL = 2.0;
// A tapped hole wants this much material to bite into; the boss makes up the difference.
constexpr double TAP_ENGAGEMENT = 6.0;
// How far the bore's floor lands past the tab's far face, and how far the cutter's entrance cap
// starts outside its top: both a hair, because a face exactly flush with either is the coplanar pair
// ME-3 documents (a zero-thickness skin over the recess, or a cutter that leaves a sliver).
constexpr double BORE_OVERSHOOT = 0.2;
// The weld: how far the root ALWAYS enters the material. Not a field - the slicer unions the tab with
// the part per layer, so a root that only touches the face is a pinch vertex there and the tab's
// walls meet the part's at a point per layer, which snaps off by hand. A millimetre of material is
// what makes the junction real. The surface's own fall-away raises it on a curved pick (and past
// CURVE_DROP_MAX the tab is refused, because a flat plate cannot sit on the face at all).
constexpr double WELD_MM = 1.0;
constexpr double CURVE_DROP_MAX = 3.0;
// Share of the ROOT BAND that must have material under it before a tab can be added, and the share
// below which the panel warns. The plate itself is meant to reach past the material.
constexpr double COVERAGE_REFUSE = 0.3;
constexpr double COVERAGE_WARN = 0.6;
// How little the cursor has to move for the tab under it to be worth rebuilding, and the shortest
// interval between two surface probes while a slider is dragged.
constexpr double PROBE_MIN_INTERVAL = 0.1;
// How far ahead of the root edge the probe measures: the strip the tab is welded by, in world mm.
constexpr double ROOT_BAND = 1.5;

double deg2rad(double d) { return d * PI / 180.; }

std::string feature_name(const char *prefix, int idx)
{
    return std::string(prefix) + "#" + std::to_string(idx);
}

int parsed_feature_index(const std::string &name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.compare(0, n, prefix) != 0 || name[n] != '#')
        return -1;
    try {
        return std::stoi(name.substr(n + 1));
    } catch (...) {
        return -1;
    }
}

bool is_tab_volume(const ModelVolume *v)
{
    if (v == nullptr)
        return false;
    return parsed_feature_index(v->name, TAB_NAME) >= 0 || parsed_feature_index(v->name, TAB_BOSS_NAME) >= 0
           || parsed_feature_index(v->name, TAB_HOLE_NAME) >= 0;
}

int object_idx_of(const Selection &selection, const ModelObject *mo)
{
    const int idx = selection.get_object_idx();
    if (idx >= 0 || mo == nullptr)
        return idx;
    const Model &model = wxGetApp().plater()->model();
    for (size_t i = 0; i < model.objects.size(); ++i)
        if (model.objects[i] == mo)
            return int(i);
    return -1;
}

std::string head_name(BoreHead h)
{
    switch (h) {
    case BoreHead::None: return _u8L("None");
    case BoreHead::SocketHead: return _u8L("Socket");
    case BoreHead::ButtonHead: return _u8L("Button");
    case BoreHead::Countersink: return _u8L("Sink");
    }
    return _u8L("None");
}

// ─────────────────────────────────────────────────────────────────────────────────────────────
// The screw: the Holes tool's vocabulary, resolved for a tab
// ─────────────────────────────────────────────────────────────────────────────────────────────

} // namespace

GLGizmoTabs::GLGizmoTabs(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

ModelObject *GLGizmoTabs::model_object() const
{
    return m_c != nullptr && m_c->selection_info() != nullptr ? m_c->selection_info()->model_object() : nullptr;
}

int GLGizmoTabs::object_idx() const { return object_idx_of(m_parent.get_selection(), model_object()); }

Transform3d GLGizmoTabs::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

double GLGizmoTabs::object_scale() const
{
    const Transform3d im = instance_matrix();
    const double      s  = (im.linear().col(0).norm() + im.linear().col(1).norm() + im.linear().col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

double GLGizmoTabs::volume_scale(const ModelVolume *mv) const
{
    if (mv == nullptr)
        return 1.0;
    const Eigen::Matrix3d L = mv->get_matrix().linear();
    const double          s = (L.col(0).norm() + L.col(1).norm() + L.col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

Vec3d GLGizmoTabs::object_centre() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return Vec3d::Zero();
    BoundingBoxf3 bb;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || !v->is_model_part() || is_tab_volume(v) || v->mesh().its.vertices.empty())
            continue;
        bb.merge(v->mesh().bounding_box().transformed(v->get_matrix()));
    }
    return bb.defined ? bb.center() : Vec3d::Zero();
}

const HoleStandard *GLGizmoTabs::screw_standard() const
{
    return (m_standard >= 0 && m_standard < int(hole_standards().size())) ? &hole_standards()[m_standard] : nullptr;
}

std::string GLGizmoTabs::standard_name(int idx) const
{
    if (idx <= 0 || idx > int(hole_standards().size()))
        return "Custom";
    return hole_standards()[idx - 1].designation;
}

// ---------------------------------------------------------------------------------------------
// The hover, the anchor and the tab's frame
// ---------------------------------------------------------------------------------------------

void GLGizmoTabs::clear_hover()
{
    m_hover         = Hover();
    m_preview_dirty = true;
    m_preview_face.reset();
}

void GLGizmoTabs::update_hover(const Vec2d &screen_pos)
{
    const Hover previous = m_hover;
    m_hover              = Hover();

    const ModelObject *mo = model_object();
    if (mo != nullptr) {
        const ClippingPlane *clipping = m_c != nullptr && m_c->object_clipper() != nullptr
                                            ? m_c->object_clipper()->get_clipping_plane()
                                            : nullptr;
        const GLVolume    *volume = nullptr;
        const ModelVolume *mv     = nullptr;
        size_t             facet  = 0;
        Vec3d              hit_world;
        // The tabs already added are picked THROUGH: they sit on the surface, and a committed tab must
        // not stop the next one from being placed beside it.
        if (raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit_world,
                                [](const ModelVolume *v) { return !is_tab_volume(v); })) {
            if (mv != nullptr && facet < mv->mesh().its.indices.size()) {
                m_hover.valid   = true;
                m_hover.mv      = mv;
                m_hover.facet   = int(facet);
                m_hover.hit_obj = instance_matrix().inverse() * hit_world;
            }
        }
    }

    if (m_hover.valid != previous.valid
        || (m_hover.valid && (m_hover.mv != previous.mv || m_hover.facet != previous.facet
                              || (m_hover.hit_obj - previous.hit_obj).norm() > 1e-6)))
        m_preview_dirty = true;
}

// Where the tab goes. `out_bite` receives the bite the PLATE itself carries: on a face the plate lies
// in that is the surface's own weld, and on a wall it is the minimum, because there the weld is the
// root's sideways overlap with the face - and the frame carries that. A plate sunk `bite` below a wall
// click would put the tab's underside below the height that was clicked, and a foot has to sit on
// something.
TabFrame GLGizmoTabs::frame(double *out_bite) const
{
    if (!m_pick.valid) {
        if (out_bite != nullptr)
            *out_bite = WELD_MM;
        return TabFrame();
    }
    const double k        = 1. / object_scale(); // world mm -> object units
    const double root_obj = std::clamp(m_hole_offset, hole_offset_min(), hole_offset_max()) * k;
    const double weld_obj = std::clamp(m_effective_weld, WELD_MM, CURVE_DROP_MAX) * k;

    const TabFrame probe = tab_frame(m_pick.normal_obj, m_pick.click_obj, object_centre(), root_obj, 0., m_angle);
    // Which side the weld goes in: on a wall the plate enters the face SIDEWAYS (tab_frame's origin
    // carries it, and the plate must not also sink below the click or a foot would sit on nothing); on
    // a face the plate lies in, the plate itself is pushed in along its own normal. Doing both would
    // embed it twice.
    if (out_bite != nullptr)
        *out_bite = probe.wall ? 0.05 : weld_obj;
    return probe.wall ? tab_frame(m_pick.normal_obj, m_pick.click_obj, object_centre(), root_obj, weld_obj, m_angle)
                      : probe;
}

Transform3d GLGizmoTabs::frame_to_volume(const ModelVolume *mv) const
{
    const Transform3d f = frame(nullptr).to_obj;
    return mv == nullptr ? f : mv->get_matrix().inverse() * f;
}

bool GLGizmoTabs::anchor_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    if (!m_hover.valid || m_hover.mv == nullptr || m_hover.facet < 0)
        return false;

    Vec3d n = facet_normal_in_world(m_hover.mv->mesh().its, m_hover.facet, m_hover.mv->get_matrix());
    if (!n.allFinite() || n.norm() < 1e-9)
        return false;
    n.normalize();

    Pick p;
    p.valid     = true;
    p.mv        = m_hover.mv;
    p.facet     = m_hover.facet;
    p.click_obj = m_hover.hit_obj;
    p.normal_obj = n;

    m_pick = p;
    // The angle is measured from the frame's own zero (straight out of a wall, or away from the
    // object's centre on a face the plate lies in), so a fresh anchor starts at zero and the frame
    // decides what that means.
    m_angle         = 0.;
    m_preview_dirty = true;
    probe_surface(true);
    m_parent.set_as_dirty();
    return true;
}

void GLGizmoTabs::clear_anchor()
{
    if (!m_pick.valid)
        return;
    m_pick          = Pick();
    m_probe         = TabSurfaceProbe();
    m_effective_weld = WELD_MM;
    m_why.clear();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

// ---------------------------------------------------------------------------------------------
// The screw
// ---------------------------------------------------------------------------------------------

double GLGizmoTabs::true_diameter() const
{
    return std::max(0.1, m_diameter + m_tolerance);
}

double GLGizmoTabs::head_sink() const { return std::max(0., -m_head_fit); }

double GLGizmoTabs::seat_diameter() const
{
    const HoleStandard *s = screw_standard();
    if (s == nullptr)
        return 0.;
    switch (m_head) {
    case BoreHead::SocketHead: return s->socket_d;
    case BoreHead::ButtonHead: return s->button_d;
    case BoreHead::Countersink: return s->csink_d;
    case BoreHead::None: break;
    }
    return 0.;
}

double GLGizmoTabs::seat_depth() const
{
    const HoleStandard *s = screw_standard();
    if (s == nullptr)
        return 0.;
    const double sink = head_sink();
    switch (m_head) {
    case BoreHead::SocketHead: return s->socket_k + sink;
    case BoreHead::ButtonHead: return s->button_k + sink;
    case BoreHead::Countersink: return 0.; // the cone's own depth follows from its angle
    case BoreHead::None: break;
    }
    return 0.;
}

double GLGizmoTabs::seat_angle() const
{
    const HoleStandard *s = screw_standard();
    return s != nullptr ? s->csink_angle : 90.;
}

void GLGizmoTabs::suggest_boss()
{
    // Sized to the screw whenever the screw changes; the switch and the typed values win afterwards.
    const double head_d = seat_diameter();
    const double d      = true_diameter();
    m_boss_d            = std::clamp((head_d > 0. ? head_d : d) + 2. * BOSS_WALL, BOSS_D_MIN, BOSS_D_MAX);
    if (m_screw_fit == ScrewFit::Tap && m_boss)
        m_boss_h = std::clamp(std::max(BOSS_H_MIN, TAP_ENGAGEMENT - m_thickness), BOSS_H_MIN, BOSS_H_MAX);
    m_preview_dirty = true;
}

void GLGizmoTabs::reseat_defaults()
{
    // M3 socket head, free fit: the screw the owner prints most of.
    for (size_t i = 0; i < hole_standards().size(); ++i) {
        const HoleStandard &s = hole_standards()[i];
        if (s.kind == HoleStandardKind::Screw && s.designation == "M3") {
            m_standard = int(i);
            m_diameter = screw_nominal_diameter(s, false);
            break;
        }
    }
    // Length 16 with the screw 6 in from the root: the default a mounting foot wants.
    m_length        = 16.;
    m_hole_offset   = 6.;
    m_tip           = TabTip::Round;
    m_taper         = 12.;
    // A solo boss's height only means something for a tap; a free screw is held by the host.
    m_boss   = false;
    m_boss_h = std::clamp(std::max(0., TAP_ENGAGEMENT - m_thickness), BOSS_H_MIN, BOSS_H_MAX);
    suggest_boss();
}

// ---------------------------------------------------------------------------------------------
// The spec, the probe and the geometry
// ---------------------------------------------------------------------------------------------

void GLGizmoTabs::probe_surface(bool force)
{
    m_effective_weld = WELD_MM;
    m_probe          = TabSurfaceProbe();
    if (!m_pick.valid)
        return;

    // A wall tab's plate hangs beside the face, so there is no surface under it to measure - and none
    // is needed: tab_frame() puts its root edge `bite` INSIDE the face that was clicked, which is what
    // attaches it, and the raycast proved the face is material. What has to be measured is a plate that
    // lies in the face's own plane, where the root can miss the material (a click near an edge) and
    // where a curved face makes it fall away.
    if (frame(nullptr).wall)
        return;

    // The one O(mesh) piece of work the panel does. While a slider is dragged it is throttled: the
    // measurement only decides the bite (invisible, inside the material) and the coverage warning, so
    // standing still for a few frames costs nothing - and Add tab forces a fresh one.
    const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
    if (!force && m_probe_time > 0. && now - m_probe_time < PROBE_MIN_INTERVAL)
        return;
    m_probe_time = now;

    const double k = 1. / object_scale(); // world mm -> object units
    // The ROOT BAND, not the whole plate: the tab is meant to reach out past the material, so what has
    // to be over material is the strip it is welded by.
    const double   root_obj = std::clamp(m_hole_offset, hole_offset_min(), hole_offset_max()) * k;
    const double   band     = (m_effective_weld + ROOT_BAND) * k;
    const double   half_w   = 0.5 * std::clamp(m_root_w, ROOT_W_MIN, ROOT_W_MAX) * k;
    Polygon        root_poly;
    for (const Point &p : {Point::new_scale(-root_obj, -half_w), Point::new_scale(-root_obj + band, -half_w),
                           Point::new_scale(-root_obj + band, half_w), Point::new_scale(-root_obj, half_w)})
        root_poly.points.push_back(p);
    root_poly.make_counter_clockwise();
    ExPolygons root_band;
    root_band.emplace_back(std::move(root_poly));

    m_probe = tab_probe_surface(m_pick.mv->mesh().its, frame_to_volume(m_pick.mv), root_band,
                                CURVE_DROP_MAX * k * 1.5);

    // The plate owes the surface a weld; where the surface falls away from the tab's plane it owes it
    // that too, or only the middle of the plate would touch.
    m_effective_weld = std::max(WELD_MM, m_probe.fall_away * object_scale() + BORE_OVERSHOOT);
}

TabSpec GLGizmoTabs::build_spec(std::string *why) const
{
    TabSpec s;
    if (why != nullptr)
        why->clear();

    const double k        = 1. / object_scale(); // world mm -> object units
    double       weld_obj = 0.;
    frame(&weld_obj); // the weld the PLATE carries (a wall's is in the frame's origin instead)
    // One Length and one Hole offset: the passage sits `hole_offset` from the root edge and the tip
    // `length - hole_offset` past it.
    const double len       = std::clamp(m_length, LENGTH_MIN, LENGTH_MAX);
    const double off       = std::clamp(m_hole_offset, hole_offset_min(), hole_offset_max());
    s.root_w               = std::clamp(m_root_w, ROOT_W_MIN, ROOT_W_MAX) * k;
    s.root_len             = off * k;
    s.tip_len              = (len - off) * k;
    s.taper_deg            = std::clamp(m_taper, TAPER_MIN, TAPER_MAX);
    s.tip                  = m_tip;
    s.min_wall             = MIN_WALL * k;
    s.passage              = m_passage;
    s.passage_r            = 0.5 * true_diameter() * k;
    s.slot_w               = 0.; // the builder defaults it to the screw's own clearance
    s.thickness            = std::clamp(m_thickness, THICK_MIN, THICK_MAX) * k;
    s.weld                 = weld_obj;
    s.boss_d              = m_boss ? std::clamp(m_boss_d, BOSS_D_MIN, BOSS_D_MAX) * k : 0.;
    s.boss_h              = m_boss ? std::clamp(m_boss_h, BOSS_H_MIN, BOSS_H_MAX) * k : 0.;
    if (tab_outline(s).poly.empty() && why != nullptr)
        *why = _u8L("No room for a tab with this passage and these walls - widen the tab or the passage.");
    return s;
}

TabScrew GLGizmoTabs::build_screw(const TabSpec &spec) const
{
    TabScrew out;
    if (m_passage == TabPassage::Slot) // the slot is part of the outline, so there is nothing to cut
        return out;
    if (!m_pick.valid)
        return out;

    const double k = 1. / object_scale();
    out.d          = true_diameter() * k;
    out.seat_d     = seat_diameter() * k;
    switch (m_head) {
    case BoreHead::SocketHead:
    case BoreHead::ButtonHead:
        out.seat = out.seat_d > out.d ? TabSeat::Counterbore : TabSeat::None;
        break;
    case BoreHead::Countersink:
        out.seat = out.seat_d > out.d ? TabSeat::Countersink : TabSeat::None;
        break;
    case BoreHead::None: break;
    }
    out.seat_depth = seat_depth() * k;
    out.seat_angle = seat_angle();

    // The bore always goes through the TAB and a hair past its far face: a mounting tab's hole is a
    // clearance hole for a screw that bites whatever is below it, and a floor exactly flush with that
    // surface would be the coplanar pair ME-3 documents. `depth` is measured down from the tab's top.
    out.depth = tab_top(spec) + spec.weld + BORE_OVERSHOOT * k;
    return out;
}

// ---------------------------------------------------------------------------------------------
// The preview
// ---------------------------------------------------------------------------------------------

void GLGizmoTabs::rebuild_preview()
{
    m_preview_dirty = false;
    m_preview_face.reset();
    m_preview_tab.reset();
    m_preview_added.reset();
    m_preview_cut.reset();
    m_why.clear();

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    // The face under the cursor, in the fork's teal: the surface the tab would sit on, lifted a hair
    // so it does not z-fight with itself.
    if (m_hover.valid && m_hover.mv != nullptr && m_hover.facet >= 0) {
        const std::vector<int> region = coplanar_region(m_hover.mv, size_t(m_hover.facet), m_face_cache);
        indexed_triangle_set   patch  = build_coplanar_patch(m_hover.mv, region, m_face_cache, 0.02f);
        if (!patch.indices.empty()) {
            const Transform3d m = m_hover.mv->get_matrix();
            if (!m.isApprox(Transform3d::Identity()))
                for (stl_vertex &p : patch.vertices)
                    p = (m * p.cast<double>()).cast<float>();
            m_preview_face.model.init_from(patch);
            m_preview_face.model.set_color(FACE_COLOR);
        }
    }

    // What the current values would add, in the fork's orange: the plate and its boss, and the screw
    // they are cut by (both go in one overlay - the cutter is inside the plate, and one colour is what
    // "this is the geometry the panel would make" means).
    if (m_pick.valid) {
        std::string    why;
        const TabSpec  spec  = build_spec(&why);
        const TabScrew screw = build_screw(spec);
        // A wall tab's plate hangs beside the face and the probe does not measure it (its root is welded
        // by construction), so neither the coverage nor the curl rule can apply there.
        const bool wall = frame(nullptr).wall;
        if (!why.empty())
            m_why = why;
        else if (!wall && m_probe.coverage < COVERAGE_REFUSE)
            m_why = Slic3r::GUI::format(
                _L("Only %d%% of the tab's root has the face under it - move it onto the material."),
                int(m_probe.coverage * 100.));
        else if (!wall && m_effective_weld > CURVE_DROP_MAX)
            m_why = _u8L("The face curves away too fast for a tab this size: shorten it, or pick a flatter one.");

        indexed_triangle_set ghost = make_tab_plate(spec);
        its_merge(ghost, make_tab_boss(spec));
        its_merge(ghost, make_tab_screw(spec, screw));
        if (!ghost.indices.empty()) {
            its_transform(ghost, frame(nullptr).to_obj);
            m_preview_tab.model.init_from(ghost);
            m_preview_tab.model.set_color(PROPOSED_COLOR);
        }
    }

    // Every tab already on the object: the material it added in green, what it cuts in red.
    indexed_triangle_set added, cut;
    for (const ModelVolume *v : mo->volumes) {
        if (!is_tab_volume(v))
            continue;
        indexed_triangle_set part = v->mesh().its;
        const Transform3d    m    = v->get_matrix();
        if (!m.isApprox(Transform3d::Identity()))
            for (stl_vertex &p : part.vertices)
                p = (m * p.cast<double>()).cast<float>();
        indexed_triangle_set &dst = v->type() == ModelVolumeType::NEGATIVE_VOLUME ? cut : added;
        its_merge(dst, part);
    }
    if (!added.indices.empty()) {
        m_preview_added.model.init_from(added);
        m_preview_added.model.set_color(ADDED_COLOR);
    }
    if (!cut.indices.empty()) {
        m_preview_cut.model.init_from(cut);
        m_preview_cut.model.set_color(CUT_COLOR);
    }
}

// ---------------------------------------------------------------------------------------------
// The volumes
// ---------------------------------------------------------------------------------------------

int GLGizmoTabs::next_tab_id() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 1;
    int next = 1;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr)
            continue;
        next = std::max(next, parsed_feature_index(v->name, TAB_NAME) + 1);
        next = std::max(next, parsed_feature_index(v->name, TAB_BOSS_NAME) + 1);
        next = std::max(next, parsed_feature_index(v->name, TAB_HOLE_NAME) + 1);
    }
    return next;
}

int GLGizmoTabs::tab_count() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    std::vector<int> ids;
    for (const ModelVolume *v : mo->volumes) {
        int id = parsed_feature_index(v->name, TAB_NAME);
        if (id < 0)
            id = parsed_feature_index(v->name, TAB_BOSS_NAME);
        if (id < 0)
            continue;
        if (std::find(ids.begin(), ids.end(), id) == ids.end())
            ids.push_back(id);
    }
    return int(ids.size());
}

std::vector<std::array<double, 3>> GLGizmoTabs::list_tabs() const
{
    std::vector<std::array<double, 3>> out;
    const ModelObject                  *mo = model_object();
    if (mo == nullptr)
        return out;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || parsed_feature_index(v->name, TAB_NAME) < 0 || v->mesh().its.vertices.empty())
            continue;
        Vec3f c = Vec3f::Zero();
        for (const stl_vertex &p : v->mesh().its.vertices)
            c += p;
        c /= float(v->mesh().its.vertices.size());
        const Vec3d center = v->get_matrix() * c.cast<double>();
        out.push_back({ center.x(), center.y(), center.z() });
    }
    return out;
}

bool GLGizmoTabs::add_tab_volumes(const TabSpec &spec, const TabScrew &screw, const std::string &action)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return false;

    indexed_triangle_set plate = make_tab_plate(spec);
    if (plate.indices.empty())
        return false;
    indexed_triangle_set boss = make_tab_boss(spec);
    indexed_triangle_set hole = make_tab_screw(spec, screw);

    const Transform3d to_obj = frame(nullptr).to_obj;
    const int         id     = next_tab_id();

    Plater *plater = wxGetApp().plater();
    plater->take_snapshot(action);

    auto add = [&](indexed_triangle_set its, ModelVolumeType type, const std::string &name) {
        if (its.indices.empty())
            return;
        its_transform(its, to_obj);
        ModelVolume *v = mo->add_volume(TriangleMesh(std::move(its)), type, false);
        v->name        = name;
    };
    add(plate, ModelVolumeType::MODEL_PART, feature_name(TAB_NAME, id));
    add(boss, ModelVolumeType::MODEL_PART, feature_name(TAB_BOSS_NAME, id));
    add(hole, ModelVolumeType::NEGATIVE_VOLUME, feature_name(TAB_HOLE_NAME, id));

    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
    return true;
}

bool GLGizmoTabs::commit()
{
    if (!m_pick.valid)
        return false;

    // A fresh measurement decides the bite and whether the tab is attached at all: the commit is the
    // one place that must not act on a throttled reading.
    probe_surface(true);

    std::string    why;
    const TabSpec  spec  = build_spec(&why);
    const TabScrew screw = build_screw(spec);
    if (!why.empty()) {
        m_why = why;
        return false;
    }
    // The two measurements only exist for a plate lying in the face's own plane; a wall tab is welded
    // by its construction (see probe_surface()).
    if (!frame(nullptr).wall && m_probe.coverage < COVERAGE_REFUSE) {
        m_why = Slic3r::GUI::format(
            _L("Only %d%% of the tab's root has the face under it - move it onto the material."),
            int(m_probe.coverage * 100.));
        return false;
    }
    if (!frame(nullptr).wall && m_effective_weld > CURVE_DROP_MAX) {
        m_why = _u8L("The face curves away too fast for a tab this size: shorten it, or pick a flatter one.");
        return false;
    }
    if (!add_tab_volumes(spec, screw, _u8L("Add tab")))
        return false;

    // The tab is placed; the next click places the next one. One commit per placement keeps a second
    // press from stacking an identical tab on the same spot.
    clear_anchor();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
    return true;
}

void GLGizmoTabs::remove_tab(int id)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0 || id < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if (v == nullptr)
            continue;
        if (parsed_feature_index(v->name, TAB_NAME) == id || parsed_feature_index(v->name, TAB_BOSS_NAME) == id
            || parsed_feature_index(v->name, TAB_HOLE_NAME) == id)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Remove tab"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
    m_preview_dirty = true;
}

bool GLGizmoTabs::remove_hovered()
{
    return remove_at(m_parent.get_local_mouse_position());
}

bool GLGizmoTabs::remove_at(const Vec2d &screen_pos)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return false;

    const ClippingPlane *clipping = m_c != nullptr && m_c->object_clipper() != nullptr
                                        ? m_c->object_clipper()->get_clipping_plane()
                                        : nullptr;
    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit;
    // The mirror of update_hover(): here only the tabs are hit, so a right-click can tell "remove the
    // tab under the cursor" from "place another one".
    if (!raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit,
                             [](const ModelVolume *v) { return is_tab_volume(v); }))
        return false;

    int id = parsed_feature_index(mv->name, TAB_NAME);
    if (id < 0)
        id = parsed_feature_index(mv->name, TAB_BOSS_NAME);
    if (id < 0)
        id = parsed_feature_index(mv->name, TAB_HOLE_NAME);
    if (id < 0)
        return false;
    remove_tab(id);
    return true;
}

void GLGizmoTabs::clear_all()
{
    const ModelObject *mo = model_object();
    const int          oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi)
        if (is_tab_volume(mo->volumes[vi]))
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Clear tabs"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
    m_preview_dirty = true;
}

void GLGizmoTabs::refresh()
{
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

// ---------------------------------------------------------------------------------------------
// The parameters
// ---------------------------------------------------------------------------------------------

void GLGizmoTabs::set_tip(TabTip t)
{
    if (m_tip == t)
        return;
    m_tip           = t;
    m_preview_dirty = true;
    probe_surface(true);
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_root_w(double mm) { m_root_w = std::clamp(mm, ROOT_W_MIN, ROOT_W_MAX); probe_surface(true); m_preview_dirty = true; m_parent.set_as_dirty(); }

void GLGizmoTabs::set_length(double mm)
{
    m_length = std::clamp(mm, LENGTH_MIN, LENGTH_MAX);
    // The hole has to keep material on both sides of it, so a shorter tab pulls it in.
    m_hole_offset   = std::clamp(m_hole_offset, hole_offset_min(), hole_offset_max());
    probe_surface(true);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_taper(double deg) { m_taper = std::clamp(deg, TAPER_MIN, TAPER_MAX); probe_surface(true); m_preview_dirty = true; m_parent.set_as_dirty(); }

void GLGizmoTabs::set_passage(TabPassage p)
{
    m_passage       = p;
    m_preview_dirty = true;
    probe_surface(true);
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_hole_offset(double mm)
{
    // The hole slides INSIDE the tab: the outline stays where Length put it, and the bounds come from
    // the tab's own ends.
    m_hole_offset   = std::clamp(mm, hole_offset_min(), hole_offset_max());
    probe_surface(true);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

double GLGizmoTabs::hole_offset_min() const
{
    return std::min(MIN_WALL + 0.5 * true_diameter(), 0.4 * std::clamp(m_length, LENGTH_MIN, LENGTH_MAX));
}

double GLGizmoTabs::hole_offset_max() const
{
    return std::max(hole_offset_min(), std::clamp(m_length, LENGTH_MIN, LENGTH_MAX) - hole_offset_min());
}

int GLGizmoTabs::available_heads(BoreHead *out) const
{
    if (out == nullptr)
        return 0;
    const HoleStandard *s = screw_standard();
    if (s == nullptr || s->kind != HoleStandardKind::Screw)
        return 0; // Custom: there is no catalogue to take a seat from, so no seat to offer
    int n    = 0;
    out[n++] = BoreHead::None;
    if (s->socket_d > 0.)
        out[n++] = BoreHead::SocketHead;
    if (s->button_d > 0.)
        out[n++] = BoreHead::ButtonHead;
    if (s->csink_d > 0.)
        out[n++] = BoreHead::Countersink;
    return n == 1 ? 0 : n; // a single entry (None) is nothing to choose
}

void GLGizmoTabs::set_standard(int idx)
{
    m_standard = std::clamp(idx, 0, standard_count() - 1) - 1;
    if (const HoleStandard *s = screw_standard(); s != nullptr && s->kind == HoleStandardKind::Screw) {
        if (m_screw_fit == ScrewFit::Tap && s->tap_d <= 0.)
            m_screw_fit = ScrewFit::Free;
        if (m_screw_fit == ScrewFit::Free && s->clearance_d <= 0.)
            m_screw_fit = ScrewFit::Tap; // a tap-only size
        m_diameter = screw_nominal_diameter(*s, m_screw_fit == ScrewFit::Tap);
        m_tolerance = 0.;
        // Drop a head style this screw has no dimensions for.
        if ((m_head == BoreHead::SocketHead && s->socket_d <= 0.) || (m_head == BoreHead::ButtonHead && s->button_d <= 0.)
            || (m_head == BoreHead::Countersink && s->csink_d <= 0.))
            m_head = BoreHead::None;
    }
    suggest_boss();
    probe_surface(true);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_diameter(double mm)
{
    m_diameter      = std::max(0.1, mm);
    m_preview_dirty = true;
    probe_surface(true);
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_tolerance(double mm)
{
    m_tolerance     = std::max(0., mm);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_screw_fit(ScrewFit f)
{
    m_screw_fit = f;
    if (const HoleStandard *s = screw_standard(); s != nullptr && s->kind == HoleStandardKind::Screw)
        m_diameter = screw_nominal_diameter(*s, f == ScrewFit::Tap);
    suggest_boss();
    probe_surface(true);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_head(BoreHead h)
{
    m_head          = h;
    suggest_boss();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_head_fit(double f)
{
    m_head_fit      = std::clamp(f, HEAD_FIT_MIN, HEAD_FIT_MAX);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoTabs::set_boss(bool on) { m_boss = on; suggest_boss(); m_preview_dirty = true; m_parent.set_as_dirty(); }
void GLGizmoTabs::set_boss_d(double mm) { m_boss_d = std::clamp(mm, BOSS_D_MIN, BOSS_D_MAX); m_preview_dirty = true; m_parent.set_as_dirty(); }
void GLGizmoTabs::set_boss_h(double mm) { m_boss_h = std::clamp(mm, BOSS_H_MIN, BOSS_H_MAX); m_preview_dirty = true; m_parent.set_as_dirty(); }
void GLGizmoTabs::set_thickness(double mm) { m_thickness = std::clamp(mm, THICK_MIN, THICK_MAX); suggest_boss(); m_preview_dirty = true; m_parent.set_as_dirty(); }
void GLGizmoTabs::set_angle(double deg) { m_angle = std::clamp(deg, ANGLE_MIN, ANGLE_MAX); m_preview_dirty = true; m_parent.set_as_dirty(); }

// ---------------------------------------------------------------------------------------------
// GLGizmoBase callbacks
// ---------------------------------------------------------------------------------------------

bool GLGizmoTabs::on_init()
{
    m_shortcut = Shortcut::GizmoTabs; // registered in Shortcuts.cpp
    m_desc["applied"] = _L("Tabs");
    reseat_defaults();
    m_effective_weld = WELD_MM;
    return true;
}

std::string GLGizmoTabs::on_get_name() const { return _u8L("Tabs"); }

bool GLGizmoTabs::on_is_activable() const { return m_parent.get_selection().is_single_full_instance(); }

void GLGizmoTabs::on_set_state()
{
    if (get_state() == On) {
        m_preview_dirty  = true;
        m_hover_computed = false;
        m_parent.set_as_dirty();
    } else {
        clear_anchor();
        clear_hover();
    }
}

CommonGizmosDataID GLGizmoTabs::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoTabs::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_old_object     = nullptr;
        m_hover_computed = false;
        clear_hover();
        clear_anchor();
        return;
    }
    if (mo != m_old_object || int(mo->volumes.size()) != m_old_volume_count
        || instance_matrix().matrix() != m_old_matrix.matrix()) {
        m_old_object       = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_old_matrix       = instance_matrix();
        m_preview_dirty    = true;
        m_hover_computed   = false;
        // A volume-list change is usually our own commit, and the anchor is still on the same mesh -
        // but it does not survive the volume it pointed at going away.
        if (m_pick.valid && std::find(mo->volumes.begin(), mo->volumes.end(), m_pick.mv) == mo->volumes.end())
            clear_anchor();
        m_parent.set_as_dirty();
    }
}

void GLGizmoTabs::on_render()
{
    if (get_state() != On)
        return;

    const Camera     &camera = wxGetApp().plater()->get_camera();
    const Vec2d       mouse  = m_parent.get_local_mouse_position();
    const Transform3d view   = camera.get_view_matrix();
    if (!m_hover_computed || mouse.x() != m_last_mouse.x() || mouse.y() != m_last_mouse.y()
        || !view.matrix().isApprox(m_last_view.matrix(), 1e-12)) {
        update_hover(mouse);
        m_last_mouse     = mouse;
        m_last_view      = view;
        m_hover_computed = true;
    }
    if (m_preview_dirty)
        rebuild_preview();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    // Culling stays ON: these are closed volumes, and drawing their back walls too would blend every
    // surface twice.
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));

    const Transform3d view_model_matrix = camera.get_view_matrix() * instance_matrix();
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    // Paint order matters: the depth test is off here, so later draws win. The surface underneath,
    // what the values would make, then every tab already placed.
    m_preview_face.model.render(shader);
    m_preview_added.model.render(shader);
    m_preview_cut.model.render(shader);
    m_preview_tab.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

void GLGizmoTabs::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
#if BBS_TOOLBAR_ON_TOP
    float right = x + m_parent.get_gizmos_manager().get_scaled_icons_size();
    if (gizmo_panel_near_right_edge(x, last_input_window_width, m_parent.get_canvas_size().get_width(),
                                    m_parent.get_gizmos_manager().get_scaled_icons_size()))
        GizmoImguiSetNextWIndowPos(right, y, 0.f, 0.f, ImGuiCond_Always, 1.0f, 0.0f);
    else
        GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif
    ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f), ImGuiCond_Always);
    ImGuiWrapper::push_toolbar_style(scale);
    gizmo_push_window_style();
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse
                                    | ImGuiWindowFlags_NoTitleBar);
    gizmo_push_panel_style();

    // The widest caption the rows below use, so the label column is exact rather than a magic number.
    const float label_w = gizmo_label_w(_u8L("True diameter").c_str());

    const bool     slot      = m_passage == TabPassage::Slot;
    const TabSpec  spec      = build_spec(nullptr); // the hint line reads what the geometry resolved to
    const TabOutline outline = tab_outline(spec);
    const bool     wall      = frame(nullptr).wall;

    // Tip: the only real choice in the plate. Round is the u-turn end, Flat cuts the flanks at the tip.
    {
        const std::string t0 = _u8L("Round");
        const std::string t1 = _u8L("Flat");
        const char       *labels[] = {t0.c_str(), t1.c_str()};
        const int seg = gizmo_row_segment("##tip", _u8L("Tip").c_str(), labels, 2, m_tip == TabTip::Flat ? 1 : 0,
                                          label_w,
                                          _u8L("The tab's far end: Round is the u-turn, Flat cuts the flanks "
                                               "there (a trapezoid once it is tapered).").c_str());
        if (seg >= 0)
            set_tip(seg == 1 ? TabTip::Flat : TabTip::Round);
    }
    // What the taper and the minimum-material rules resolved to.
    {
        const std::string what = outline.clamped
                                     ? Slic3r::GUI::format(_L("Clamped to a %.1f mm tip, %.0f deg flanks"),
                                                           outline.tip_w * object_scale(), outline.taper_deg)
                                     : Slic3r::GUI::format(_L("%.1f mm tip, %.0f deg flanks, %.1f / %.1f root "
                                                              "to tip"),
                                                           outline.tip_w * object_scale(), outline.taper_deg,
                                                           m_hole_offset, m_length - m_hole_offset);
        gizmo_hint(what.c_str(), outline.clamped ? GizmoCol::Warn : GizmoCol::TextDim);
    }

    float v = float(m_root_w);
    if (gizmo_row_slider("##rw", _u8L("Root width").c_str(), &v, float(ROOT_W_MIN), float(ROOT_W_MAX), "%.2f", label_w,
                         _u8L("How wide the tab is where it leaves the part.").c_str()))
        set_root_w(v);
    v = float(m_length);
    if (gizmo_row_slider("##len", _u8L("Length").c_str(), &v, float(LENGTH_MIN), float(LENGTH_MAX), "%.2f", label_w,
                         _u8L("How far the tab reaches out from its root.").c_str()))
        set_length(v);
    // The hole slides INSIDE the tab: its bounds are the tab's own ends, so the slider follows Length.
    v = float(m_hole_offset);
    if (gizmo_row_slider("##ho", _u8L("Hole offset").c_str(), &v, float(hole_offset_min()), float(hole_offset_max()),
                         "%.2f", label_w,
                         _u8L("Where the screw sits along the tab, measured from its root. On a wall that is "
                              "how far the screw is out from the face you clicked.").c_str()))
        set_hole_offset(v);
    v = float(m_taper);
    if (gizmo_row_slider("##tp", _u8L("Taper").c_str(), &v, float(TAPER_MIN), float(TAPER_MAX), "%.1f", label_w,
                         _u8L("How much the tab narrows toward its tip, on each side.").c_str()))
        set_taper(v);

    ImGui::Separator();

    {
        const std::string h0 = _u8L("Hole");
        const std::string h1 = _u8L("Slot");
        const char       *labels[] = {h0.c_str(), h1.c_str()};
        const int seg = gizmo_row_segment("##passage", _u8L("Passage").c_str(), labels, 2, slot ? 1 : 0, label_w,
                                          _u8L("A hole is drilled through the tab; a slot is an opening cut in from "
                                               "the tip, so the tab can be slid onto a screw already in place.")
                                              .c_str());
        if (seg >= 0)
            set_passage(seg == 1 ? TabPassage::Slot : TabPassage::Hole);
    }

    ImGui::Separator();

    // The screw: the same standards, fits and heads the Holes tool offers.
    {
        std::vector<std::string> names;
        std::vector<int>         idxs;
        names.push_back("Custom");
        idxs.push_back(0);
        const std::vector<HoleStandard> &t = hole_standards();
        for (size_t i = 0; i < t.size(); ++i)
            if (t[i].kind == HoleStandardKind::Screw) {
                names.push_back(t[i].designation);
                idxs.push_back(int(i) + 1);
            }
        int sel = 0;
        for (size_t k = 0; k < idxs.size(); ++k)
            if (idxs[k] == m_standard + 1) {
                sel = int(k);
                break;
            }
        const int   prev    = sel;
        const float combo_w = std::max(gizmo_panel_width() - label_w - ImGui::GetStyle().WindowPadding.x * 2.f, 40.f);
        if (render_combo(_u8L("Standard"), names, sel, label_w, combo_w) && sel != prev)
            set_standard(idxs[sel]);
    }
    v = float(m_diameter);
    if (gizmo_row_input("##dia", _u8L("Diameter").c_str(), &v, "%.2f", label_w,
                        _u8L("The screw's clearance or tap drill diameter.").c_str()))
        set_diameter(v);
    v = float(m_tolerance);
    if (gizmo_row_input("##tol", _u8L("Tolerance").c_str(), &v, "%.2f", label_w,
                        _u8L("Added to the diameter - a printed hole comes out a little tight.").c_str()))
        set_tolerance(v);
    {
        m_imgui->disabled_begin(true);
        v = float(true_diameter());
        gizmo_row_input("##true", _u8L("True diameter").c_str(), &v, "%.2f", label_w);
        m_imgui->disabled_end();
    }
    {
        const std::string f0 = _u8L("Free");
        const std::string f1 = _u8L("Tap");
        const char       *labels[] = {f0.c_str(), f1.c_str()};
        const int seg = gizmo_row_segment("##fit", _u8L("Screw fit").c_str(), labels, 2,
                                          m_screw_fit == ScrewFit::Tap ? 1 : 0, label_w,
                                          _u8L("Free is a clearance hole; Tap is the smaller drill a screw cuts its "
                                               "own thread into.").c_str());
        if (seg >= 0)
            set_screw_fit(seg == 1 ? ScrewFit::Tap : ScrewFit::Free);
    }
    // Only the heads this screw's standard actually carries, and no row at all when it carries none
    // (Custom): a seat the standard has no dimensions for would cut nothing.
    if (!slot) {
        BoreHead heads[4];
        const int n = available_heads(heads);
        if (n > 0) {
            std::vector<std::string> names;
            std::vector<BoreHead>     vals;
            for (int i = 0; i < n; ++i) {
                names.push_back(head_name(heads[i]));
                vals.push_back(heads[i]);
            }
            std::vector<const char *> lbl;
            for (const std::string &x : names)
                lbl.push_back(x.c_str());
            int cur = 0;
            for (int i = 0; i < n; ++i)
                if (vals[size_t(i)] == m_head)
                    cur = i;
            const int sel = gizmo_row_segment("##head", _u8L("Head").c_str(), lbl.data(), n, cur, label_w,
                                              _u8L("Which screw head the tab takes, or None for a bare hole.")
                                                  .c_str());
            if (sel >= 0)
                set_head(vals[size_t(sel)]);
        }
        if (m_head != BoreHead::None) {
            v = float(m_head_fit);
            if (gizmo_row_slider("##hs", _u8L("Head sink").c_str(), &v, float(HEAD_FIT_MIN), float(HEAD_FIT_MAX), "%.2f",
                                 label_w, _u8L("Sinks the head below the surface so it sits flush or proud.")
                                              .c_str()))
                set_head_fit(v);
        }
    }

    ImGui::Separator();

    if (!slot) {
        bool boss = m_boss;
        if (gizmo_row_switch("##bo", _u8L("Boss").c_str(), &boss,
                             _u8L("Raises a pad around the screw. A tapped hole needs it: the tab itself is "
                                  "thinner than the thread wants.").c_str()))
            set_boss(boss);
        if (m_boss) {
            v = float(m_boss_d);
            if (gizmo_row_input("##bd", _u8L("Boss diameter").c_str(), &v, "%.2f", label_w,
                                _u8L("Defaults to the screw head plus a wall.").c_str()))
                set_boss_d(v);
            v = float(m_boss_h);
            if (gizmo_row_slider("##bh", _u8L("Boss height").c_str(), &v, float(BOSS_H_MIN), float(BOSS_H_MAX),
                                 "%.2f", label_w, _u8L("How far the pad stands above the tab's top.").c_str()))
                set_boss_h(v);
        }
        ImGui::Separator();
    }

    v = float(m_thickness);
    if (gizmo_row_slider("##tk", _u8L("Thickness").c_str(), &v, float(THICK_MIN), float(THICK_MAX), "%.2f", label_w,
                         _u8L("How thick the tab is, measured from the surface it sits on.").c_str()))
        set_thickness(v);
    v = float(m_angle);
    if (gizmo_row_slider("##an", _u8L("Angle").c_str(), &v, float(ANGLE_MIN), float(ANGLE_MAX), "%.0f", label_w,
                         _u8L("Turns the tab about its own plate. Zero is straight out of the face you clicked "
                              "(on a wall) or away from the object's centre (on a flat face).").c_str()))
        set_angle(v);

    // The state line: where the tab stands, what it is over, and anything that stops it.
    if (!m_why.empty())
        gizmo_hint(m_why.c_str(), GizmoCol::Warn);
    else if (!m_pick.valid)
        gizmo_hint(_u8L("Click a face to place the tab. On a wall the click is where its root attaches; on a "
                        "flat face it marks the screw.")
                       .c_str());
    else {
        const int         pct  = int(m_probe.coverage * 100. + 0.5);
        const std::string line = wall
                                     ? Slic3r::GUI::format(_L("Root on the face, plate parallel to the bed, screw "
                                                              "%.2f mm out of it."),
                                                           m_hole_offset)
                                     : Slic3r::GUI::format(_L("%d%% of the tab's root is on the face, welding "
                                                              "%.2f mm into it."),
                                                           pct, m_effective_weld);
        gizmo_hint(line.c_str(), pct < int(COVERAGE_WARN * 100.) && !wall ? GizmoCol::Warn : GizmoCol::TextDim);

        // A seat deeper than the plate cuts through its underside: worth saying, since it is invisible
        // on the tab itself.
        if (!slot && m_head != BoreHead::None && seat_depth() > m_thickness + 1e-6)
            gizmo_hint(_u8L("The head seat is deeper than the tab: it opens through the tab's underside.")
                           .c_str(),
                       GizmoCol::Warn);
    }

    if (m_pick.valid && gizmo_primary_button(_L("Add tab").utf8_str().data()))
        commit();

    const wxString applied = from_u8(Slic3r::GUI::format("%s: %d", m_desc.at("applied").c_str(), tab_count()));
    m_imgui->text(applied);
    if (tab_count() > 0) {
        ImGui::SameLine();
        if (m_imgui->button(_L("Clear all")))
            clear_all();
        gizmo_hint(_u8L("Right-click a tab to remove it.").c_str());
    }

    gizmo_pop_panel_style();
    GizmoImguiEnd();
    gizmo_pop_window_style();
    ImGuiWrapper::pop_toolbar_style();
}

bool GLGizmoTabs::on_mouse(const wxMouseEvent &mouse_event)
{
    if (get_state() != On)
        return false;

    if (mouse_event.RightDown()) {
        // Right-click on a tab already added removes it; anywhere else it is the panel's Add tab, the
        // way the Extrude gizmo confirms its pick (PP-4).
        if (remove_hovered()) {
            m_parent.request_extra_frame();
            return true;
        }
        if (m_pick.valid && commit()) {
            m_parent.request_extra_frame();
            return true;
        }
        return false;
    }

    if (!mouse_event.LeftUp())
        return false;
    // A click anchors the screw. Clicking off the object falls through to the canvas, and clicking a
    // face again moves the tab.
    if (!anchor_at(m_parent.get_local_mouse_position()))
        return false;
    m_parent.request_extra_frame();
    return true;
}

void GLGizmoTabs::on_register_raycasters_for_picking()
{
    // Face picking uses raycast_object_face on the scene volumes, so no gizmo raycaster is added.
}

void GLGizmoTabs::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
}

} // namespace GUI
} // namespace Slic3r
