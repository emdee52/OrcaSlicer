// [ORCAPORT FILE] TabShapes - see TabShapes.hpp
#include "TabShapes.hpp"

#include "AABBMesh.hpp"
#include "ClipperUtils.hpp"
#include "HoleShapes.hpp"
#include "SurfaceExtrude.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

// Tessellation of the tip arc. Degrees / segment count.
constexpr double TAB_TIP_SEG_DEG = 6.;

// The smallest tip radius a round tip is allowed to collapse to. A pointed tip is not a tab.
constexpr double TAB_MIN_TIP_R = 0.25;

// Clamped, so a spec typed by hand or driven over MCP can never build a degenerate solid.
inline double plate_weld(const TabSpec &s)      { return std::max(0.05, s.weld); }
inline double plate_thickness(const TabSpec &s) { return std::max(0.4, s.thickness); }
inline bool   has_boss(const TabSpec &s)        { return s.boss_d > 0. && s.boss_h > 0.; }

inline double deg2rad(double d) { return d * PI / 180.; }
inline double rad2deg(double r) { return r * 180. / PI; }

// The flank's half width at `x` for a straight flank at `tan_a`.
inline double half_width_at(const TabSpec &s, double x, double tan_a)
{
    return 0.5 * s.root_w - (x + s.root_len) * tan_a;
}

// The radius of the tip circle tangent to both flanks, centred on the axis with its far side at
// `length` from the passage. Solving this is what makes `taper_deg` a real angle: for 0 it comes out
// at half the root width, i.e. a stadium, and a steeper taper gives a smaller tip.
inline double tip_radius(double half_root, double taper_deg, double length)
{
    const double sa = std::sin(deg2rad(taper_deg));
    const double ca = std::cos(deg2rad(taper_deg));
    return (half_root * ca - length * sa) / (1. - sa);
}

// Where the flank from `p` touches the circle (`c`, `r`): the two solutions, picked by which side
// of the circle they land on. Both tangent and radius conditions hold exactly, which is what the
// tests assert.
Vec2d tangent_point(const Vec2d &p, const Vec2d &c, double r, bool upper)
{
    const Vec2d  v = p - c;
    const double d = v.norm();
    if (d <= r + 1e-9)
        return c; // degenerate: the corner is on or inside the tip
    const double phi = std::acos(std::clamp(r / d, -1., 1.));
    const double a0  = std::atan2(v.y(), v.x());
    const Vec2d  t0(c.x() + r * std::cos(a0 + phi), c.y() + r * std::sin(a0 + phi));
    const Vec2d  t1(c.x() + r * std::cos(a0 - phi), c.y() + r * std::sin(a0 - phi));
    return (upper == (t0.y() > t1.y())) ? t0 : t1;
}

inline Point scaled(const Vec2d &v) { return Point::new_scale(v.x(), v.y()); }

// The open slot, in scaled coordinates: a half-disc of radius `half` about the passage and a band
// running out past `reach`. Subtracted from the outline, so the tab becomes a fork.
Polygon slot_polygon(double half, double reach)
{
    std::vector<Vec2d> pts;
    const int          n = 16;
    for (int i = 0; i <= n; ++i) { // the rounded closed end, +y round to -y
        const double a = 0.5 * PI + PI * double(i) / double(n);
        pts.emplace_back(half * std::cos(a), half * std::sin(a));
    }
    pts.emplace_back(reach, -half);
    pts.emplace_back(reach, half);

    Polygon p;
    p.points.reserve(pts.size());
    for (const Vec2d &v : pts)
        p.points.emplace_back(scaled(v));
    p.make_counter_clockwise();
    return p;
}

} // namespace

