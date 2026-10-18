#include <catch2/catch_all.hpp>

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TabShapes.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>
#include <limits>
#include <utility>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// A 14 mm tab, 6 mm behind the passage and 10 mm in front of it, tapering 12 degrees each side, with
// an M3 clearance hole. These are the panel's defaults; every case below varies one of them.
TabSpec tab_12deg()
{
    TabSpec s;
    s.root_w    = 14.;
    s.root_len  = 6.;
    s.tip_len   = 10.;
    s.taper_deg = 12.;
    s.corner_r  = 0.;
    s.passage_r = 1.7;
    s.min_wall  = 2.;
    s.thickness = 3.;
    s.bite      = 0.6;
    return s;
}

// Scaled contour coordinates back to mm. Exact to the scaling factor's half step.
Vec2d unscale_p(const Point &p) { return Vec2d(unscale_(double(p.x())), unscale_(double(p.y()))); }

double area_mm2(const Polygon &p) { return p.area() * SCALING_FACTOR * SCALING_FACTOR; }

// The two ends of the contour's longest edge above the axis: the tab's upper flank, from the root
// corner (first) to where it meets the tip (second). Measured on the shipped contour rather than
// recomputed, so it is the shape that is checked and not the formula that made it.
std::pair<Vec2d, Vec2d> upper_flank(const Polygon &p)
{
    std::pair<Vec2d, Vec2d> best;
    double                  best_len = 0.;
    for (size_t i = 0; i < p.points.size(); ++i) {
        const Vec2d a = unscale_p(p.points[i]);
        const Vec2d b = unscale_p(p.points[(i + 1) % p.points.size()]);
        if (a.y() <= 0. || b.y() <= 0.)
            continue;
        const double len = (b - a).norm();
        if (len > best_len) {
            best_len = len;
            best     = (a.x() < b.x()) ? std::make_pair(a, b) : std::make_pair(b, a);
        }
    }
    return best;
}

// Distance along the ray to the first surface, or NaN when it passes clean through.
double first_hit(const indexed_triangle_set &its, const Vec3d &origin, const Vec3d &dir)
{
    const AABBMesh mesh(its);
    double         best = 1e30;
    for (const AABBMesh::hit_result &hit : mesh.query_ray_hits(origin, dir))
        if (hit.is_hit())
            best = std::min(best, hit.distance());
    return best > 1e29 ? std::numeric_limits<double>::quiet_NaN() : best;
}

// z of the first surface below (x, y), or NaN when the ray goes through.
double surface_z(const indexed_triangle_set &its, double x, double y, double from_z = 30.)
{
    const double d = first_hit(its, Vec3d(x, y, from_z), -Vec3d::UnitZ());
    return std::isnan(d) ? d : from_z - d;
}

// A 20x20x10 cube with the tab's frame on its top face at (10, 10, 10). The frame is axis aligned,
// so the tab's +x is the world +x. Returns union(cube, plate, boss) - screw, or an empty mesh.
indexed_triangle_set bake_tab_on_cube(const TabSpec &spec, const TabScrew &screw)
{
    const Transform3d place(Eigen::Translation3d(10., 10., 10.));
    TriangleMesh      part(its_make_cube(20., 20., 10.));

    auto add_solid = [&](const indexed_triangle_set &its) {
        if (its.empty())
            return;
        TriangleMesh add;
        add.its = its;
        its_transform(add.its, place);
        MeshBoolean::cgal::plus(part, add);
    };
    add_solid(make_tab_plate(spec));
    add_solid(make_tab_boss(spec));

    const indexed_triangle_set cut = make_tab_screw(spec, screw);
    if (cut.empty())
        return {};
    TriangleMesh tool;
    tool.its = cut;
    its_transform(tool.its, place);
    MeshBoolean::cgal::minus(part, tool);
    its_merge_vertices(part.its);
    return part.its;
}

} // namespace

