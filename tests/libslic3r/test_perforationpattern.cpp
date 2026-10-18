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

constexpr double PP_PI = 3.14159265358979323846;

// Area of scaled ExPolygons, in mm^2.
double area_mm2(const ExPolygons &polys)
{
    double a = 0.;
    for (const ExPolygon &p : polys)
        a += p.area();
    return a * SCALING_FACTOR * SCALING_FACTOR;
}

} // namespace

TEST_CASE("A valid width keeps the holes discrete", "[PerforationPattern]")
{
    // With width comfortably below spacing the holes must stay separate. `Lines` is the sharpest
    // case: if the width reached the spacing the slots would fuse into one rectangle.
    const PerforationParams discrete[] = {
        {PerforationKind::Lines, 0., 6., 2., 2., false},
        {PerforationKind::Honeycomb, 0., 6., 2., 2., false},
        {PerforationKind::Circles, 0., 6., 2., 2., false},
    };
    for (const PerforationParams &p : discrete) {
        const ExPolygons pattern = make_perforation_pattern(rectangle_domain(), p);
        REQUIRE(pattern.size() > 1);
        for (const ExPolygon &e : pattern)
            REQUIRE(area_mm2(ExPolygons{e}) < 0.5 * area_mm2(rectangle_domain()));
    }

    // Grid is ALWAYS cut as grooves ([ORCAPORT:PP-6], see the dedicated case below), so what it
    // subtracts is the complement of the lattice - the boundary band plus the squares between the
    // crossings, several contours rather than one connected slab.
    const ExPolygons grid = make_perforation_pattern(
        rectangle_domain(), {PerforationKind::Grid, 0., 12., 2., 2., false});
    REQUIRE(!grid.empty());
    REQUIRE(grid.size() > 1);
}

TEST_CASE("Lines turn through the full quarter, 0 to 90 degrees", "[PerforationPattern]")
{
    // A single family (Lines) is the one pattern the 0..45 bound did not fit: 45 degrees is only its
    // diagonal, so vertical lines were unreachable. 90 must be the same family turned a quarter round -
    // the slots run down the face instead of across it. [ORCAPORT:PP-6]
    const ExPolygons face = rectangle_domain();

    const ExPolygons flat = make_perforation_pattern(face, {PerforationKind::Lines, 0., 10., 3., 0., false});
    const ExPolygons up   = make_perforation_pattern(face, {PerforationKind::Lines, 90., 10., 3., 0., false});

    REQUIRE(flat.size() > 1);
    REQUIRE(up.size() > 1);
    for (const ExPolygon &p : flat) {
        const BoundingBox b = get_extents(p);
        REQUIRE(b.size().x() > b.size().y());
    }
    for (const ExPolygon &p : up) {
        const BoundingBox b = get_extents(p);
        REQUIRE(b.size().y() > b.size().x());
    }
}

TEST_CASE("A grid is cut as grooves even when holes are asked for", "[PerforationPattern]")
{
    // [ORCAPORT:PP-6] A grid subtracted as two crossing families of HOLES leaves the material between
    // the crossings as squares that touch nothing - floating regions the slicer cannot print, which is
    // what the panel used to be able to ask for. The builder forces the complement (grooves), so no
    // caller can produce that, and the property that says it worked is that what is left standing is
    // ONE connected region.
    const ExPolygons domain = rectangle_domain();
    const ExPolygons inset  = offset_ex(domain, -scale_(2.));

    PerforationParams params;
    params.kind    = PerforationKind::Grid;
    params.spacing = 8.;
    params.width   = 3.;
    params.margin  = 2.;
    params.invert  = false;   // exactly what a caller used to be able to ask for

    const ExPolygons pattern = make_perforation_pattern(domain, params);
    REQUIRE(!pattern.empty());
    REQUIRE(diff_ex(inset, pattern).size() == 1);

    // The forced version is the same geometry the explicit grooves flag builds.
    params.invert = true;
    const ExPolygons grooves = make_perforation_pattern(domain, params);
    REQUIRE_THAT(area_mm2(pattern), WithinRel(area_mm2(grooves), 1e-6));
}

TEST_CASE("An angle the new pattern cannot express goes back to zero", "[PerforationPattern]")
{
    // [ORCAPORT:PP-6] The panel's rule, and the one the MCP tool applies: a single family may sit
    // anywhere in 0..90, a pair only in 0..45 because its own 90-degree cross covers the rest. An
    // angle the new pattern cannot hold used to stay in place, leaving the slider pinned at its
    // maximum while the geometry went on using the old value.
    REQUIRE(perforation_max_angle_deg(PerforationKind::Lines) == 90.);
    REQUIRE(perforation_max_angle_deg(PerforationKind::Grid) == 45.);

    // Lines -> Grid: 80 does not fit, 30 does, 45 is the boundary and stays.
    REQUIRE(perforation_angle_for_kind(PerforationKind::Grid, 80.) == 0.);
    REQUIRE(perforation_angle_for_kind(PerforationKind::Grid, 30.) == 30.);
    REQUIRE(perforation_angle_for_kind(PerforationKind::Grid, 45.) == 45.);
    // Grid -> Lines keeps every value Grid could set...
    REQUIRE(perforation_angle_for_kind(PerforationKind::Lines, 45.) == 45.);
    // ...and a Lines value beyond its own range does not survive either.
    REQUIRE(perforation_angle_for_kind(PerforationKind::Lines, 120.) == 0.);
    // The isotropic patterns ignore the angle; the bound is defined for them so callers need no
    // special case, and it is the pair bound.
    REQUIRE(perforation_max_angle_deg(PerforationKind::Honeycomb) == 45.);
    REQUIRE(perforation_max_angle_deg(PerforationKind::Circles) == 45.);
}