TabOutline tab_outline(const TabSpec &spec_in)
{
    TabOutline out;
    TabSpec    s = spec_in;

    const double min_wall = std::max(0., s.min_wall);
    const double pr       = std::max(0.05, s.passage_r);
    if (s.root_w <= 0.)
        return out;

    // --- the minimum-material rules. They only ever GROW the tab, and say so through `clamped`. ---
    // Material behind the passage: for a slot, behind the slot's rounded closed end. The requested
    // slot width is bounded here by the tab's own sides first, so an absurd width cannot make the
    // root chase it and take the tip down with it; the tip's own rule bounds it again below.
    const double slot_want = s.slot_w > 0. ? s.slot_w : 2. * pr;
    const double slot_side = std::max(0., s.root_w - 2. * min_wall);
    const double back      = s.passage == TabPassage::Slot ? std::max(pr, std::min(0.5 * slot_want, 0.5 * slot_side)) : pr;
    if (s.root_len < back + min_wall) {
        s.root_len = back + min_wall;
        out.clamped = true;
    }
    if (s.tip_len < pr + min_wall) {
        s.tip_len = pr + min_wall;
        out.clamped = true;
    }

    // 60 degrees is already a tab cut away to nothing; beyond it the minimum-material rule below
    // would answer a steeper flank by growing the root without bound.
    double a_deg = std::clamp(s.taper_deg, 0., 60.);
    double L     = s.root_len + s.tip_len;

    // A straight tip has to keep an edge to cut, so the taper is bounded by it.
    if (s.tip == TabTip::Flat) {
        const double a_max = rad2deg(std::atan(std::max(0.5 * s.root_w - 0.5, 0.05) / L));
        if (a_deg > a_max) {
            a_deg       = std::max(0., a_max);
            out.clamped = true;
        }
    }

    // The tip's own floor: not just a radius, the material min_wall all round the passage. Where the
    // flanks meet the tip arc is the outline's closest approach to the passage, so it is the tip
    // radius - not the flanks - that decides how thin the tab can get there.
    const double sin_a  = std::sin(deg2rad(a_deg));
    const double r_need = s.tip == TabTip::Round
                              ? std::max(TAB_MIN_TIP_R, (pr + min_wall - s.tip_len * sin_a) / (1. - sin_a))
                              : 0.;

    // Material beside the passage, measured against the flank LINE so it follows the taper.
    const double need_w = std::max(2. * (pr + min_wall) / std::cos(deg2rad(a_deg)), 2. * r_need);
    if (s.root_w < need_w) {
        s.root_w    = need_w;
        out.clamped = true;
    }

    const double half_root = 0.5 * s.root_w;
    double       R         = 0.; // round tip radius
    double       tip_half  = half_root;
    if (s.tip == TabTip::Round) {
        const double r_taper = tip_radius(half_root, a_deg, L);
        R                    = std::max(r_taper, r_need);
        if (R > half_root) { // not reachable after the width rule above; kept as a guard
            R           = half_root;
            out.clamped = true;
        }
        if (R > r_taper + 1e-9) // the tip's floor set the tip, not the taper that was asked for
            out.clamped = true;
        tip_half = R;
    } else if (s.tip == TabTip::Flat) {
        tip_half = std::max(0.5, half_width_at(s, s.tip_len, std::tan(deg2rad(a_deg))));
    }
    out.tip_w = 2. * tip_half;

    // The slot must leave a prong on each side and material beside its closed end.
    if (s.passage == TabPassage::Slot) {
        const double want  = slot_want;
        const double max_w = std::min(2. * (half_root - min_wall), out.tip_w - 2. * min_wall);
        if (max_w <= 0.2) // no room for a fork at all
            return out;
        if (want > max_w) {
            s.slot_w    = max_w;
            out.clamped = true;
        } else {
            s.slot_w = want;
        }
        out.slot_w = s.slot_w;
    }

    // --- the contour ------------------------------------------------------------------------------
    std::vector<Vec2d> pts;
    if (s.tip == TabTip::Flat) {
        out.taper_deg = a_deg;
        pts.emplace_back(-s.root_len, half_root);
        pts.emplace_back(s.tip_len, tip_half);
        pts.emplace_back(s.tip_len, -tip_half);
        pts.emplace_back(-s.root_len, -half_root);
    } else {
        const Vec2d c(s.tip_len - R, 0.);
        const Vec2d p_up(-s.root_len, half_root);
        const Vec2d p_dn(-s.root_len, -half_root);
        const Vec2d t_up = tangent_point(p_up, c, R, true);
        const Vec2d t_dn = tangent_point(p_dn, c, R, false);

        // The flank's realised angle. Equal to the taper asked for unless the minimum-material rules
        // had to move the tip, and reported because the panel shows it as the tab's taper.
        out.taper_deg = rad2deg(std::atan2(p_up.y() - t_up.y(), t_up.x() - p_up.x()));

        pts.push_back(p_up);
        pts.push_back(t_up); // the tangency itself is a vertex, or the flank cuts the corner there
        const double a_up = std::atan2(t_up.y() - c.y(), t_up.x() - c.x());
        const double a_dn = std::atan2(t_dn.y() - c.y(), t_dn.x() - c.x());
        const int    n    = std::max(6, int(std::ceil(rad2deg(std::abs(a_up - a_dn)) / TAB_TIP_SEG_DEG)));
        for (int i = 1; i < n; ++i) { // strictly between the two tangencies, through the outermost point
            const double a = a_up + (a_dn - a_up) * double(i) / double(n);
            pts.emplace_back(c.x() + R * std::cos(a), c.y() + R * std::sin(a));
        }
        pts.push_back(t_dn);
        pts.push_back(p_dn);
    }

    Polygon p;
    p.points.reserve(pts.size());
    for (const Vec2d &v : pts)
        p.points.emplace_back(scaled(v));
    if (p.points.size() < 3)
        return out;
    p.make_counter_clockwise();

    ExPolygons poly;
    poly.emplace_back(std::move(p));

    if (s.passage == TabPassage::Slot) {
        // Far enough out that the band always leaves the tip, never stops short of it.
        const double reach = s.tip_len + s.root_w + 10.;
        const ExPolygons cut = diff_ex(poly, Polygons{slot_polygon(0.5 * s.slot_w, reach)});
        if (cut.size() != 1 || ! cut.front().holes.empty()) { // a fork is one simply-connected shape
            out.poly.clear();
            out.tip_w = 0.;
            return out;
        }
        poly = cut;
    }

    if (poly.size() != 1 || poly.front().contour.points.size() < 3) {
        out.poly.clear();
        out.tip_w = 0.;
        return out;
    }
    out.poly = std::move(poly);
    return out;
}

