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

namespace {

// Index of the first face whose outward normal points up.
int top_face(const indexed_triangle_set &its)
{
    const std::vector<Vec3f> normals = its_face_normals(its);
    for (size_t i = 0; i < normals.size(); ++i)
        if (normals[i].z() > 0.9f)
            return int(i);
    return -1;
}

} // namespace

TEST_CASE("A flat face with a round boundary yields one closed loop", "[EdgeProfiles]")
{
    constexpr double radius = 5.;
    constexpr double height = 10.;
    const indexed_triangle_set cyl = its_make_cylinder(radius, height, 2. * PI / 64.);

    const int seed = top_face(cyl);
    REQUIRE(seed >= 0);
    const std::vector<std::vector<LoopFrame>> loops =
        its_face_patch_loops(cyl, Transform3d::Identity(), seed);

    REQUIRE(loops.size() == 1);
    CHECK(loops.front().size() == 64);
    for (const LoopFrame &f : loops.front())
        CHECK_THAT(f.p.z(), WithinAbs(height, 1e-4));
}

TEST_CASE("A loop chamfer rings the whole hole or boss rim", "[EdgeProfiles]")
{
    constexpr double radius = 5.;
    constexpr double height = 10.;
    constexpr double size   = 1.;
    const indexed_triangle_set cyl = its_make_cylinder(radius, height, 2. * PI / 64.);

    const std::vector<std::vector<LoopFrame>> loops =
        its_face_patch_loops(cyl, Transform3d::Identity(), top_face(cyl));
    REQUIRE_FALSE(loops.empty());

    const indexed_triangle_set chamfer = make_loop_chamfer(loops.front(), size);
    REQUIRE_FALSE(chamfer.empty());
    CHECK(its_num_open_edges(chamfer) == 0);
    CHECK(its_volume(chamfer) > 0.f);

    // A triangular section of area size^2/2 revolved at the rim, its centroid size/3 inwards.
    CHECK_THAT(double(its_volume(chamfer)),
               WithinRel(PI * size * size * (radius - size / 3.), 0.02));

    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.max.z(), WithinAbs(height, 1e-3));
    CHECK_THAT(bb.min.z(), WithinAbs(height - size, 1e-3));

    const indexed_triangle_set fillet = make_loop_fillet(loops.front(), size);
    REQUIRE_FALSE(fillet.empty());
    CHECK(its_num_open_edges(fillet) == 0);
    CHECK(its_volume(fillet) < its_volume(chamfer));
}

TEST_CASE("A loop chamfer follows a square face outline", "[EdgeProfiles]")
{
    constexpr double size = 1.;
    const indexed_triangle_set box = its_make_cube(10., 10., 10.);

    const std::vector<std::vector<LoopFrame>> loops =
        its_face_patch_loops(box, Transform3d::Identity(), top_face(box));
    REQUIRE(loops.size() == 1);
    REQUIRE(loops.front().size() == 4);

    const indexed_triangle_set chamfer = make_loop_chamfer(loops.front(), size);
    REQUIRE_FALSE(chamfer.empty());
    CHECK(its_num_open_edges(chamfer) == 0);
    CHECK(its_volume(chamfer) > 0.f);
    // The section keeps the crease line, so the ring spans the whole top face outline and drops
    // from the top down by `size`. With a miter at each corner the volume is the perimeter sweep.
    CHECK_THAT(double(its_volume(chamfer)), WithinRel(0.5 * size * size * 40., 0.05));

    const BoundingBoxf3 bb = bounding_box(chamfer);
    CHECK_THAT(bb.min.z(), WithinAbs(10. - size, 1e-3));
    CHECK_THAT(bb.max.z(), WithinAbs(10., 1e-3));
    CHECK_THAT(bb.min.x(), WithinAbs(0., 1e-3));
    CHECK_THAT(bb.max.x(), WithinAbs(10., 1e-3));
}

TEST_CASE("A piece of a curved surface is not a rim", "[EdgeProfiles]")
{
    // A face of the cylinder wall is coplanar only with the rest of its own quad, and its outline
    // runs along the wall, where the next face is a few degrees away. Dressing such a boundary
    // would sweep a section around a sliver of a smooth surface, so no loop may be offered. The
    // top face, whose outline really is a rim, is still offered.
    const indexed_triangle_set   cyl     = its_make_cylinder(5., 10., 2. * PI / 64.);
    const std::vector<Vec3f>     normals = its_face_normals(cyl);
    int                          walls   = 0;
    for (size_t f = 0; f < cyl.indices.size(); ++f) {
        if (std::abs(normals[f].z()) > 0.1f)
            continue;
        ++walls;
        CHECK(its_face_patch_loops(cyl, Transform3d::Identity(), int(f)).empty());
    }
    CHECK(walls > 0);
    CHECK_FALSE(its_face_patch_loops(cyl, Transform3d::Identity(), top_face(cyl)).empty());
}

TEST_CASE("Loop dress rejects degenerate input", "[EdgeProfiles]")
{
    const indexed_triangle_set cyl = its_make_cylinder(5., 10., 2. * PI / 32.);
    const std::vector<std::vector<LoopFrame>> loops =
        its_face_patch_loops(cyl, Transform3d::Identity(), top_face(cyl));
    REQUIRE_FALSE(loops.empty());

    CHECK(make_loop_chamfer(loops.front(), 0.).empty());
    CHECK(make_loop_fillet(loops.front(), -1.).empty());
    CHECK(sweep_loop({}, chamfer_profile(1.)).empty());
    CHECK(sweep_loop(loops.front(), {}).empty());
    CHECK(its_face_patch_loops(cyl, Transform3d::Identity(), -1).empty());
    CHECK(its_face_patch_loops(cyl, Transform3d::Identity(), int(cyl.indices.size())).empty());
}

TEST_CASE("A rim behind a smooth band is found by the ring fallback", "[EdgeProfiles]")
{
    // The wall of a cylinder is a smooth band: a patch on it has no loop of its own. A cursor there
    // must still be able to pick the rim of the cap just beyond the band, so the search rings out
    // from the face under the cursor until it reaches a patch that does have a loop.
    const indexed_triangle_set cyl       = its_make_cylinder(5., 10., 2. * PI / 64.);
    const std::vector<Vec3f>   normals   = its_face_normals(cyl);
    const std::vector<Vec3i32> neighbors = its_face_neighbors(cyl);
    const int                  top       = top_face(cyl);
    int                        wall      = -1;
    for (size_t f = 0; f < cyl.indices.size() && wall < 0; ++f) {
        if (std::abs(normals[f].z()) > 0.1f)
            continue;
        for (int k = 0; k < 3; ++k)
            if (neighbors[f][k] == top) {
                wall = int(f);
                break;
            }
    }
    REQUIRE(wall >= 0);

    CHECK(its_face_patch_loops(cyl, Transform3d::Identity(), wall).empty());
    CHECK(its_face_patch_loops_around(cyl, Transform3d::Identity(), wall, 0).empty());
    CHECK_FALSE(its_face_patch_loops_around(cyl, Transform3d::Identity(), wall, 1).empty());
    CHECK_FALSE(its_face_patch_loops_around(cyl, Transform3d::Identity(), top, 0).empty());
}

