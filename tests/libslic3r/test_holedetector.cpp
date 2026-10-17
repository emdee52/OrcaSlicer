#include <catch2/catch_all.hpp>

#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/HoleShapes.hpp" // its_make_teardrop_prism: the teardrop tunnel fixture
#include "libslic3r/MeshBoolean.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <algorithm>
#include <cmath>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

// An open cylindrical wall (no caps): a through-hole wall in isolation. The solid sits outside
// the wall, so the outward normals point toward the axis (wound inward).
indexed_triangle_set make_cylinder_wall(double r, double h, int n)
{
    indexed_triangle_set its;
    std::vector<int>     bottom(n), top(n);
    for (int i = 0; i < n; ++i) {
        const double a = 2. * PI * i / n;
        const float  x = float(r * std::cos(a));
        const float  y = float(r * std::sin(a));
        bottom[i] = int(its.vertices.size());
        its.vertices.emplace_back(x, y, 0.f);
        top[i] = int(its.vertices.size());
        its.vertices.emplace_back(x, y, float(h));
    }
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        its.indices.emplace_back(bottom[i], top[j], bottom[j]);
        its.indices.emplace_back(bottom[i], top[i], top[j]);
    }
    return its;
}

void rotate_x(indexed_triangle_set &its, double deg)
{
    const double a = deg * PI / 180.;
    const double c = std::cos(a), s = std::sin(a);
    for (stl_vertex &v : its.vertices) {
        const double y = v(1), z = v(2);
        v(1) = float(c * y - s * z);
        v(2) = float(s * y + c * z);
    }
}

const DetectedHole *hole_with_radius(const std::vector<DetectedHole> &holes, double radius)
{
    for (const DetectedHole &h : holes)
        if (std::abs(h.radius - radius) < 0.05)
            return &h;
    return nullptr;
}

double radial_distance(const DetectedHole &h, const Vec3d &p)
{
    const Vec3d d = p - h.center;
    return (d - h.axis * d.dot(h.axis)).norm();
}

} // namespace

TEST_CASE("A cylindrical wall is detected with its radius and axis", "[HoleDetector]")
{
    indexed_triangle_set its = make_cylinder_wall(2.5, 10., 64);
    const auto          holes = detect_holes(its);

    const DetectedHole *h = hole_with_radius(holes, 2.5);
    REQUIRE(h != nullptr);
    CHECK_THAT(std::abs(h->axis.dot(Vec3d::UnitZ())), WithinAbs(1., 1e-3));
    CHECK_THAT(h->depth, WithinAbs(10., 1e-3));
    CHECK(h->through);
    CHECK_THAT(h->center.z(), WithinAbs(5., 1e-3));
    CHECK(h->confidence > 0.9);
}

TEST_CASE("A horizontal cylindrical wall keeps its axis through a rotation", "[HoleDetector]")
{
    indexed_triangle_set its = make_cylinder_wall(2.5, 10., 64);
    rotate_x(its, 90.); // axis Z -> Y
    const auto holes = detect_holes(its);

    REQUIRE(holes.size() >= 1);
    const DetectedHole &h = holes.front();
    CHECK_THAT(std::abs(h.axis.dot(Vec3d::UnitY())), WithinAbs(1., 1e-3));
    CHECK_THAT(h.radius, WithinAbs(2.5, 0.05));
}

TEST_CASE("Two separated cylindrical walls are detected as two holes", "[HoleDetector]")
{
    indexed_triangle_set a = make_cylinder_wall(1.5, 8., 64);
    indexed_triangle_set b = make_cylinder_wall(1.5, 8., 64);
    for (stl_vertex &v : b.vertices)
        v(0) += 20.f;
    const indexed_triangle_set merged = [&] {
        indexed_triangle_set m = a;
        its_merge(m, b);
        return m;
    }();

    const auto holes = detect_holes(merged);
    int        found = 0;
    for (const DetectedHole &h : holes)
        if (std::abs(h.radius - 1.5) < 0.05)
            ++found;
    CHECK(found == 2);
}

TEST_CASE("A solid cylinder is not reported as a hole", "[HoleDetector]")
{
    // The wall is convex: its normals point away from the axis, so it is not a cavity.
    const TriangleMesh cylinder = make_cylinder(3., 12.);
    CHECK(detect_holes(cylinder.its).empty());
}

TEST_CASE("A flat cube is not reported as a hole", "[HoleDetector]")
{
    const TriangleMesh cube = make_cube(20., 20., 20.);
    CHECK(detect_holes(cube.its).empty());
}