double tab_top(const TabSpec &spec)
{
    return plate_thickness(spec) + (has_boss(spec) ? spec.boss_h : 0.);
}

indexed_triangle_set make_tab_plate(const TabSpec &spec)
{
    const TabOutline outline = tab_outline(spec);
    if (outline.poly.empty())
        return {};

    // From `bite` below the surface to `thickness` above it. Never flush with the surface: see the
    // header.
    const double weld = plate_weld(spec);
    return SurfaceExtrude::extrude_outline(outline.poly, 0., -weld, plate_thickness(spec) + weld);
}

indexed_triangle_set make_tab_boss(const TabSpec &spec)
{
    if (! has_boss(spec))
        return {};

    // The boss reaches `bite` below the PLATE'S TOP - not to the plate's own bottom, which would
    // make the two solids share the surface they both end on, and not to the surface, which is the
    // host's own face. It overlaps the plate in volume, which is what a union wants.
    const double weld = plate_weld(spec);
    const double base = plate_thickness(spec) - weld;
    return its_make_bore(spec.boss_d, spec.boss_h + weld, Vec3d::UnitZ(), Vec3d(0., 0., base));
}

indexed_triangle_set make_tab_screw(const TabSpec &spec, const TabScrew &screw)
{
    if (spec.passage == TabPassage::Slot) // the slot is part of the outline, so there is nothing to cut
        return {};
    if (screw.d <= 1e-3 || screw.depth <= 1e-3)
        return {};

    // Entered at the face the head sits on - the boss's top when there is one - with the cutter's
    // entrance cap a hair further out. See hole_cutter_entry_margin(): a cap landing in that face is
    // coplanar with it, and a mesh boolean answers that with a zero-thickness skin over the recess.
    const Vec3d  entry(0., 0., tab_top(spec));
    const double margin = hole_cutter_entry_margin(screw.d);
    const Vec3d  axis   = -Vec3d::UnitZ();

    if (screw.seat == TabSeat::Counterbore && screw.seat_d > screw.d)
        return its_make_counterbore(screw.d, screw.seat_d, screw.seat_depth, screw.depth, axis, entry, margin);
    if (screw.seat == TabSeat::Countersink && screw.seat_d > screw.d)
        return its_make_countersink(screw.d, screw.seat_d, screw.seat_angle, screw.depth, axis, entry, margin);

    // A plain bore has no margin parameter of its own, so its cap is moved out by hand and the same
    // distance comes off the depth.
    return its_make_bore(screw.d, screw.depth + margin, axis, Vec3d(0., 0., entry.z() + margin));
}

