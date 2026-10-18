#include "GLGizmoPerforation.hpp"
#include "GLGizmoUtils.hpp"
#include "GizmoStyle.hpp" // [ORCAPORT:PP-1] fork panel style (docs/handoff.md section 28)

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/GUI/format.hpp" // [ORCAPORT:PP-6] GUI::format for the translated To hint

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

// [ORCAPORT:PP-2] The 3D overlays use the shared scheme (GizmoStyle.hpp): the surface under the cursor
// is teal, the picked face green, the pattern the current values would cut yellow, and every committed
// perforation red (they are all NEGATIVE volumes - this tool never adds material).
const char *const PERFORATION_NAME = "Perforation";

// [ORCAPORT:PP-1] The four pattern glyphs. A flat square plate with the pattern cut through it,
// drawn straight into the draw list like the Support Zones glyphs, so it scales with the DPI without
// a resource. The plate and the shared margin live in GizmoStyle.hpp - the Holes operation glyphs
// are built on the same two.
constexpr float PP_PI_F = 3.14159265358979323846f;

// [ORCAPORT:PP-6] How far past the "To" face the pattern carries, in mm. The pattern has to reach the
// face and stop there, but a cap exactly coplanar with it slices as a zero-thickness sliver (the PP-4
// lesson), so it goes slightly past. A fifth of a millimetre is under one extrusion width - it prints
// as nothing where the face is the far side of the part, and as a dimple well under a layer where
// there is material behind it - and it is 200000 units at the slicer's 1e-6 mm resolution.
constexpr double PP_TARGET_OVERSHOOT = 0.2;

// Lines - three parallel slots.
void pp_lines(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float x0 = p.x + kGizmoGlyphInset * s, x1 = p.x + (1.f - kGizmoGlyphInset) * s;
    const float h = 0.095f * s, r = 0.0475f * s;
    for (int i = 0; i < 3; ++i) {
        const float y = p.y + (0.2625f + 0.19f * float(i)) * s;
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), col, r);
    }
}

// Grid - two families of slots crossing, i.e. the intersections are the holes.
void pp_grid(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float a = kGizmoGlyphInset * s, b = (1.f - kGizmoGlyphInset) * s, t = 0.09f * s;
    const float u1 = 0.34f * s, u2 = 0.57f * s;
    dl->AddRectFilled(ImVec2(p.x + a, p.y + u1), ImVec2(p.x + b, p.y + u1 + t), col);
    dl->AddRectFilled(ImVec2(p.x + a, p.y + u2), ImVec2(p.x + b, p.y + u2 + t), col);
    dl->AddRectFilled(ImVec2(p.x + u1, p.y + a), ImVec2(p.x + u1 + t, p.y + b), col);
    dl->AddRectFilled(ImVec2(p.x + u2, p.y + a), ImVec2(p.x + u2 + t, p.y + b), col);
}

// Circles - four round holes.
void pp_circles(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float r = 0.1225f * s;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            dl->AddCircleFilled(ImVec2(p.x + (0.3325f + 0.335f * float(i)) * s,
                                       p.y + (0.3325f + 0.335f * float(j)) * s), r, col, 18);
}

// Honeycomb - a 2/3/2 cluster of COMPLETE cells, sized to sit inside the shared pattern box, with NO
// clip rect at all.
//
// 🔑 Every earlier version clipped a comb to the box, and a clipped comb is at the mercy of where the
// cut lands. The lattice's stagger put its two cuts at different distances from the centre, which read
// as "the comb is off-centre" no matter how centred the maths was, and the cut edges are drawn through
// ImGui's anti-aliased path, which does not behave identically at both ends - one cell of the middle
// row came out half drawn. Seven whole cells in rows of 2, 3 and 2 are mirror-symmetric about both
// axes by construction: they cannot be off-centre, they cannot be half-drawn, and there is nothing to
// line up. The cluster is sized so it also clears the plate's border, which is what the cut used to
// guarantee.
void pp_honeycomb(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    // A 1.1 px wall is the minimum that survives: two adjacent filled polygons in ImGui do not
    // antialias against each other, so a fraction-of-a-pixel gap simply disappears and the comb stops
    // reading as cells. draw_glyph protects its strokes the same way (max(1.2, s*0.075)).
    const float r      = 0.119f * s;                 // lattice radius; cell pitch = 1.732 r
    const float wall   = std::max(1.1f, 0.019f * s); // the gap between cells, in PIXELS
    const float r_cell = r - wall / 1.732f;          // shrinking r shrinks the across-flats width by 1.732
    const float pitch  = 1.732f * r;                 // centre to centre, same row
    const float rowh   = 1.5f * r;                   // centre to centre, adjacent rows
    const ImVec2 c(p.x + 0.5f * s, p.y + 0.5f * s);
    for (int j = -1; j <= 1; ++j) {
        const float cy = c.y + rowh * float(j);
        const int   n  = (j == 0) ? 3 : 2; // 3 cells in the middle row, 2 above and below
        for (int i = 0; i < n; ++i) {
            const float cx = c.x + pitch * (float(i) - 0.5f * float(n - 1));
            ImVec2 pts[6];
            for (int k = 0; k < 6; ++k) {
                const float a = PP_PI_F / 180.f * (60.f * float(k) - 90.f);
                pts[k] = ImVec2(cx + r_cell * std::cos(a), cy + r_cell * std::sin(a));
            }
            dl->AddConvexPolyFilled(pts, 6, col);
        }
    }
}

} // namespace

