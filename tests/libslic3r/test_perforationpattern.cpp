#include <catch2/catch_all.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/PerforationPattern.hpp"
#include "libslic3r/TriangleMesh.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinRel;

namespace {

// A 100 x 60 mm rectangle in scaled coordinates.
ExPolygons rectangle_domain()
{
    ExPolygon r;
    r.contour.points = {Point::new_scale(0., 0.), Point::new_scale(100., 0.),
                        Point::new_scale(100., 60.), Point::new_scale(0., 60.)};
    return ExPolygons{r};
}

// Area of scaled ExPolygons, in mm^2.
double area_mm2(const ExPolygons &polys)
{
    double a = 0.;
    for (const ExPolygon &p : polys)
        a += p.area();
    return a * SCALING_FACTOR * SCALING_FACTOR;
}

} // namespace

TEST_CASE("A wall is always kept between holes whatever the width", "[PerforationPattern]")
{
    // Width is clamped so a wall of at least PERFORATION_MIN_WALL survives, even when the
    // caller asks for width >= spacing. Without the clamp the holes merge into a single void.
    REQUIRE(perforation_effective_width(6., 3.) == Catch::Approx(3.));
    REQUIRE(perforation_effective_width(6., 10.) == Catch::Approx(6. - PERFORATION_MIN_WALL));
    REQUIRE(perforation_effective_width(2., 10.) <= 2. - PERFORATION_MIN_WALL + 1e-9);

    // With width >> spacing the clamp must keep the holes discrete, not merge them into one
    // void. `Lines` is the sharpest test: an unclamped width would fuse every slot into a
    // single continuous rectangle (one contour), whereas the clamped comb has many.
    // Discrete-hole patterns (Lines / Honeycomb / Circles) must stay as many separate holes.
    const PerforationParams discrete[] = {
        {PerforationKind::Lines, 0., 6., 100., 2., false},
        {PerforationKind::Honeycomb, 0., 6., 100., 2., false},
        {PerforationKind::Circles, 0., 6., 100., 2., false},
    };
    for (const PerforationParams &p : discrete) {
        const ExPolygons pattern = make_perforation_pattern(rectangle_domain(), p);
        REQUIRE(pattern.size() > 1);
        for (const ExPolygon &e : pattern)
            REQUIRE(area_mm2(ExPolygons{e}) < 0.5 * area_mm2(rectangle_domain()));
    }

    // Grid is a single connected lattice by construction (the two families cross), so the
    // check is that it is a perforated lattice - it has holes - rather than one solid slab.
    // Use a spacing where the bars (width clamped to 5mm) leave clear open cells.
    const ExPolygons grid = make_perforation_pattern(
        rectangle_domain(), {PerforationKind::Grid, 0., 12., 100., 2., false});
    REQUIRE(!grid.empty());
    REQUIRE(grid.front().holes.size() > 1);
}

TEST_CASE("A honeycomb pattern removes holes from a rectangular face", "[PerforationPattern]")
{
    const ExPolygons domain = rectangle_domain();

    PerforationParams params;
    params.kind    = PerforationKind::Honeycomb;
    params.spacing = 8.;
    params.width   = 4.;
    params.margin  = 2.;

    const ExPolygons pattern = make_perforation_pattern(domain, params);
    REQUIRE(!pattern.empty());

    // Holes leave material behind: the pattern covers less than the inset face.
    REQUIRE(area_mm2(pattern) < 100. * 60.);
    REQUIRE(area_mm2(pattern) > 0.);

    // Nothing sticks out past the boundary inset.
    const BoundingBox inset = get_extents(offset_ex(domain, -scale_(2.)));
    bool              inside = true;
    for (const ExPolygon &p : pattern) {
        for (const Point &pt : p.contour.points)
            if (pt.x() < inset.min.x() - 1 || pt.x() > inset.max.x() + 1 ||
                pt.y() < inset.min.y() - 1 || pt.y() > inset.max.y() + 1)
                inside = false;
        for (const Polygon &h : p.holes)
            for (const Point &pt : h.points)
                if (pt.x() < inset.min.x() - 1 || pt.x() > inset.max.x() + 1 ||
                    pt.y() < inset.min.y() - 1 || pt.y() > inset.max.y() + 1)
                    inside = false;
    }
    REQUIRE(inside);
}

TEST_CASE("Inverting a pattern complements it inside the inset face", "[PerforationPattern]")
{
    const ExPolygons domain = rectangle_domain();

    PerforationParams params;
    params.kind    = PerforationKind::Honeycomb;
    params.spacing = 8.;
    params.width   = 4.;
    params.margin  = 2.;
    params.invert  = false;

    const ExPolygons holes  = make_perforation_pattern(domain, params);
    params.invert           = true;
    const ExPolygons grooves = make_perforation_pattern(domain, params);

    const ExPolygons inset = offset_ex(domain, -scale_(2.));
    const double    inset_area = area_mm2(inset);

    REQUIRE(inset_area > 0.);
    REQUIRE_THAT(area_mm2(holes) + area_mm2(grooves), WithinRel(inset_area, 0.01));
}

TEST_CASE("An extruded pattern prism has the volume of its cross-section times its height", "[PerforationPattern]")
{
    const ExPolygons domain = rectangle_domain();

    PerforationParams params;
    params.kind    = PerforationKind::Lines;
    params.angle_deg = 0.;
    params.spacing = 10.;
    params.width   = 3.;
    params.margin  = 0.;

    const ExPolygons pattern = make_perforation_pattern(domain, params);
    REQUIRE(!pattern.empty());

    const double height = 5.;
    const indexed_triangle_set prism = extrude_perforation_pattern(
        pattern, Vec3d::Zero(), Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ(), 0., height);

    REQUIRE(!prism.indices.empty());
    REQUIRE_THAT(its_volume(prism), WithinRel(area_mm2(pattern) * height, 0.02));
}