TEST_CASE("A pattern is symmetric about the centre of the face", "[PerforationPattern]")
{
    // [ORCAPORT:PP-6] The lattice is anchored at the centre of the inset face, not at a corner of its
    // bounding box. A corner phase is a function of where the bounding box starts, so on a face that is
    // not a whole number of pitches across (this one is 96 x 56 mm inset against a 6 mm pitch) one edge
    // is cut differently from the parallel one. Centre-anchored, a 180-degree turn about the centre
    // leaves the pattern unchanged, and that is the property that makes the two edges match.
    const ExPolygons  domain = rectangle_domain();
    const ExPolygons  inset  = offset_ex(domain, -scale_(2.));
    REQUIRE(area_mm2(inset) > 0.);
    // A uniform margin leaves the inset symmetric about the face centre, so anchoring the lattice on
    // the inset's centre puts it on the FACE centre - which is the point the user is looking at.
    const BoundingBox db = get_extents(domain);
    const BoundingBox ib = get_extents(inset);
    const Point face_centre(db.min.x() + (db.max.x() - db.min.x()) / 2,
                            db.min.y() + (db.max.y() - db.min.y()) / 2);
    const Point inset_centre(ib.min.x() + (ib.max.x() - ib.min.x()) / 2,
                             ib.min.y() + (ib.max.y() - ib.min.y()) / 2);
    REQUIRE(inset_centre == face_centre);

    const PerforationKind kinds[] = {PerforationKind::Lines, PerforationKind::Grid,
                                     PerforationKind::Honeycomb, PerforationKind::Circles};
    for (const PerforationKind kind : kinds) {
        // The in-plane angle only moves the two families; honeycomb and circles are isotropic.
        const bool   angular = (kind == PerforationKind::Lines || kind == PerforationKind::Grid);
        const double angles[] = {0., 30.};
        for (const double angle : angles) {
            if (angle != 0. && !angular)
                continue;
            for (const bool invert : {false, true}) {
                PerforationParams p;
                p.kind      = kind;
                p.angle_deg = angle;
                p.spacing   = 6.;
                p.width     = 2.;
                p.margin    = 2.;
                p.invert    = invert;

                const ExPolygons pattern = make_perforation_pattern(domain, p);
                REQUIRE(!pattern.empty());

                ExPolygons turned = pattern;
                for (ExPolygon &e : turned)
                    e.rotate(PP_PI, face_centre);

                // A phase that is not centred leaves whole features of residual here, so a tolerance of
                // a thousandth of the pattern is plenty - the rotation itself only rounds to the
                // scaled unit.
                const double area = area_mm2(pattern);
                REQUIRE(area_mm2(diff_ex(pattern, turned)) < 1e-3 * area);
                REQUIRE(area_mm2(diff_ex(turned, pattern)) < 1e-3 * area);
            }
        }
    }
}

TEST_CASE("The pattern uses the width as given, even when holes merge", "[PerforationPattern]")
{    // No silent shrink: width is not corrected. Two parallel slots at spacing 6 with width 6
    // merge into one continuous void - the documented outcome the UI warns about.
    const ExPolygons merged = make_perforation_pattern(
        rectangle_domain(), {PerforationKind::Lines, 0., 6., 6., 2., false});
    REQUIRE(!merged.empty());
    // Merged slots cover almost the whole inset face (the 1 mm margin is the only loss): this is
    // the "square" the user can create on purpose, not a corrected shape.
    const ExPolygons inset = offset_ex(rectangle_domain(), -scale_(2.));
    REQUIRE(area_mm2(merged) > 0.9 * area_mm2(inset));

    // The warning threshold is advisory and lives in the UI; the constant itself just marks it.
    REQUIRE(PERFORATION_MIN_WALL > 0.);
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

TEST_CASE("An extruded pattern prism is a closed solid", "[PerforationPattern]")
{
    // Mesh boolean hands the prism to mcut, which needs closed manifold solids. The builder used to
    // push three fresh vertices per cap triangle and its own per wall strip - a geometrically closed
    // but topologically open soup (measured: 360 open edges on a honeycomb), which mcut cannot resolve
    // reliably: it fails or crashes, and a result it made from one poisons the NEXT boolean. Every
    // pattern must weld into a solid with no open edges and a positive signed volume.
    const ExPolygons domain = rectangle_domain();

    const PerforationParams cases[] = {
        {PerforationKind::Lines, 0., 6., 2., 2., false},
        {PerforationKind::Grid, 0., 8., 2., 2., false},
        {PerforationKind::Honeycomb, 0., 8., 4., 2., false},
        {PerforationKind::Circles, 0., 7., 4., 3., false},
    };
    for (const PerforationParams &p : cases) {
        const ExPolygons pattern = make_perforation_pattern(domain, p);
        REQUIRE(!pattern.empty());

        const indexed_triangle_set prism = extrude_perforation_pattern(
            pattern, Vec3d::Zero(), Vec3d::UnitX(), Vec3d::UnitY(), Vec3d::UnitZ(), -1., 6.);

        REQUIRE(!prism.indices.empty());
        CHECK(its_num_open_edges(prism) == 0);
        CHECK(its_volume(prism) > 0.);
    }
}