GLGizmoPerforation::GLGizmoPerforation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

ModelObject *GLGizmoPerforation::model_object() const
{
    if (m_c == nullptr || m_c->selection_info() == nullptr)
        return nullptr;
    return m_c->selection_info()->model_object();
}

Transform3d GLGizmoPerforation::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

bool GLGizmoPerforation::on_init()
{
    m_shortcut = Shortcut::GizmoPerforation; // [ORCAPORT:PP-1] registered in Shortcuts.cpp

    m_desc["pattern"]    = _L("Pattern");
    m_desc["spacing"]    = _L("Spacing");
    m_desc["width"]      = _L("Hole width");
    m_desc["boundary"]   = _L("Boundary");
    m_desc["angle"]      = _L("Angle");
    m_desc["invert"]     = _L("Invert (grooves)");
    m_desc["depth_hint"] = _L("Depth follows the face to the first surface behind it.");
    m_desc["to"]         = _L("To");
    m_desc["to_pick"]    = _L("Pick face");
    m_desc["to_cancel"]  = _L("Cancel");
    m_desc["to_clear"]   = _L("Clear");
    m_desc["to_tip"]     = _L("Stops the pattern on a second face instead of at the first surface behind this one. "
                              "Pick a face parallel to the picked one; note it has to be visible, so orbit to it.");
    m_desc["to_noface"]  = _L("No face under the cursor.");
    m_desc["to_parallel"] = _L("The target face is not parallel to the picked face.");
    m_desc["to_behind"]  = _L("The target face has to be behind the picked one.");
    m_desc["click_hint"] = _L("Click a flat face to pick it, adjust the pattern, then press Perforate (or right-click).");
    m_desc["drop_hint"]  = _L("Esc closes the tool and drops the pick.");
    m_desc["perforate"]  = _L("Perforate");
    m_desc["applied"]    = _L("Perforated");
    m_desc["forbidden"]  = _L("Only flat faces can be perforated.");
    m_desc["horizontal"] = _L("Near-horizontal face: holes may bridge or leave gaps.");
    m_desc["reduced"]    = _L("Hole width is too large for the spacing.");
    return true;
}

std::string GLGizmoPerforation::on_get_name() const { return _u8L("Perforations"); }

bool GLGizmoPerforation::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

CommonGizmosDataID GLGizmoPerforation::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoPerforation::on_set_state()
{
    if (get_state() == On) {
        m_merged_dirty = true;
        m_pick_dirty   = true;
        m_parent.set_as_dirty();
    } else {
        clear_hover();
        // The pick goes with the panel: leaving it set would resurrect a green face and a yellow
        // pattern the next time the tool opens, on a mesh that may have been edited meanwhile. The
        // [ORCAPORT:PP-6] To target and the armed pick mode go with it.
        clear_pick();
    }
}

void GLGizmoPerforation::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_merged_its.indices.clear();
        m_merged_its.vertices.clear();
        m_merged_dirty = true;
        clear_hover();
        clear_pick();
        return;
    }
    if (mo != m_old_model_object || int(mo->volumes.size()) != m_old_volume_count) {
        m_old_model_object = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_merged_dirty     = true;
        // [ORCAPORT:PP-2] A volume-list change is usually OUR OWN cut, and the picked face is still
        // there, so the pick survives it (the panel can Cut twice from one pick, like Extrude). What
        // it does NOT survive is the volume it pointed at going away.
        if (m_pick.valid && std::find(mo->volumes.begin(), mo->volumes.end(), m_pick.mv) == mo->volumes.end())
            clear_pick();
        // [ORCAPORT:PP-6] The same rule for the To target, which is the second half of a pair.
        if (m_target.valid && std::find(mo->volumes.begin(), mo->volumes.end(), m_target.mv) == mo->volumes.end())
            clear_target();
        m_parent.set_as_dirty();
    }
}

void GLGizmoPerforation::rebuild_merged_mesh()
{
    TriangleMesh mesh;
    const ModelObject *mo = model_object();
    if (mo != nullptr)
        for (const ModelVolume *v : mo->volumes) {
            if (v == nullptr || !v->is_model_part())
                continue;
            TriangleMesh part = v->mesh();
            part.transform(v->get_matrix());
            mesh.merge(part);
        }
    m_merged_its = mesh.its;
    // The applied overlay is derived from the volume list too, and this is the one place that runs on
    // activation and on every volume change - including the cut this tool just added.
    rebuild_applied_patterns();
}

void GLGizmoPerforation::clear_hover()
{
    m_hover_mv     = nullptr;
    m_hover_facet  = -1;
    m_hover_horizontal = false;
    m_hover_region.clear();
    m_last_hit = Vec3d::Constant(1e30);
    m_face_highlight.reset();
    m_parent.set_as_dirty();
}

// [ORCAPORT:PP-2] The four overlays of the pick/adjust/cut flow.
//
// The hovered surface, in the scheme's teal. Skipped while the cursor is on the picked face: the green
// already says "this is the one you clicked", and teal over it only muddies the two.
void GLGizmoPerforation::rebuild_hover_highlight()
{
    m_face_highlight.reset();
    if (m_hover_mv == nullptr || m_hover_region.empty())
        return;
    if (m_pick.valid && m_pick.mv == m_hover_mv && m_pick.facet == m_hover_facet)
        return;
    const indexed_triangle_set patch = build_coplanar_patch(m_hover_mv, m_hover_region, m_face_cache, 0.05f);
    if (patch.indices.empty())
        return;
    m_face_highlight.init_from(patch);
    m_face_highlight.set_color(gizmo_surface_colour());
}