TEST_CASE("A tapered tab runs its flanks at the angle it was given", "[TabShapes]")
{
    const TabOutline o = tab_outline(tab_12deg());
    REQUIRE(o.poly.size() == 1);
    CHECK_FALSE(o.clamped);

    // The taper is an input, not a consequence: the flank the shape got is the one asked for, and the
    // tip radius is what answers to it. R solves the tangency asserted below, which is what makes the
    // taper exact instead of approximate.
    const double ta = 12. * PI / 180.;
    const double R  = (7. * std::cos(ta) - 16. * std::sin(ta)) / (1. - std::sin(ta));
    CHECK_THAT(o.taper_deg, WithinAbs(12., 1e-6));
    CHECK_THAT(o.tip_w, WithinAbs(2. * R, 1e-6));

    const auto [p, t] = upper_flank(o.poly.front().contour);
    CHECK_THAT((p - Vec2d(-6., 7.)).norm(), WithinAbs(0., 1e-5)); // the root corner

    // The flank's own angle below the axis, straight off the shipped contour.
    CHECK_THAT(std::atan2(p.y() - t.y(), t.x() - p.x()) * 180. / PI, WithinAbs(12., 1e-4));

    // And it is tangent to the tip circle, so the outline has no crease where the two meet.
    const Vec2d c(10. - 0.5 * o.tip_w, 0.);
    CHECK_THAT((t - c).norm(), WithinAbs(0.5 * o.tip_w, 1e-5));
    CHECK_THAT((t - p).dot(t - c), WithinAbs(0., 1e-4));
}

