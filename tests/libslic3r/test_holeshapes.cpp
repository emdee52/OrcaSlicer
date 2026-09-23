#include <catch2/catch_all.hpp>

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_utils.hpp"

#include <algorithm>
#include <cmath>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// Teardrop cross-section area for a circle radius r whose top cap is replaced by two lines
// meeting `angle` from vertical.
double teardrop_area(double r, double angle_deg)
{
    const double a     = angle_deg * PI / 180.;
    const double theta = PI - 2. * a; // central angle of the discarded circular segment
    return PI * r * r - 0.5 * r * r * (theta - std::sin(theta)) + r * r * std::pow(std::cos(a), 3) / std::sin(a);
}

double apex_height(double r, double angle_deg) { return r / std::sin(angle_deg * PI / 180.); }

// A 20x20x4 plate with a 6x6x2 pocket carved into the top face (open upward).
indexed_triangle_set plate_with_pocket()
{
    TriangleMesh plate(its_make_cube(20., 20., 4.));
    TriangleMesh cutter(its_make_cube(6., 6., 3.));
    cutter.translate(Vec3f(7.f, 7.f, 2.f)); // z in [2, 5], pokes above the plate top
    MeshBoolean::cgal::minus(plate, cutter);
    its_merge_vertices(plate.its);
    return plate.its;
}

// The facet whose normal is closest to `normal` and whose centroid is closest to `point`.
int facet_near(const indexed_triangle_set &its, const Vec3d &normal, const Vec3d &point)
{
    const std::vector<Vec3f> normals = its_face_normals(its);
    int                      best    = -1;
    double                   best_d  = 1e30;
    for (int f = 0; f < int(its.indices.size()); ++f) {
        if (Vec3d(normals[f](0), normals[f](1), normals[f](2)).dot(normal) < 0.99)
            continue;
        Vec3d c = Vec3d::Zero();
        for (int k = 0; k < 3; ++k) {
            const Vec3f &v = its.vertices[its.indices[f](k)];
            c += Vec3d(v(0), v(1), v(2));
        }
        c /= 3.;
        const double d = (c - point).norm();
        if (d < best_d) {
            best_d = d;
            best   = f;
        }
    }
    return best;
}

} // namespace