// The picked surface, in the scheme's green: only a click turns a surface green.
void GLGizmoPerforation::rebuild_picked_highlight()
{
    m_face_picked.reset();
    if (! m_pick.valid || m_pick.mv == nullptr)
        return;
    const std::vector<int> region = coplanar_region(m_pick.mv, size_t(m_pick.facet), m_face_cache);
    if (region.empty())
        return;
    const indexed_triangle_set patch = build_coplanar_patch(m_pick.mv, region, m_face_cache, 0.05f);
    if (patch.indices.empty())
        return;
    m_face_picked.init_from(patch);
    m_face_picked.set_color(gizmo_picked_colour());
}

// What the current values would cut on the picked face, in the scheme's yellow. The one thing that
// moves when a slider moves - and it is NOT drawn while it is exactly what was just applied, so a
// committed cut reads as its own red instead of yellow over red.
void GLGizmoPerforation::rebuild_proposed_pattern()
{
    m_pattern_proposed.reset();
    if (! m_pick.valid || m_pick.mv == nullptr || ! m_show_proposal)
        return;

    ExPolygons           pattern;
    indexed_triangle_set prism;
    if (! build_pattern_at(m_pick.hit_obj, m_pick.mv, size_t(m_pick.facet), pattern, prism) || prism.indices.empty())
        return;
    m_pattern_proposed.init_from(prism);
    m_pattern_proposed.set_color(gizmo_proposed_colour());
}

// Every perforation already committed, in the scheme's red. Built from the actual volumes, so it always
// matches what was baked in, and it is what makes an already-cut area readable in the gizmo.
void GLGizmoPerforation::rebuild_applied_patterns()
{
    m_patterns_applied.reset();

    indexed_triangle_set applied;
    const ModelObject  *mo = model_object();
    if (mo != nullptr) {
        for (const ModelVolume *v : mo->volumes) {
            if (v == nullptr || v->name != PERFORATION_NAME)
                continue;
            indexed_triangle_set part = v->mesh().its;
            const Transform3d    m    = v->get_matrix();
            if (! m.isApprox(Transform3d::Identity()))
                for (stl_vertex &p : part.vertices)
                    p = (m * p.cast<double>()).cast<float>();
            its_merge(applied, part);
        }
    }
    if (applied.indices.empty())
        return;
    m_patterns_applied.init_from(applied);
    m_patterns_applied.set_color(gizmo_cut_colour());
}

int GLGizmoPerforation::gizmo_applied_count() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    return int(std::count_if(mo->volumes.begin(), mo->volumes.end(),
                             [](const ModelVolume *v) { return v != nullptr && v->name == PERFORATION_NAME; }));
}

// A left click's job: remember the face, its click point (the pattern is anchored there) and whether it
// is close to horizontal (the panel warns about it). Nothing is cut here.
void GLGizmoPerforation::pick_at(const Vec2d &screen_pos)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit    = Vec3d::Zero();
    // The tool's own negatives are picked THROUGH, so clicking an area that is already perforated picks
    // the face behind it instead of the cut volume (the same rule the Extrude tool applies to its own).
    if (! raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit,
                              [](const ModelVolume *v) { return v == nullptr || v->name != PERFORATION_NAME; }))
        return;

    const Vec3d normal_obj = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()).normalized();
    const Vec3d up         = instance_matrix().linear().inverse() * Vec3d::UnitZ();

    m_pick.mv         = mv;
    m_pick.facet      = int(facet);
    m_pick.hit_obj    = instance_matrix().inverse() * hit;
    m_pick.horizontal = up.allFinite() && up.norm() > 1e-9 && normal_obj.allFinite() && normal_obj.norm() > 1e-9 &&
                        std::abs(normal_obj.dot(up.normalized())) > 0.9;
    m_pick.valid = true;
    m_pick_dirty = true;
    m_show_proposal = true;
    rebuild_picked_highlight();
    m_parent.set_as_dirty();
}

void GLGizmoPerforation::clear_pick()
{
    // [ORCAPORT:PP-6] The target is measured from the picked face, so it cannot outlive it.
    clear_target();
    if (! m_pick.valid && ! m_face_picked.is_initialized() && ! m_pattern_proposed.is_initialized())
        return;
    m_pick       = Pick();
    m_pick_dirty = true;
    m_show_proposal = true;
    m_face_picked.reset();
    m_pattern_proposed.reset();
    // The cursor is usually still on the face it just released, and the hover highlight was suppressed
    // while it was picked: put it back now instead of waiting for the cursor to move off the face.
    rebuild_hover_highlight();
    m_parent.set_as_dirty();
}

// The panel's Perforate button: cut the pattern the current values describe into the picked face. The
// pick is KEPT, so another press cuts another one and a value can be tried without re-picking.
bool GLGizmoPerforation::create_from_pick()
{
    if (! m_pick.valid || m_pick.mv == nullptr)
        return false;

    ExPolygons           pattern;
    indexed_triangle_set prism;
    if (! build_pattern_at(m_pick.hit_obj, m_pick.mv, size_t(m_pick.facet), pattern, prism) || prism.indices.empty())
        return false;

    add_pattern_volume(prism);
    m_pick_dirty    = true;  // the applied overlay gained a volume
    m_show_proposal = false; // the proposal IS what was just cut: show the red, not yellow over red
    return true;
}