TEST_CASE("A tab is as long as its root and tip lengths and as wide as its root", "[TabShapes]")
{
    const TabSpec s = tab_12deg();

    const BoundingBoxf3 bb = bounding_box(make_tab_plate(s));
    CHECK_THAT(bb.min.x(), WithinAbs(-s.root_len, 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(s.tip_len, 2e-2));        // the tip arc's outermost point
    CHECK_THAT(bb.max.y(), WithinAbs(0.5 * s.root_w, 1e-3));   // the root is the widest part
    CHECK_THAT(bb.min.y(), WithinAbs(-0.5 * s.root_w, 1e-3));
}

TEST_CASE("A stadium has parallel flanks and a tip as wide as its root", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.taper_deg = 0.; // the Stadium preset

    const TabOutline o = tab_outline(s);
    REQUIRE(o.poly.size() == 1);
    CHECK_FALSE(o.clamped);
    CHECK_THAT(o.taper_deg, WithinAbs(0., 1e-9));
    CHECK_THAT(o.tip_w, WithinAbs(s.root_w, 1e-6));

    // Parallel means the upper flank is horizontal at half the root width.
    const auto [p, t] = upper_flank(o.poly.front().contour);
    CHECK_THAT(p.y(), WithinAbs(0.5 * s.root_w, 1e-5));
    CHECK_THAT(t.y(), WithinAbs(0.5 * s.root_w, 1e-5));
}

TEST_CASE("A flat tip is cut where the tip length ends", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.tip     = TabTip::Flat; // the Rect / Rounded preset family

    const TabOutline o = tab_outline(s);
    REQUIRE(o.poly.size() == 1);
    CHECK_FALSE(o.clamped);
    CHECK_THAT(o.taper_deg, WithinAbs(12., 1e-9));

    // The straight end lands where the taper puts it.
    const double expected = s.root_w - 2. * (s.root_len + s.tip_len) * std::tan(12. * PI / 180.);
    CHECK_THAT(o.tip_w, WithinAbs(expected, 1e-6));

    // Two contour points sit on x = tip_len, and they are the tip edge's own ends.
    int ends = 0;
    for (const Point &q : o.poly.front().contour.points) {
        const Vec2d v = unscale_p(q);
        if (std::abs(v.x() - s.tip_len) < 1e-5) {
            ++ends;
            CHECK_THAT(std::abs(v.y()), WithinAbs(0.5 * o.tip_w, 1e-5));
        }
    }
    CHECK(ends == 2);
}

TEST_CASE("A slot opens the tip and is not cut as a hole", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.passage = TabPassage::Slot;

    const TabOutline o = tab_outline(s);
    REQUIRE(o.poly.size() == 1);
    CHECK(o.poly.front().holes.empty());                       // a fork is one simply connected shape
    CHECK_THAT(o.slot_w, WithinAbs(2. * s.passage_r, 1e-9));   // defaults to the screw clearance

    // A slot is the outline, so there is nothing for a negative volume to cut.
    TabScrew hole;
    hole.d     = 3.4;
    hole.depth = 10.;
    CHECK(make_tab_screw(s, hole).empty());

    // Along the slot's own axis, entering from beyond the tip, the first surface is its rounded
    // closed end, behind the passage. A drilled tab would have stopped the ray at its tip instead.
    const double fork_hit = first_hit(make_tab_plate(s), Vec3d(25., 0., 1.5), -Vec3d::UnitX());
    REQUIRE_FALSE(std::isnan(fork_hit));
    CHECK_THAT(25. - fork_hit, WithinAbs(-0.5 * o.slot_w, 0.05));

    TabSpec drilled = s;
    drilled.passage = TabPassage::Hole;
    const double wall_hit = first_hit(make_tab_plate(drilled), Vec3d(25., 0., 1.5), -Vec3d::UnitX());
    REQUIRE_FALSE(std::isnan(wall_hit));
    CHECK_THAT(25. - wall_hit, WithinAbs(s.tip_len, 0.02));
}

TEST_CASE("A slot wider than the tip can hold is narrowed", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.passage = TabPassage::Slot;
    s.slot_w  = 40.;

    const TabOutline o = tab_outline(s);
    REQUIRE(o.poly.size() == 1);
    CHECK(o.clamped);
    // A prong on each side of the slot, and material beside its closed end.
    CHECK(o.slot_w <= o.tip_w - 2. * s.min_wall + 1e-9);
    CHECK(o.slot_w <= s.root_w - 2. * s.min_wall + 1e-9);
}

TEST_CASE("A slot fits the tip it opens through", "[TabShapes]")
{
    // Where a slot stops fitting is the tip's own wall: past it the fork stops being a fork.
    for (const double w : {1., 2., 3., 4., 4.8, 6., 8., 10., 16.}) {
        TabSpec s = tab_12deg();
        s.passage = TabPassage::Slot;
        s.slot_w  = w;
        DYNAMIC_SECTION("slot width " << w)
        {
            const TabOutline o = tab_outline(s);
            REQUIRE(o.poly.size() == 1);
            CHECK(o.poly.front().holes.empty());
            CHECK(o.slot_w <= o.tip_w - 2. * s.min_wall + 1e-9);
        }
    }
}

TEST_CASE("A slot with no room for prongs is refused", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.passage   = TabPassage::Slot;
    s.taper_deg = 60.; // collapses the tip to the smallest radius a round tip may keep

    CHECK(tab_outline(s).poly.empty());
    CHECK(make_tab_plate(s).empty());
}

TEST_CASE("A tab too small to hold its screw is grown, not refused", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.root_w   = 4.; // narrower than the passage plus its walls
    s.tip_len  = 1.;
    s.root_len = 0.5;

    const TabOutline o = tab_outline(s);
    REQUIRE(o.poly.size() == 1);
    CHECK(o.clamped);

    // Every direction out of the passage keeps min_wall of material. The tip arc, where it meets the
    // flanks, is the closest the outline comes to the passage - which is why the tip radius answers
    // to this rule and not only to the taper.
    for (int i = 0; i < 16; ++i) {
        const double a = 2. * PI * double(i) / 16.;
        const double r = s.passage_r + s.min_wall - 0.02;
        DYNAMIC_SECTION("direction " << i)
        {
            CHECK(o.poly.front().contains(Point::new_scale(r * std::cos(a), r * std::sin(a)), false));
        }
    }

    // A spec that cannot make a tab at all still produces nothing.
    TabSpec none = s;
    none.root_w  = 0.;
    CHECK(tab_outline(none).poly.empty());
}

TEST_CASE("A plate never ends flush with the surface it sits on", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.bite    = 0.; // asking for no bite still has to bury the plate a little

    const BoundingBoxf3 bb = bounding_box(make_tab_plate(s));
    CHECK(bb.min.z() < -0.01);
    CHECK_THAT(bb.max.z(), WithinAbs(s.thickness, 1e-3));

    s.bite = 0.6;
    const indexed_triangle_set plate = make_tab_plate(s);
    CHECK(its_num_open_edges(plate) == 0);
    CHECK_THAT(bounding_box(plate).min.z(), WithinAbs(-0.6, 1e-3));

    // One closed solid whose volume is the outline's area times its height.
    CHECK_THAT(double(its_volume(plate)),
               WithinRel(area_mm2(tab_outline(s).poly.front().contour) * (s.thickness + s.bite), 1e-3));
}

TEST_CASE("A boss passes through the plate and reaches the height asked for", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.boss_d  = 8.;
    s.boss_h  = 4.;

    CHECK_THAT(tab_top(s), WithinAbs(3. + 4., 1e-9)); // the screw is entered from here

    const indexed_triangle_set boss = make_tab_boss(s);
    REQUIRE_FALSE(boss.empty());
    CHECK(its_num_open_edges(boss) == 0);

    const BoundingBoxf3 bb = bounding_box(boss);
    // It starts INSIDE the plate - below its top, above its own bottom - so the two solids overlap in
    // volume instead of sharing a face the boolean would have to resolve as coplanar.
    CHECK_THAT(bb.min.z(), WithinAbs(s.thickness - s.bite, 1e-4));
    CHECK(bb.min.z() < s.thickness);
    CHECK(bb.min.z() > -s.bite);
    CHECK_THAT(bb.max.z(), WithinAbs(tab_top(s), 1e-4));
    CHECK_THAT(bb.max.x(), WithinAbs(0.5 * s.boss_d, 1e-3));

    TabSpec none = s;
    none.boss_d  = 0.;
    CHECK(make_tab_boss(none).empty());
}

TEST_CASE("A screw cutter starts above the tab's top and keeps its depths below it", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.boss_d  = 8.;
    s.boss_h  = 4.;

    TabScrew screw;
    screw.d          = 3.4;
    screw.seat       = TabSeat::Counterbore;
    screw.seat_d     = 6.;
    screw.seat_depth = 3.;
    screw.depth      = 18.;

    const double               top = tab_top(s);
    const indexed_triangle_set cut = make_tab_screw(s, screw);
    REQUIRE_FALSE(cut.empty());

    const BoundingBoxf3 bb = bounding_box(cut);
    CHECK(bb.max.z() > top); // the entrance cap is out of the solid the seat opens through
    CHECK_THAT(bb.max.z(), WithinAbs(top + hole_cutter_entry_margin(screw.d), 1e-4));
    CHECK_THAT(bb.min.z(), WithinAbs(top - screw.depth, 1e-4)); // and the floor did not move with it
    CHECK_THAT(bb.max.x() + bb.min.x(), WithinAbs(0., 1e-6));   // coaxial with the passage
    CHECK_THAT(bb.max.x(), WithinAbs(0.5 * screw.seat_d, 1e-3));

    // Nothing to cut without a depth, and nothing at all for a slot.
    TabScrew none = screw;
    none.depth    = 0.;
    CHECK(make_tab_screw(s, none).empty());
    TabSpec slotted = s;
    slotted.passage = TabPassage::Slot;
    CHECK(make_tab_screw(slotted, screw).empty());
}

TEST_CASE("A tab baked onto a cube bores a clean through hole", "[TabShapes]")
{
    TabSpec s = tab_12deg();
    s.boss_d  = 8.;
    s.boss_h  = 4.;
    // The tip must not land on the cube's own +x face: a tangential contact is a degeneracy for the
    // boolean, not a tab.
    s.tip_len = 8.;

    TabScrew screw;
    screw.d          = 3.4;
    screw.seat       = TabSeat::Counterbore;
    screw.seat_d     = 6.;
    screw.seat_depth = 3.;
    screw.depth      = 18.; // from the tab's top (10 + 7) out through the cube's bottom

    const indexed_triangle_set baked = bake_tab_on_cube(s, screw);
    REQUIRE_FALSE(baked.empty());
    // A mesh boolean's output carries T-junctions wherever the kernel split a face, and
    // `its_num_open_edges` counts those as boundary edges - so it cannot say anything here. What the
    // bake has to get right is the shape, and the four probes below are that.
    CHECK(double(its_volume(baked)) > 20. * 20. * 10.);

    // Down the axis the hole is open the whole way: no hit at all, which is also the check that the
    // cutter's entrance cap left no skin across the recess.
    CHECK(std::isnan(surface_z(baked, 10., 10.)));
    // On the counterbore's shoulder the first surface is the seat's floor, 3 mm below the tab's top.
    CHECK_THAT(surface_z(baked, 12.4, 10.), WithinAbs(14., 1e-3));
    // Inside the boss but outside the seat, it is the boss's top.
    CHECK_THAT(surface_z(baked, 13.5, 10.), WithinAbs(17., 1e-3));
    // And beside the tab, the cube's own face.
    CHECK_THAT(surface_z(baked, 10., 2.), WithinAbs(10., 1e-3));
}

TEST_CASE("A tab on a wall lies parallel to the bed with its root on the face", "[TabShapes]")
{
    // The rule the first cut got wrong: a mounting tab on a wall is a FOOT that stands out sideways
    // and is screwed down from above, not a plate lying in the wall's own plane.
    const Vec3d wall_normal(-1., 0., 0.); // a wall at x = 0, its material at x > 0
    const Vec3d click(0., 0., 5.);
    const Vec3d centre(10., 10., 5.);
    constexpr double root_len = 6., bite = 0.6, thickness = 3.;

    const TabFrame f = tab_frame(wall_normal, click, centre, root_len, bite, 0.);
    CHECK(f.wall);

    // The plate's own normal is the bed's up, so the tab prints flat and the screw goes down.
    const Vec3d z = f.to_obj.linear().col(2);
    CHECK_THAT(z.z(), WithinAbs(1., 1e-9));
    // Straight out of the wall.
    const Vec3d x = f.to_obj.linear().col(0);
    CHECK_THAT(x.x(), WithinAbs(-1., 1e-9));

    // The screw sits root_len out from the face...
    CHECK_THAT(f.to_obj.translation().x(), WithinAbs(-(root_len - bite), 1e-9));
    // ... and the outline's root edge lands bite INSIDE it, which is the weld.
    CHECK(f.to_obj.translation().x() - root_len * x.x() > 0.);

    // Both as a solid: a slab whose z extent is its thickness (parallel to the bed), reaching the wall.
    TabSpec s = tab_12deg();
    s.root_len = root_len;
    s.tip_len  = 8.;
    s.thickness = thickness;
    s.bite      = bite;
    indexed_triangle_set plate = make_tab_plate(s);
    its_transform(plate, f.to_obj);
    const BoundingBoxf3 bb = bounding_box(plate);
    CHECK_THAT(bb.max.z() - bb.min.z(), WithinAbs(thickness + bite, 0.02));
    CHECK_THAT(bb.min.z(), WithinAbs(click.z() - bite, 0.02)); // its underside sits at the click
    CHECK(bb.max.x() > 0.);                                    // the root's weld reaches into the wall
    CHECK(bb.min.x() < -(root_len + 6.));                      // and the tab stands out of it
    CHECK(bb.max.y() > bb.max.x() + 4.);                       // the width runs along the wall
}

TEST_CASE("A tab on a face within 25 degrees of horizontal lies in that face", "[TabShapes]")
{
    // A bottom face: the plate lies in the face's plane, which is horizontal anyway - a foot under the
    // part, welded by the bite and walking out from under it.
    const Vec3d click(10., 10., 5.);
    const TabFrame f = tab_frame(-Vec3d::UnitZ(), click, Vec3d(10., 10., 5.), 6., 0.6, 0.);
    CHECK_FALSE(f.wall);
    CHECK_THAT(f.to_obj.linear().col(2).z(), WithinAbs(-1., 1e-9)); // the face's own normal
    CHECK_THAT(f.to_obj.translation().z(), WithinAbs(click.z() + 0.6, 1e-9)); // pushed into the material

    // 25 degrees off horizontal is still the plate's own plane; 45 is a wall.
    CHECK_FALSE(tab_frame(Vec3d(0.42, 0., -0.91), click, click, 6., 0.6, 0.).wall);
    CHECK(tab_frame(Vec3d(-0.71, 0., -0.71), click, click, 6., 0.6, 0.).wall);
}

TEST_CASE("A wall tab's angle turns it about the bed, not the wall", "[TabShapes]")
{
    const Vec3d wall_normal(-1., 0., 0.);
    const Vec3d click(0., 0., 5.);

    const TabFrame a0 = tab_frame(wall_normal, click, Vec3d(10., 10., 5.), 6., 0.6, 0.);
    const TabFrame a90 = tab_frame(wall_normal, click, Vec3d(10., 10., 5.), 6., 0.6, 90.);

    // 90 degrees about the plate's own normal takes it along the wall, still parallel to the bed.
    CHECK_THAT(std::abs(a90.to_obj.linear().col(0).y()), WithinAbs(1., 1e-9));
    CHECK_THAT(a90.to_obj.linear().col(2).z(), WithinAbs(1., 1e-9));
    CHECK_THAT(a90.to_obj.linear().col(0).dot(a0.to_obj.linear().col(0)), WithinAbs(0., 1e-9));
}

TEST_CASE("A probe measures what is under a footprint", "[TabShapes]")
{
    const TabSpec s = tab_12deg();

    SECTION("a flat face is fully covered and does not fall away")
    {
        const indexed_triangle_set cube = its_make_cube(20., 20., 20.);
        const Transform3d         frame(Eigen::Translation3d(10., 10., 20.));
        const TabSurfaceProbe     p = tab_probe_surface(cube, frame, tab_outline(s).poly, 3.);
        CHECK_THAT(p.coverage, WithinAbs(1., 1e-9));
        CHECK_THAT(p.fall_away, WithinAbs(0., 1e-9));
    }

    SECTION("a tab hanging over an edge is only partly over material")
    {
        const indexed_triangle_set cube = its_make_cube(20., 20., 20.);
        const Transform3d         frame(Eigen::Translation3d(2., 10., 20.));
        const TabSurfaceProbe     p = tab_probe_surface(cube, frame, tab_outline(s).poly, 3.);
        CHECK(p.coverage > 0.1);
        CHECK(p.coverage < 0.9);
    }

    SECTION("a curved face falls away from the tab's plane")
    {
        const indexed_triangle_set cyl = its_make_cylinder(5., 10.);
        Transform3d                frame = Transform3d::Identity();
        frame.linear().col(0) = Vec3d(0., 1., 0.); // the tab's +x runs round the cylinder
        frame.linear().col(1) = Vec3d(0., 0., 1.); // and its +y along the axis
        frame.linear().col(2) = Vec3d(1., 0., 0.); // out of the surface
        frame.translation()   = Vec3d(5., 0., 5.);

        TabSpec small  = s;
        small.root_w   = 4.;
        small.root_len = 1.5;
        small.tip_len  = 3.;
        small.passage_r = 0.5; // a small tab: the clamps would otherwise grow it past the cylinder
        small.min_wall  = 0.5;

        const TabSurfaceProbe p = tab_probe_surface(cyl, frame, tab_outline(small).poly, 3.);
        CHECK_THAT(p.coverage, WithinAbs(1., 1e-9));
        CHECK(p.fall_away > 0.6); // 5 - sqrt(25 - 3^2) where the footprint ends
        CHECK(p.fall_away < 1.1);
    }
}