TEST_CASE("A teardrop prism has the expected size and orientation", "[HoleShapes]")
{
    constexpr double      r = 2., depth = 10., angle = 45.;
    const indexed_triangle_set teardrop = its_make_teardrop(r, depth, angle, 64);

    REQUIRE_FALSE(teardrop.empty());
    CHECK(its_volume(teardrop) > 0.f);

    const BoundingBoxf3 bb = bounding_box(teardrop);
    CHECK_THAT(bb.min.x(), WithinAbs(-r, 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(r, 1e-3));
    CHECK_THAT(bb.min.y(), WithinAbs(-r, 1e-3));
    CHECK_THAT(bb.max.y(), WithinAbs(apex_height(r, angle), 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(depth, 1e-3));

    CHECK_THAT(double(its_volume(teardrop)), WithinRel(teardrop_area(r, angle) * depth, 0.02));
}

TEST_CASE("The teardrop apex rises as the angle narrows", "[HoleShapes]")
{
    const double apex_60 = bounding_box(its_make_teardrop(2., 5., 60., 64)).max.y();
    const double apex_45 = bounding_box(its_make_teardrop(2., 5., 45., 64)).max.y();
    const double apex_30 = bounding_box(its_make_teardrop(2., 5., 30., 64)).max.y();

    CHECK(apex_45 > apex_60);
    CHECK(apex_30 > apex_45);
    CHECK_THAT(apex_60, WithinAbs(apex_height(2., 60.), 1e-3));
    CHECK_THAT(apex_30, WithinAbs(apex_height(2., 30.), 1e-3));
}

TEST_CASE("A teardrop placed on a horizontal hole points up", "[HoleShapes]")
{
    DetectedHole hole;
    hole.axis   = Vec3d::UnitX();
    hole.center = Vec3d::Zero();
    hole.radius = 2.;

    constexpr double depth = 6.;
    const indexed_triangle_set teardrop = its_make_teardrop_for_hole(hole, depth, 45., 64);
    REQUIRE_FALSE(teardrop.empty());

    const BoundingBoxf3 bb = bounding_box(teardrop);
    CHECK_THAT(bb.max.z(), WithinAbs(apex_height(2., 45.), 1e-3)); // apex above the axis
    CHECK_THAT(bb.min.x(), WithinAbs(-0.5 * depth, 1e-3));          // extruded along the axis
    CHECK_THAT(bb.max.x(), WithinAbs(0.5 * depth, 1e-3));
}

TEST_CASE("A teardrop on a vertical hole is refused", "[HoleShapes]")
{
    DetectedHole hole;
    hole.axis   = Vec3d::UnitZ();
    hole.center = Vec3d::Zero();
    hole.radius = 2.;
    CHECK(its_make_teardrop_for_hole(hole, 6., 45., 64).empty());

    hole.axis = -Vec3d::UnitZ();
    CHECK(its_make_teardrop_for_hole(hole, 6., 45., 64).empty());
}

TEST_CASE("Invalid teardrop parameters produce nothing", "[HoleShapes]")
{
    CHECK(its_make_teardrop(0., 10., 45., 64).empty());
    CHECK(its_make_teardrop(2., -1., 45., 64).empty());
    CHECK(its_make_teardrop(2., 10., 45., 4).empty());
}

TEST_CASE("A bore is placed at the entry and runs along the axis", "[HoleShapes]")
{
    const indexed_triangle_set bore = its_make_bore(4., 10., Vec3d::UnitZ(), Vec3d(1., 2., 3.));
    REQUIRE_FALSE(bore.empty());
    CHECK(its_volume(bore) > 0.f);

    const BoundingBoxf3 bb = bounding_box(bore);
    CHECK_THAT(bb.min.x(), WithinAbs(-1., 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(3., 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(3., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(13., 1e-3));
    CHECK_THAT(double(its_volume(bore)), WithinRel(PI * 4. * 10., 0.03));
}

TEST_CASE("A counterbore widens the entrance to the head diameter", "[HoleShapes]")
{
    const indexed_triangle_set cbore = its_make_counterbore(3.4, 6., 3., 10., Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(cbore.empty());
    CHECK(its_volume(cbore) > 0.f);

    const BoundingBoxf3 bb = bounding_box(cbore);
    CHECK_THAT(bb.min.x(), WithinAbs(-3., 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(3., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(10., 1e-3));
    CHECK(its_volume(cbore) > its_volume(its_make_bore(3.4, 10., Vec3d::UnitZ(), Vec3d::Zero())));
}

TEST_CASE("A countersink widens the entrance to the sink diameter", "[HoleShapes]")
{
    const indexed_triangle_set csink = its_make_countersink(3.4, 6.3, 90., 10., Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(csink.empty());
    CHECK(its_volume(csink) > 0.f);

    const BoundingBoxf3 bb = bounding_box(csink);
    CHECK_THAT(bb.max.x(), WithinAbs(3.15, 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(10., 1e-3));
}

TEST_CASE("A tube is an annulus with a bore", "[HoleShapes]")
{
    const indexed_triangle_set tube = its_make_tube(6., 4., 5., Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(tube.empty());
    CHECK(its_volume(tube) > 0.f);

    const BoundingBoxf3 bb = bounding_box(tube);
    CHECK_THAT(bb.min.x(), WithinAbs(-3., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(5., 1e-3));
    CHECK_THAT(double(its_volume(tube)), WithinRel(PI * (9. - 4.) * 5., 0.03));
}

TEST_CASE("A bore follows an arbitrary axis", "[HoleShapes]")
{
    const indexed_triangle_set bore = its_make_bore(4., 10., Vec3d::UnitX(), Vec3d::Zero());
    REQUIRE_FALSE(bore.empty());
    CHECK(its_volume(bore) > 0.f);

    const BoundingBoxf3 bb = bounding_box(bore);
    CHECK_THAT(bb.min.x(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(10., 1e-3));
    CHECK_THAT(bb.min.y(), WithinAbs(-2., 1e-3));
    CHECK_THAT(bb.max.y(), WithinAbs(2., 1e-3));
}

TEST_CASE("A nut pocket is a hexagon across the given flats", "[HoleShapes]")
{
    constexpr double      s = 5.5, h = 2.4, clearance = 3.4;
    const indexed_triangle_set nut = its_make_nut_pocket(s, h, clearance, h, Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(nut.empty());
    CHECK(its_volume(nut) > 0.f);

    const BoundingBoxf3 bb = bounding_box(nut);
    // Circumradius = across_flats / sqrt(3).
    CHECK_THAT(bb.max.x(), WithinAbs(s / std::sqrt(3.), 1e-3));
    CHECK_THAT(bb.min.x(), WithinAbs(-s / std::sqrt(3.), 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(h, 1e-3));
    // Merged closed meshes sum their volumes: hexagon + the coaxial bore.
    const double hex_vol  = std::sqrt(3.) * 0.5 * s * s * h;
    const double bore_vol = PI * (clearance * 0.5) * (clearance * 0.5) * h;
    CHECK_THAT(double(its_volume(nut)), WithinRel(hex_vol + bore_vol, 0.04));
}

TEST_CASE("A through-hole depth reaches the far wall", "[HoleShapes]")
{
    constexpr double           r = 3., h = 12.;
    const indexed_triangle_set cyl = its_make_cylinder(r, h, 2. * PI / 64.);

    // Entering the top cap travelling -Z, the far wall is the bottom cap at z = 0.
    const double through = hole_through_depth(cyl, Vec3d(0., 0., h), Vec3d(0., 0., -1.), 0.5);
    CHECK_THAT(through, WithinAbs(h + 0.5, 1e-3));

    // A ray leaving the mesh without a far wall returns 0 so the caller can fall back.
    CHECK_THAT(hole_through_depth(cyl, Vec3d(0., 0., 2. * h), Vec3d(0., 0., 1.), 0.5), WithinAbs(0., 1e-9));

    // An empty mesh has nothing to march through.
    CHECK_THAT(hole_through_depth(indexed_triangle_set{}, Vec3d::Zero(), Vec3d::UnitZ(), 0.5), WithinAbs(0., 1e-9));
}

TEST_CASE("A rim chamfer is a closed ring at the hole edge", "[HoleShapes]")
{
    constexpr double      r = 3., size = 1.;
    const indexed_triangle_set chamfer = its_make_rim_chamfer(r, size, Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(chamfer.empty());
    CHECK(its_num_open_edges(chamfer) == 0);
    CHECK(its_volume(chamfer) > 0.f);

    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.min.x(), WithinAbs(-(r + size), 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(r + size, 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(size, 1e-3));

    // Ring volume by Pappus: triangle of area size^2/2 at centroid radius r + size/3.
    CHECK_THAT(double(its_volume(chamfer)), WithinRel(PI * size * size * (r + size / 3.), 0.02));
}

TEST_CASE("A rim fillet is a closed ring smaller than the equivalent chamfer", "[HoleShapes]")
{
    constexpr double      r = 3., size = 1.;
    const indexed_triangle_set fillet = its_make_rim_fillet(r, size, Vec3d::UnitZ(), Vec3d::Zero());
    REQUIRE_FALSE(fillet.empty());
    CHECK(its_num_open_edges(fillet) == 0);
    CHECK(its_volume(fillet) > 0.f);

    const BoundingBoxf3 bb = bounding_box(fillet);
    CHECK_THAT(bb.min.x(), WithinAbs(-(r + size), 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(r + size, 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(size, 1e-3));

    // The lune is smaller than the chamfer triangle for the same size.
    const indexed_triangle_set chamfer = its_make_rim_chamfer(r, size, Vec3d::UnitZ(), Vec3d::Zero());
    CHECK(its_volume(fillet) < its_volume(chamfer));
}

TEST_CASE("A rim shape is placed on the hole axis", "[HoleShapes]")
{
    const Vec3d entry(5., 0., 0.);
    const indexed_triangle_set chamfer = its_make_rim_chamfer(2., 0.5, Vec3d::UnitX(), entry);
    REQUIRE_FALSE(chamfer.empty());

    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.min.x(), WithinAbs(entry.x(), 1e-3));         // grows into the material
    CHECK_THAT(bb.max.x(), WithinAbs(entry.x() + 0.5, 1e-3));
    CHECK_THAT(bb.min.y(), WithinAbs(-2.5, 1e-3));              // ring around the axis
    CHECK_THAT(bb.max.y(), WithinAbs(2.5, 1e-3));
}

TEST_CASE("Rim shapes reject non-positive inputs", "[HoleShapes]")
{
    CHECK(its_make_rim_chamfer(0., 1., Vec3d::UnitZ(), Vec3d::Zero()).empty());
    CHECK(its_make_rim_chamfer(2., 0., Vec3d::UnitZ(), Vec3d::Zero()).empty());
    CHECK(its_make_rim_fillet(2., -1., Vec3d::UnitZ(), Vec3d::Zero()).empty());
}

TEST_CASE("A cavity fill over a flat surface adds nothing", "[HoleShapes]")
{
    const indexed_triangle_set cube = its_make_cube(20., 20., 20.);
    const Vec3d                top(2., 2., 20.);
    const int                  facet = facet_near(cube, Vec3d::UnitZ(), top);
    REQUIRE(facet >= 0);
    CHECK(cavity_fill_local(cube, std::vector<Vec3d>{top}, std::vector<int>{facet}, 3.).empty());
}

TEST_CASE("A cavity fill over a plain wall adds nothing", "[HoleShapes]")
{
    const indexed_triangle_set plate = plate_with_pocket();
    const Vec3d                wall(0., 10., 2.);
    const int                  facet = facet_near(plate, -Vec3d::UnitX(), wall);
    REQUIRE(facet >= 0);
    CHECK(cavity_fill_local(plate, std::vector<Vec3d>{wall}, std::vector<int>{facet}, 3.).empty());
}

TEST_CASE("A cavity fill rejects bad input", "[HoleShapes]")
{
    const indexed_triangle_set plate = plate_with_pocket();
    const std::vector<Vec3d>   one{Vec3d(10., 10., 2.)};
    const std::vector<int>     facet{0};
    CHECK(cavity_fill_local(indexed_triangle_set{}, one, facet, 3.).empty());
    CHECK(cavity_fill_local(plate, one, std::vector<int>{-1}, 3.).empty());
    CHECK(cavity_fill_local(plate, one, facet, 0.).empty());
}

TEST_CASE("Every letter of a watermark is filled from its own seed", "[HoleShapes]")
{
    // The model the tool has to handle: a watermark engraved into a wall, in mm. Each letter is a
    // separate depression, and each has to fill from a single seed with no reference surface to set
    // and no re-setting as the wall turns away: the surface is fitted locally, facet by facet.
    const TriangleMesh         part = load_model("watermark.obj");
    const indexed_triangle_set its  = part.its;
    REQUIRE_FALSE(its.indices.empty());

    const std::vector<Vec3d> seeds{Vec3d(87.1, 133.5, 34.8), Vec3d(87.1, 115.7, 34.5),
                                   Vec3d(87.1, 125.0, 35.0)};
    for (const Vec3d &seed : seeds) {
        const int facet = facet_near(its, Vec3d::UnitX(), seed);
        REQUIRE(facet >= 0);

        const indexed_triangle_set fill = cavity_fill_local(its, std::vector<Vec3d>{seed}, std::vector<int>{facet}, 3.);
        DYNAMIC_SECTION("letter at " << seed.x() << "," << seed.y() << "," << seed.z()) {
            REQUIRE_FALSE(fill.empty());
            // Wound outward, so it slices as a positive volume.
            CHECK(its_volume(fill) > 0.f);
            // Flush: the plug is the letter volume between its floor and the wall, so it stays thin
            // in x and far below the volume of a brush-shaped block.
            const BoundingBoxf3 bb = bounding_box(fill);
            CHECK_THAT(bb.min.x(), WithinAbs(87.1, 1e-2));
            CHECK(bb.max.x() - bb.min.x() < 1.);
            CHECK(double(its_volume(fill)) < 10.);
        }
    }
}

TEST_CASE("A plain wall of a watermark is left alone", "[HoleShapes]")
{
    const TriangleMesh         part = load_model("watermark.obj");
    const indexed_triangle_set its  = part.its;
    REQUIRE_FALSE(its.indices.empty());

    // A flat facet of the same model, far from the engraving.
    const Vec3d wall(87.5, 156., 27.2);
    const int   facet = facet_near(its, Vec3d::UnitX(), wall);
    REQUIRE(facet >= 0);
    CHECK(cavity_fill_local(its, std::vector<Vec3d>{wall}, std::vector<int>{facet}, 3.).empty());
}