// The old PP-1 shape of the call: pick, then commit, for MCP scripts that were written against it.
bool GLGizmoPerforation::apply_at(const Vec2d &screen_pos)
{
    pick_at(screen_pos);
    return m_pick.valid && create_from_pick();
}

// [ORCAPORT:PP-6] The "To" pick: a second face the pattern stops on. Accepted only when it is
// PARALLEL to the picked face (the hole path has one axis) and BEHIND it (this tool cuts inward
// only); anything else sets a reason the panel shows and leaves the previous target alone.
void GLGizmoPerforation::pick_target_at(const Vec2d &screen_pos)
{
    m_target_why = TargetWhy::None;
    if (! m_pick.valid || m_pick.mv == nullptr) {
        m_target_why    = TargetWhy::NoFace;
        m_pick_target   = false;
        return;
    }

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit    = Vec3d::Zero();
    // The tool's own negatives are picked through, exactly as the first pick does it.
    const auto not_ours = [](const ModelVolume *v) { return v == nullptr || v->name != PERFORATION_NAME; };
    if (! raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit, not_ours)) {
        m_target_why = TargetWhy::NoFace;
        return;
    }

    const Vec3d n = facet_normal_in_world(m_pick.mv->mesh().its, m_pick.facet, m_pick.mv->get_matrix()).normalized();
    if (! n.allFinite() || n.norm() < 1e-9) {
        m_target_why = TargetWhy::NoFace;
        return;
    }
    const Vec3d n_to = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()).normalized();
    if (! n_to.allFinite() || std::abs(n.dot(n_to)) < 0.99) {
        m_target_why = TargetWhy::NotParallel;
        return;
    }

    // Both points in object space: the picked face is planar, so projecting the target's hit point
    // onto its normal gives the plane-to-plane distance, whatever the target's lateral offset.
    const Vec3d  hit_obj = instance_matrix().inverse() * hit;
    const double distance = -(hit_obj - m_pick.hit_obj).dot(n);
    if (distance < 0.05) {
        m_target_why = TargetWhy::NotBehind;
        return;
    }

    m_target       = Target{ true, mv, int(facet), distance };
    m_pick_target  = false;
    m_pick_dirty   = true;  // the depth changed: the proposal is built from it
    m_show_proposal = true;
    rebuild_target_highlight();
    m_parent.set_as_dirty();
}

void GLGizmoPerforation::clear_target()
{
    m_pick_target = false;
    m_target_why  = TargetWhy::None;
    if (! m_target.valid && ! m_face_target.is_initialized())
        return;
    m_target = Target();
    m_face_target.reset();
    m_pick_dirty    = true;  // back to the depth probe
    m_show_proposal = true;
    m_parent.set_as_dirty();
}

// The target face, in the picked colour: the two green patches are the two ends of the hole path.
void GLGizmoPerforation::rebuild_target_highlight()
{
    m_face_target.reset();
    if (! m_target.valid || m_target.mv == nullptr)
        return;
    const std::vector<int> region = coplanar_region(m_target.mv, size_t(m_target.facet), m_face_cache);
    if (region.empty())
        return;
    const indexed_triangle_set patch = build_coplanar_patch(m_target.mv, region, m_face_cache, 0.05f);
    if (patch.indices.empty())
        return;
    m_face_target.init_from(patch);
    m_face_target.set_color(gizmo_picked_colour());
}

// [ORCAPORT:PP-6] Why a target pick was refused, as text for the MCP tool. The panel maps the same
// enum to translated msgids, so neither side parses the other's strings.
const char *GLGizmoPerforation::gizmo_target_refusal() const
{
    switch (m_target_why) {
    case TargetWhy::None:        return "";
    case TargetWhy::NoFace:      return "No face under the screen point (pick the face the pattern reaches, and orbit to it if it is not visible)";
    case TargetWhy::NotParallel: return "The target face is not parallel to the picked face";
    case TargetWhy::NotBehind:   return "The target face has to be behind the picked one";
    }
    return "";
}

bool GLGizmoPerforation::gizmo_pick_target_at(const Vec2d &screen_pos)
{
    pick_target_at(screen_pos);
    return m_target.valid && m_target_why == TargetWhy::None;
}

void GLGizmoPerforation::gizmo_clear_target()
{
    clear_target();
}

// The picked face as a clean 2D polygon in the face plane: the coplanar region's triangles,
// projected onto the plane and unioned. The plane origin is `origin_obj` (object space).
ExPolygons GLGizmoPerforation::face_domain(const ModelVolume *mv, size_t facet, const Vec3d &normal_obj,
                                           const Vec3d &origin_obj)
{
    const std::vector<int> region = coplanar_region(mv, facet, m_face_cache);
    if (region.empty())
        return {};

    Vec3d ux, uy;
    face_plane_axes(normal_obj, ux, uy);

    const Transform3d to_obj = mv->get_matrix();
    Polygons          tris;
    tris.reserve(region.size());
    for (const int f : region) {
        const Vec3i32 &idx = mv->mesh().its.indices[f];
        Polygon        tri;
        for (int k = 0; k < 3; ++k) {
            const Vec3d v   = to_obj * mv->mesh().its.vertices[idx(k)].cast<double>();
            const Vec3d rel = v - origin_obj;
            tri.points.emplace_back(Point::new_scale(rel.dot(ux), rel.dot(uy)));
        }
        tris.emplace_back(std::move(tri));
    }
    return union_ex(tris);
}