TEST_CASE("A sphere is not reported as a hole", "[HoleDetector]")
{
    const TriangleMesh sphere = make_sphere(5., PI / 30.);
    CHECK(detect_holes(sphere.its).empty());
}

// [ORCAPORT:PP-5] The teardrop case, with the numbers that pinned the gate. The patch around a
// teardrop tunnel is its whole side wall: arc and roof meet tangentially, so it is one smooth patch
// (132 faces; the two caps are separate and the cylinder test drops them).
//   [PP5] seed pts=132 r=2.543 residual=0.902      <- the roof pulls the first circle fit out
//   [PP5] circle pts=130/132 r=2.500 residual=0.000 <- the band keeps only the vertices ON the circle
//   [PP5] gap max=90.0 deg   convexity avg=-0.999
// The band has to select VERTICES, not faces: the roof quad splits into two triangles and the one
// sharing the arc's tangent vertex has its centroid on the circle (2.635 against r = 2.543) while it
// carries the apex vertex at 3.5 r; a face-centroid band re-admitted that apex to the refit and the
// maximum-distance residual then rejected the patch at 0.99 against the 0.20 limit. Selecting the
// vertices on the circle excludes the apex, and the roof faces stay out of the convexity average by
// their centroid.
TEST_CASE("A teardrop tunnel is detected as a hole", "[HoleDetector]")
{
    // What a teardrop bore leaves behind: a round wall for part of the perimeter and a sloped roof for
    // the rest. Built here as the tunnel surface alone and flipped, so its normals point at the axis -
    // which is what the detector sees inside a cavity. The roof is NOT the problem the shape suggests:
    // it contains the axis, so its normals are perpendicular to it like the wall's, and the convexity
    // average (measured -0.999) is fine. What the roof did was drag the circle fit: its apex vertices
    // sit 1.0 r off the circle and made the residual gate reject the patch.
    indexed_triangle_set its = its_make_teardrop_prism(2.5, 10., 45., Vec3d::UnitZ(), Vec3d::Zero(),
                                                      Vec3d::UnitY(), 64);
    REQUIRE_FALSE(its.empty());
    its_flip_triangles(its);

    const auto          holes = detect_holes(its);
    const DetectedHole *h     = hole_with_radius(holes, 2.5);
    REQUIRE(h != nullptr);
    CHECK_THAT(std::abs(h->axis.dot(Vec3d::UnitZ())), WithinAbs(1., 1e-3));
}

TEST_CASE("A through hole cut from a plate is detected as through", "[HoleDetector]")
{
    TriangleMesh plate  = make_cube(30., 30., 5.);
    TriangleMesh cutter = make_cylinder(2., 20., PI / 30.);
    cutter.translate(Vec3f(15.f, 15.f, -10.f));
    MeshBoolean::cgal::minus(plate, cutter);
    its_merge_vertices(plate.its);

    const auto holes = detect_holes(plate.its);
    const DetectedHole *h = hole_with_radius(holes, 2.);
    REQUIRE(h != nullptr);
    CHECK_THAT(std::abs(h->axis.dot(Vec3d::UnitZ())), WithinAbs(1., 1e-2));
    CHECK(h->through);
    CHECK_THAT(radial_distance(*h, Vec3d(15., 15., 2.5)), WithinAbs(0., 0.1));
}

TEST_CASE("A counterbore reports both its bore and its through shaft", "[HoleDetector]")
{
    TriangleMesh plate = make_cube(30., 30., 6.);
    TriangleMesh cbore = make_cylinder(4., 12., PI / 30.);
    cbore.translate(Vec3f(15.f, 15.f, 3.f));  // blind from the top, floor at z = 3
    TriangleMesh shaft = make_cylinder(2., 30., PI / 30.);
    shaft.translate(Vec3f(15.f, 15.f, -3.f)); // through
    MeshBoolean::cgal::minus(plate, cbore);
    MeshBoolean::cgal::minus(plate, shaft);
    its_merge_vertices(plate.its);

    const auto holes = detect_holes(plate.its);
    const DetectedHole *big   = hole_with_radius(holes, 4.);
    const DetectedHole *small = hole_with_radius(holes, 2.);
    REQUIRE(big != nullptr);
    REQUIRE(small != nullptr);
    CHECK_FALSE(big->through);   // the counterbore has a floor
    CHECK(small->through);       // the shaft is open
    CHECK(big->depth < small->depth);
}