TabSurfaceProbe tab_probe_surface(const indexed_triangle_set &its, const Transform3d &frame_to_its,
                                  const ExPolygons &footprint, double max_drop, int nx, int ny)
{
    TabSurfaceProbe out;
    if (its.indices.empty() || footprint.empty() || max_drop <= 0. || nx < 2 || ny < 2)
        return out;
    if (footprint.front().contour.points.size() < 3)
        return out;

    const BoundingBox bb = get_extents(footprint);
    if (! bb.defined)
        return out;

    const Transform3d its_to_frame = frame_to_its.inverse();
    const AABBMesh    mesh(its);
    const double      x0 = unscale_(double(bb.min.x())), x1 = unscale_(double(bb.max.x()));
    const double   y0 = unscale_(double(bb.min.y())), y1 = unscale_(double(bb.max.y()));
    int    total = 0, covered = 0;
    double deepest = 0.;
    for (int iy = 0; iy < ny; ++iy) {
        const double v = y0 + (y1 - y0) * double(iy) / double(ny - 1);
        for (int ix = 0; ix < nx; ++ix) {
            const double u = x0 + (x1 - x0) * double(ix) / double(nx - 1);
            if (! footprint.front().contains(Point::new_scale(u, v), false))
                continue;
            ++total;

            // Straight down onto the plane, from well above it. The origin is carried into the mesh
            // by `frame_to_its`, the direction comes back to the frame through its inverse, and the
            // affine map keeps the ray parameter: a hit distance along the normalised mesh direction
            // is a frame height once it is multiplied by the frame's own z of that direction.
            const Vec3d o_frame(u, v, max_drop + 1.);
            const Vec3d d_its   = (frame_to_its.linear() * Vec3d(0., 0., -1.)).normalized();
            const Vec3d d_frame = its_to_frame.linear() * d_its;
            const AABBMesh::hit_result hit = mesh.query_ray_hit(frame_to_its * o_frame, d_its);
            if (! hit.is_hit())
                continue;

            const double z = o_frame.z() + hit.distance() * d_frame.z();
            if (z < -max_drop)
                continue; // the far side of the object, not the surface under the footprint
            ++covered;
            deepest = std::max(deepest, -z);
        }
    }

    out.coverage  = total > 0 ? double(covered) / double(total) : 0.;
    out.fall_away = deepest;
    return out;
}

// A face this close to horizontal keeps the plate in its own plane: cos(25 degrees). Steeper than
// that is a wall, and a wall gets a plate parallel to the bed - see the header.
static constexpr double TAB_HORIZONTAL_COS = 0.9;

TabFrame tab_frame(const Vec3d &face_normal, const Vec3d &click, const Vec3d &centre, double root_len,
                   double weld, double angle_deg)
{
    TabFrame out;

    Vec3d n = face_normal;
    if (! n.allFinite() || n.norm() < 1e-9)
        n = Vec3d::UnitZ();
    n.normalize();

    out.wall = std::abs(n.z()) < TAB_HORIZONTAL_COS;
    // The plate's own normal: the bed's up on a wall, the face's own normal on a face the plate can
    // already lie in.
    const Vec3d z = out.wall ? Vec3d::UnitZ() : n;

    // The outward direction at angle 0: straight out of a wall, and away from the object on a
    // horizontal face (so the tab hangs off the nearest edge rather than lying in the middle).
    Vec3d x0;
    if (out.wall) {
        x0 = Vec3d(n.x(), n.y(), 0.);
    } else {
        x0 = click - centre;
        x0 -= z * x0.dot(z);
        if (x0.norm() < 1e-6) {
            const Vec3d ref = std::abs(z.dot(Vec3d::UnitX())) < 0.9 ? Vec3d::UnitX() : Vec3d::UnitY();
            x0              = ref - z * ref.dot(z);
        }
    }
    if (! x0.allFinite() || x0.norm() < 1e-9)
        x0 = z.unitOrthogonal();
    x0.normalize();
    const Vec3d y0 = z.cross(x0).normalized();

    const double a = angle_deg * PI / 180.;
    const Vec3d  x = x0 * std::cos(a) + y0 * std::sin(a);
    const Vec3d  y = -x0 * std::sin(a) + y0 * std::cos(a);

    out.axis   = x;
    out.to_obj = Transform3d::Identity();
    out.to_obj.linear().col(0) = x;
    out.to_obj.linear().col(1) = y;
    out.to_obj.linear().col(2) = z;
    // The screw sits `root_len` out from the root of a wall (on a face the plate lies in, the click IS
    // the screw), and the whole plate is pushed `bite` INTO the face - which on a wall is sideways,
    // and is what welds the root edge to it.
    const Vec3d root_shift = out.wall ? Vec3d(x * root_len) : Vec3d::Zero();
    out.to_obj.translation() = click + root_shift - n * weld;
    return out;
}

} // namespace Slic3r