bool GLGizmoPerforation::build_pattern_at(const Vec3d &hit_obj, const ModelVolume *mv, size_t facet,
                                          ExPolygons &pattern, indexed_triangle_set &prism)
{
    if (mv == nullptr)
        return false;

    const Vec3d normal_obj = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix());
    if (!normal_obj.allFinite() || normal_obj.norm() < 1e-9)
        return false;
    const Vec3d n = normal_obj.normalized();

    const ExPolygons domain = face_domain(mv, facet, n, hit_obj);
    if (domain.empty())
        return false;

    pattern = make_perforation_pattern(domain, m_params);
    if (pattern.empty())
        return false;

    // Depth: the first surface the inward ray crosses. That is the far side of the local wall
    // (a hollow shell) or the far side of the object (solid). Fall back to the whole object.
    double z_out = 0.5;
    double z_in  = -m_params.margin - 5.;
    // [ORCAPORT:PP-6] With a To target the depth is not probed at all: it is the distance to the face
    // the user picked, plus the small overshoot that keeps the cut from ending coplanar with it. This
    // is what lets a pattern cross a void and stop on a chosen far wall instead of the first one the
    // ray happens to find.
    if (m_target.valid) {
        z_in = -(m_target.distance + PP_TARGET_OVERSHOOT);
    } else if (!m_merged_its.indices.empty()) {
        const AABBMesh mesh(m_merged_its);
        const Vec3d    start = hit_obj - n * 1e-3;
        double         depth = 0.;
        for (const AABBMesh::hit_result &h : mesh.query_ray_hits(start, -n))
            if (h.is_hit() && h.distance() > 1e-3 && (depth == 0. || h.distance() < depth))
                depth = h.distance();
        if (depth > 0.) {
            z_in = -(depth + 0.5);
        } else {
            double lo = 0., hi = 0.;
            for (const stl_vertex &v : m_merged_its.vertices) {
                const double d = (v.cast<double>() - hit_obj).dot(n);
                lo             = std::min(lo, d);
                hi             = std::max(hi, d);
            }
            z_out = hi + 0.5;
            z_in  = lo - 0.5;
        }
    }

    Vec3d ux, uy;
    face_plane_axes(n, ux, uy);
    prism = extrude_perforation_pattern(pattern, hit_obj, ux, uy, n, z_in, z_out);
    return !prism.indices.empty();
}

void GLGizmoPerforation::update_hover()
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit    = Vec3d::Zero();
    // The tool's own negatives are picked THROUGH, so clicking an area that is already perforated picks
    // the face behind it rather than the cut volume (the rule Extrude applies to its own volumes).
    const auto not_ours = [](const ModelVolume *v) { return v == nullptr || v->name != PERFORATION_NAME; };
    if (!raycast_object_face(m_parent.get_local_mouse_position(), m_parent.get_selection(), mo, clipping, volume, mv, facet, hit,
                             not_ours)) {
        if (m_hover_mv != nullptr)
            clear_hover();
        return;
    }

    const Vec3d hit_obj      = instance_matrix().inverse() * hit;
    const bool  face_changed = (mv != m_hover_mv || int(facet) != m_hover_facet);
    if (!face_changed && (hit_obj - m_last_hit).norm() < 0.25)
        return;

    if (face_changed) {
        m_hover_mv     = mv;
        m_hover_facet  = int(facet);
        // A near-horizontal face (normal close to bed Z) means the holes bridge or leave gaps.
        const Vec3d n_obj = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()).normalized();
        const Vec3d up    = instance_matrix().linear().inverse() * Vec3d::UnitZ();
        m_hover_horizontal =
            up.allFinite() && up.norm() > 1e-9 && n_obj.allFinite() && n_obj.norm() > 1e-9 &&
            std::abs(n_obj.dot(up.normalized())) > 0.9;
        m_hover_region = coplanar_region(mv, facet, m_face_cache);
        rebuild_hover_highlight();
    }
    m_last_hit = hit_obj;
    m_parent.set_as_dirty();
}

void GLGizmoPerforation::set_params(const PerforationParams &p)
{
    m_params         = p;
    // [ORCAPORT:PP-6] Grid is grooves in the geometry (see make_perforation_pattern), so the state the
    // panel and the MCP report read says so too instead of disagreeing with what is cut.
    if (m_params.kind == PerforationKind::Grid)
        m_params.invert = true;
    m_pick_dirty     = true; // the proposal is built from the values
    m_show_proposal  = true; // ...and a new set of values is not what was applied
    m_parent.set_as_dirty();
}

bool GLGizmoPerforation::gizmo_pick_at(const Vec2d &screen_pos)
{
    pick_at(screen_pos);
    return m_pick.valid;
}

bool GLGizmoPerforation::gizmo_create()
{
    return create_from_pick();
}

void GLGizmoPerforation::gizmo_clear_pick()
{
    clear_pick();
}

bool GLGizmoPerforation::gizmo_apply_at(const Vec2d &screen_pos)
{
    return apply_at(screen_pos);
}

