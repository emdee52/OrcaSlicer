#include <catch2/catch_all.hpp>

#include "libslic3r/EdgeProfiles.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

// A convex box edge: the top face (outward +Z) meets the front face (outward -Y) at 90 degrees,
// material on the +Y / -Z side. The edge runs along +X from the origin.
const Vec3d edge_p0 = Vec3d(0., 0., 0.);
const Vec3d edge_p1 = Vec3d(10., 0., 0.);
const Vec3d face_top = Vec3d::UnitZ();
const Vec3d face_front = -Vec3d::UnitY();

} // namespace

TEST_CASE("An edge chamfer is a closed prism along the edge", "[EdgeProfiles]")
{
    constexpr double size = 1.;
    const indexed_triangle_set chamfer =
        make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, size);

    REQUIRE_FALSE(chamfer.empty());
    CHECK(its_num_open_edges(chamfer) == 0);
    CHECK(its_volume(chamfer) > 0.f);

    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.min.x(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(10., 1e-3));
    CHECK_THAT(bb.min.y(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.y(), WithinAbs(size, 1e-3));   // leg along the top face
    CHECK_THAT(bb.min.z(), WithinAbs(-size, 1e-3));  // leg along the front face
    CHECK_THAT(bb.max.z(), WithinAbs(0., 1e-3));

    const double area = 0.5 * size * size;
    CHECK_THAT(double(its_volume(chamfer)), WithinRel(area * 10., 0.01));
}

TEST_CASE("An edge fillet is a smaller closed prism than the same-size chamfer", "[EdgeProfiles]")
{
    constexpr double size = 1.;
    const indexed_triangle_set fillet =
        make_edge_fillet(edge_p0, edge_p1, face_top, face_front, size);
    const indexed_triangle_set chamfer =
        make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, size);

    REQUIRE_FALSE(fillet.empty());
    CHECK(its_num_open_edges(fillet) == 0);
    CHECK(its_volume(fillet) > 0.f);
    CHECK(its_volume(fillet) < its_volume(chamfer));

    const double lune_area = size * size * (1. - 0.25 * PI);
    CHECK_THAT(double(its_volume(fillet)), WithinRel(lune_area * 10., 0.02));

    const BoundingBoxf3 bb = bounding_box(fillet);
    CHECK_THAT(bb.max.y(), WithinAbs(size, 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(-size, 1e-3));
}

TEST_CASE("An edge shape is over-extended by the margin", "[EdgeProfiles]")
{
    const indexed_triangle_set chamfer =
        make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, 1., 0.5);

    REQUIRE_FALSE(chamfer.empty());
    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.min.x(), WithinAbs(-0.5, 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(10.5, 1e-3));
    CHECK_THAT(double(its_volume(chamfer)), WithinRel(0.5 * 11., 0.01));
}

TEST_CASE("Edge dress rejects degenerate input", "[EdgeProfiles]")
{
    CHECK(make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, 0.).empty());
    CHECK(make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, -1.).empty());
    CHECK(make_edge_chamfer(edge_p0, edge_p1, face_top, face_front, 1., -1.).empty());
    CHECK(make_edge_chamfer(edge_p0, edge_p0, face_top, face_front, 1.).empty()); // zero length
    CHECK(make_edge_chamfer(edge_p0, edge_p1, face_top, face_top, 1.).empty());   // flat crease
    CHECK(make_edge_fillet(edge_p0, edge_p1, face_top, face_front, 0.).empty());
}