bool GLGizmoPerforation::gizmo_face_info_at(const Vec2d &screen_pos, int &facet, int &region_facets, Vec3d &normal)
{
    facet         = -1;
    region_facets = 0;
    normal        = Vec3d::Zero();

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return false;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             f      = 0;
    Vec3d              hit    = Vec3d::Zero();
    if (!raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, f, hit))
        return false;

    facet         = int(f);
    normal        = facet_normal_in_world(mv->mesh().its, int(f), mv->get_matrix());
    region_facets = int(coplanar_region(mv, f, m_face_cache).size());
    return true;
}

void GLGizmoPerforation::add_pattern_volume(const indexed_triangle_set &prism)
{
    ModelObject *mo = model_object();
    const int    oi = m_parent.get_selection().get_object_idx();
    if (mo == nullptr || oi < 0 || prism.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    plater->take_snapshot(_u8L("Perforate face"));

    indexed_triangle_set copy = prism;
    ModelVolume        *v     = mo->add_volume(TriangleMesh(std::move(copy)), ModelVolumeType::NEGATIVE_VOLUME, false);
    v->name                   = PERFORATION_NAME;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

bool GLGizmoPerforation::on_mouse(const wxMouseEvent &mouse_event)
{
    // [ORCAPORT:PP-4] Right-click CONFIRMS: it cuts the picked pattern, the same as the panel's
    // Perforate button. Esc is what drops the pick. A right-click with nothing picked is left to the
    // canvas.
    if (mouse_event.RightDown()) {
        // [ORCAPORT:PP-6] A right click while the To pick is armed puts the button back instead of
        // committing: an armed pick is a mode, and the same gesture that commits a pattern should not
        // cut one while the user is aiming at a face.
        if (m_pick_target) {
            m_pick_target = false;
            m_target_why  = TargetWhy::None;
            m_parent.set_as_dirty();
            return true;
        }
        if (! m_pick.valid)
            return false;
        create_from_pick();
        m_parent.request_extra_frame();
        return true;
    }
    if (! mouse_event.LeftDown())
        return false;
    // [ORCAPORT:PP-6] Armed: the click belongs to the target face and must not re-pick the face the
    // pattern is anchored to. Swallowed either way, so a click on empty space does not fall through
    // to the canvas and drop the selection under the tool.
    if (m_pick_target) {
        pick_target_at(m_parent.get_local_mouse_position());
        m_parent.request_extra_frame();
        return true;
    }
    // A click PICKS the face; nothing is cut until Perforate or a right-click. Swallow the click only
    // when it landed on a face, so a click on empty space still falls through to the canvas.
    pick_at(m_parent.get_local_mouse_position());
    return m_pick.valid;
}

void GLGizmoPerforation::on_render()
{
    if (get_state() != On)
        return;
    if (m_merged_dirty) {
        rebuild_merged_mesh();
        m_merged_dirty = false;
    }
    update_hover();

    // [ORCAPORT:PP-6] The proposal follows the values LIVE, the drag included: a slider marks it dirty on
    // every frame whose value changed. It used to wait for the release of the item that set it, which is
    // why the highlight only caught up when the mouse was let go. The flicker that rule was written for
    // came from the old cursor-following hover ghost, gone since PP-2, and Extrude already rebuilds its
    // preview this way.
    if (m_pick_dirty) {
        rebuild_proposed_pattern();
        m_pick_dirty = false;
    }
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera  &camera = wxGetApp().plater()->get_camera();
    const Transform3d view = camera.get_view_matrix();

    shader->start_using();
    glsafe(::glEnable(GL_DEPTH_TEST));
    // 🔑 An applied pattern IS the negative volume already in the scene, at exactly the same depth, so
    // a strict test would reject the overlay and the red would never be seen. LEQUAL lets the overlay
    // win against its own volume while geometry genuinely in front still occludes it.
    glsafe(::glDepthFunc(GL_LEQUAL));
    // Culling stays ON: the patterns are closed prisms and the patches come from the volume's own
    // (outward-wound) triangles, so only the surfaces facing the camera should blend.
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    if (m_hover_mv != nullptr && m_face_highlight.is_initialized()) {
        // The surface patches are built from the volume's own mesh, so they carry the volume matrix.
        shader->set_uniform("view_model_matrix", view * instance_matrix() * m_hover_mv->get_matrix());
        m_face_highlight.render();
    }
    if (m_pick.valid && m_pick.mv != nullptr && m_face_picked.is_initialized()) {
        shader->set_uniform("view_model_matrix", view * instance_matrix() * m_pick.mv->get_matrix());
        m_face_picked.render();
    }
    // [ORCAPORT:PP-6] The To face is drawn like the picked one (same colour, same kind of patch): it is
    // the second end of the path the pattern cuts.
    if (m_target.valid && m_target.mv != nullptr && m_face_target.is_initialized()) {
        shader->set_uniform("view_model_matrix", view * instance_matrix() * m_target.mv->get_matrix());
        m_face_target.render();
    }
    if (m_pattern_proposed.is_initialized()) {
        // The patterns are object-space prisms, like the negative volumes they become.
        shader->set_uniform("view_model_matrix", view * instance_matrix());
        m_pattern_proposed.render();
    }
    if (m_patterns_applied.is_initialized()) {
        shader->set_uniform("view_model_matrix", view * instance_matrix());
        m_patterns_applied.render();
    }

    glsafe(::glDepthFunc(GL_LESS));
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    shader->stop_using();
}

void GLGizmoPerforation::on_render_input_window(float x, float y, float bottom_limit)
{
    if (model_object() == nullptr)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    // [ORCAPORT:UI-1] Left-aligned under the icon while the panel fits to its right. Once it would run
    // past the canvas edge, hang it to the left instead of letting GizmoImguiSetNextWIndowPos()'s
    // left-aligned clamp slide it along that edge: the panel's right edge then stays under its own
    // icon. Width 0 skips the clamp, which only makes sense for a left edge.
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
    // [ORCAPORT:PP-1] Fork panel style (docs/handoff.md section 28): the shared palette, one fixed
    // narrow width with wrapped hints, and the shared widget layer. The panel used to measure its
    // own width from its controls; measure it once here instead, so no string can move it.
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
    render_panel_body();
    gizmo_pop_panel_style();
    GizmoImguiEnd();
    gizmo_pop_window_style();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoPerforation::render_panel_body()
{
    // Compared at the end: a value that changed brings the proposal back (it was hidden while it was
    // exactly what the last Cut applied).
    const PerforationParams before = m_params;

    // Fixed, independent ranges. The UI never clamps one value against another: the geometry uses
    // exactly what is set, and the panel only warns when a wall gets too thin, so dragging or
    // typing a value always takes effect.
    constexpr double SPACING_MIN = 1.0,  SPACING_MAX = 25.0;
    constexpr double WIDTH_MIN   = 0.2,  WIDTH_MAX   = 10.0;
    constexpr double MARGIN_MIN  = 1.0,  MARGIN_MAX  = 25.0;
    constexpr double ANGLE_MIN   = 0.0;
    // [ORCAPORT:PP-6] A single family (Lines) gets the full quarter, horizontal -> vertical; Grid keeps
    // 0..45 because its own 90-degree cross already covers the rest (see PerforationPattern.hpp).
    const double     ANGLE_MAX   = perforation_max_angle_deg(m_params.kind);

    // Pattern: the four choices as icons with the name on hover (section 28 records the icon button
    // standard). Drawn without a caption, like the Extrude panel's own mode row.
    {
        void (*const drawers[4])(ImDrawList *, const ImVec2 &, float, ImU32) =
            {pp_lines, pp_grid, pp_honeycomb, pp_circles};
        // The names are the ones the old dropdown used, untranslated then and now: they are the
        // tooltip, and turning them into msgids is a translation pass of its own.
        const char *const names[4] = {"Lines", "Grid", "Honeycomb", "Circles"};
        for (int i = 0; i < 4; ++i) {
            if (i > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x);
            if (gizmo_icon_toggle(names[i], int(m_params.kind) == i, names[i], drawers[i])) {
                const PerforationKind kind = PerforationKind(i);
                if (kind != m_params.kind) {
                    // [ORCAPORT:PP-6] An angle the new pattern cannot express would leave the slider
                    // pinned at its maximum while the geometry kept the old value, so it goes to 0.
                    // Switch to the wider pattern and the value in range is kept.
                    m_params.angle_deg = perforation_angle_for_kind(kind, m_params.angle_deg);
                    m_params.kind      = kind;
                }
                // [ORCAPORT:PP-6] Grid is grooves in the geometry, so the panel state says so (the
                // builder forces it too, which is what a script or a test would hit).
                if (m_params.kind == PerforationKind::Grid)
                    m_params.invert = true;
                m_pick_dirty       = true;
            }
        }
    }

    ImGui::Separator();

    // The widest caption the rows below use, so the label column is exact (section 28). Max over the
    // captions actually drawn in the column: measuring one of them is how "Hole width" ended up
    // clipped to "Hole wi".
    float label_w = 0.f;
    for (const char *key : {"spacing", "width", "boundary", "angle", "to"})
        label_w = std::max(label_w, ImGui::CalcTextSize(m_desc.at(key).utf8_str().data()).x);
    label_w += ImGui::GetStyle().ItemSpacing.x * 0.5f;

    // A slider with a typed field beside it, and NO [-][+] step buttons: the buttons cost about a
    // third of the width and the field still takes a typed value, clamped to the same range. Every row
    // marks the proposal dirty: the pattern on the picked face is rebuilt from the values.
    float spacing = float(m_params.spacing);
    if (gizmo_row_slider("##spacing", m_desc.at("spacing").utf8_str().data(), &spacing,
                         float(SPACING_MIN), float(SPACING_MAX), "%.1f", label_w, nullptr)) {
        m_params.spacing = double(spacing);
        m_pick_dirty     = true;
    }

    float width = float(m_params.width);
    if (gizmo_row_slider("##width", m_desc.at("width").utf8_str().data(), &width,
                         float(WIDTH_MIN), float(WIDTH_MAX), "%.1f", label_w,
                         _u8L("Hole width, in mm. Keep it smaller than the spacing or the walls between the holes disappear.").c_str())) {
        m_params.width = double(width);
        m_pick_dirty   = true;
    }

    float margin = float(m_params.margin);
    if (gizmo_row_slider("##boundary", m_desc.at("boundary").utf8_str().data(), &margin,
                         float(MARGIN_MIN), float(MARGIN_MAX), "%.1f", label_w,
                         _u8L("How far the pattern keeps away from the edge of the picked face, in mm.").c_str())) {
        m_params.margin = double(margin);
        m_pick_dirty    = true;
    }

    if (m_params.kind == PerforationKind::Lines || m_params.kind == PerforationKind::Grid) {
        float angle = float(m_params.angle_deg);
        if (gizmo_row_slider("##angle", m_desc.at("angle").utf8_str().data(), &angle,
                             float(ANGLE_MIN), float(ANGLE_MAX), "%.0f", label_w, nullptr)) {
            m_params.angle_deg = double(angle);
            m_pick_dirty       = true;
        }
    }

    // [ORCAPORT:PP-6] The optional second face the pattern stops on. The caption sits in the same
    // label column as the rows above and the control is a plain button: there either is a target face
    // or there is not, so a slider would invent a range the geometry does not have. Drawn only once
    // there is a face to measure from, so the panel never offers a control that cannot work.
    if (m_pick.valid) {
        const float col = ImGui::GetCursorPosX() + label_w;
        ImGui::AlignTextToFramePadding();
        ImGui::PushStyleColor(ImGuiCol_Text, gizmo_col(GizmoCol::TextDim));
        ImGui::TextUnformatted(m_desc.at("to").utf8_str().data());
        ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            ImGui::SetTooltip("%s", m_desc.at("to_tip").utf8_str().data());
        ImGui::SameLine(col);
        // The label is copied: .utf8_str() hands out a temporary buffer that only lives to the end of
        // the statement, and this one is used in the next one.
        const std::string label =
            (m_pick_target ? m_desc.at("to_cancel") : m_target.valid ? m_desc.at("to_clear") : m_desc.at("to_pick"))
                .utf8_str()
                .data();
        if (ImGui::Button(label.c_str())) {
            if (m_pick_target) {
                m_pick_target = false;   // armed: the same button puts it back
                m_target_why  = TargetWhy::None;
            } else if (m_target.valid) {
                clear_target();
            } else {
                m_pick_target = true;
                m_target_why  = TargetWhy::None;
            }
            m_parent.set_as_dirty();
        }
    }

    // [ORCAPORT:PP-6] Invert is Grid's own behaviour and is forced on in the geometry (see
    // make_perforation_pattern), so the row is not offered for it: the switch could only lie.
    if (m_params.kind != PerforationKind::Grid) {
        bool invert = m_params.invert;
        if (gizmo_row_switch("##invert", m_desc.at("invert").utf8_str().data(), &invert,
                             _u8L("Cuts raised ridges instead of holes.").c_str())) {
            m_params.invert = invert;
            m_pick_dirty    = true;
        }
    }

    ImGui::Separator();

    // [ORCAPORT:PP-2] The primary action, right-aligned and in the accent colour, on its own line. The
    // pick is kept, so pressing it again cuts another one of the same pattern.
    if (gizmo_primary_button(m_desc.at("perforate").utf8_str().data()))
        create_from_pick();

    if (! m_pick.valid) {
        gizmo_hint(m_desc.at("click_hint").utf8_str().data());
    } else {
        gizmo_hint(m_desc.at("drop_hint").utf8_str().data());
        const wxString applied = wxString::Format("%s: %d", m_desc.at("applied").c_str(), gizmo_applied_count());
        m_imgui->text(applied);
    }
    // [ORCAPORT:PP-6] With a To target the depth does not follow the probe, so the hint that says it
    // does is replaced rather than left standing.
    if (m_target.valid)
        gizmo_hint(GUI::format(_L("The pattern stops on the picked face: %1% mm deep."), m_target.distance).c_str());
    else
        gizmo_hint(m_desc.at("depth_hint").utf8_str().data());

    // [ORCAPORT:PP-6] Why a target pick was refused. The same meaning reaches MCP as plain text
    // (gizmo_target_refusal()), so this enum is the only place that has to know both.
    switch (m_target_why) {
    case TargetWhy::None:        break;
    case TargetWhy::NoFace:      gizmo_hint(m_desc.at("to_noface").utf8_str().data(),   GizmoCol::Warn); break;
    case TargetWhy::NotParallel: gizmo_hint(m_desc.at("to_parallel").utf8_str().data(), GizmoCol::Warn); break;
    case TargetWhy::NotBehind:   gizmo_hint(m_desc.at("to_behind").utf8_str().data(),   GizmoCol::Warn); break;
    }

    // Advisory only: the geometry uses the width exactly as set. Amber is reserved for "something
    // is off" and nothing else in this panel uses it (section 28). Both notices describe the face the
    // pattern would actually be cut into, so they follow the pick once there is one.
    const bool show_reduced    = (m_params.spacing - m_params.width) < PERFORATION_MIN_WALL - 1e-6;
    const bool show_horizontal = m_pick.valid ? m_pick.horizontal : m_hover_horizontal;
    if (show_reduced)
        gizmo_hint(m_desc.at("reduced").utf8_str().data(), GizmoCol::Warn);
    if (show_horizontal)
        gizmo_hint(m_desc.at("horizontal").utf8_str().data(), GizmoCol::Warn);

    // [ORCAPORT:PP-6] Every row above marks the proposal dirty on the frame the value changes, and
    // on_render() rebuilds it on the next one, so the yellow pattern tracks the sliders live. Nothing is
    // deferred to the release of the item any more, and the walls-too-thin notice below reads the
    // parameters directly, so it was always live.

    // A changed value (or pattern) is no longer what the last Cut applied, so the proposal comes back.
    if (m_params.kind != before.kind || m_params.invert != before.invert || m_params.spacing != before.spacing ||
        m_params.width != before.width || m_params.margin != before.margin || m_params.angle_deg != before.angle_deg)
        m_show_proposal = true;
}

} // namespace GUI
} // namespace Slic3r
