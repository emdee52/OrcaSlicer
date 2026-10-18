#include <catch2/catch_all.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <libslic3r/TriangleMesh.hpp>
#include <libslic3r/MeshBoolean.hpp>
#include <libslic3r/AABBMesh.hpp>
#include <libslic3r/HoleShapes.hpp>
#include <libslic3r/CSGMesh/CSGMesh.hpp>
#include <libslic3r/CSGMesh/PerformCSGMeshBooleans.hpp>
#include <limits>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

namespace {

// The material a mesh shows along -Z at (x, 10): where a ray from above first enters it and where it
// last leaves it, so a covering face shows up as a high value of the second.
std::pair<double, double> z_span(const indexed_triangle_set &its, double x)
{
    const Vec3d                             origin(x, 10., 15.);
    const std::vector<AABBMesh::hit_result> hits = AABBMesh(its).query_ray_hits(origin, -Vec3d::UnitZ());
    double                                  lo = std::numeric_limits<double>::max();
    double                                  hi = -std::numeric_limits<double>::max();
    for (const AABBMesh::hit_result &h : hits) {
        if (!h.is_hit())
            continue;
        const double z = origin.z() - h.distance();
        lo             = std::min(lo, z);
        hi             = std::max(hi, z);
    }
    return {lo, hi};
}

// A 20x20x10 plate (x, y in [0, 20], z in [0, 10]) with one counterbore cut into its top face: a
// 3.4 mm clearance bore 6 mm deep (floor at z = 4) with a 6 mm socket-head counterbore 3 mm deep
// (shoulder at z = 7). The entrance is on the top face, as a detected hole's is, and `out_margin` is
// what the Holes gizmo asks a screw head for.
std::vector<TriangleMesh> cut_counterbore(double out_margin)
{
    TriangleMesh body = make_cube(20., 20., 10.);
    TriangleMesh tool;
    tool.its = its_make_counterbore(3.4, 6., 3., 6., -Vec3d::UnitZ(), Vec3d(10., 10., 10.), out_margin);
    std::vector<TriangleMesh> result;
    MeshBoolean::mcut::make_boolean(body, tool, result, "A_NOT_B");
    return result;
}

} // namespace

TEST_CASE("CGAL and TriangleMesh conversions", "[MeshBoolean]") {
    TriangleMesh sphere = make_sphere(1.);
    
    auto cgalmesh_ptr = MeshBoolean::cgal::triangle_mesh_to_cgal(sphere);
    
    REQUIRE(cgalmesh_ptr);
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(*cgalmesh_ptr));
    
    TriangleMesh M = MeshBoolean::cgal::cgal_to_triangle_mesh(*cgalmesh_ptr);
    
    REQUIRE(M.its.vertices.size() == sphere.its.vertices.size());
    REQUIRE(M.its.indices.size() == sphere.its.indices.size());
    
    REQUIRE(M.volume() == Catch::Approx(sphere.volume()));
    
    REQUIRE(! MeshBoolean::cgal::does_self_intersect(M));
}

TEST_CASE("mcut difference handles source splits between cuts", "[MeshBoolean]") {
    TriangleMesh body = make_cube(30., 10., 10.);

    TriangleMesh tool;

    // First cut splits the source into two disconnected components.
    TriangleMesh slab = make_cube(2., 12., 20.);
    slab.translate(Vec3f(14.f, -1.f, -5.f));
    its_merge(tool.its, slab.its);

    // These cuts must still be applied after the source has been split.
    TriangleMesh left_hole = make_cube(4., 4., 20.);
    left_hole.translate(Vec3f(3.f, 3.f, -5.f));
    its_merge(tool.its, left_hole.its);

    TriangleMesh right_hole = make_cube(4., 4., 20.);
    right_hole.translate(Vec3f(21.f, 3.f, -5.f));
    its_merge(tool.its, right_hole.its);

    std::vector<TriangleMesh> result;
    MeshBoolean::mcut::make_boolean(body, tool, result, "A_NOT_B");

    REQUIRE(result.size() == 1);

    const std::vector<indexed_triangle_set> components =
        its_split(result.front().its);

    REQUIRE(components.size() == 2);

    // 3000 - 200 - 160 - 160 = 2480.
    REQUIRE_THAT(
        result.front().volume(),
        Catch::Matchers::WithinRel(2480., 1e-3)
    );
}

TEST_CASE("A CSG difference the kernel cannot apply is counted, not dropped in silence", "[MeshBoolean]")
{
    // [ORCAPORT:MB-2] The whole-object boolean - the Mesh boolean item in the object context menu -
    // runs a list of parts through this sequence: model parts as Union, negative volumes as Difference.
    // When mcut cannot subtract one, do_boolean keeps the part it could not cut, so the accumulated
    // mesh comes out unchanged: no cut, no error, nothing to notice. DifferenceReport is what carries
    // that back to the caller, and this is the property it protects.
    TriangleMesh body = make_cube(20., 20., 20.);

    TriangleMesh cutter_away = make_cube(4., 4., 4.);
    cutter_away.translate(Vec3f(100.f, 100.f, 100.f));   // nowhere near the body: nothing to subtract
    TriangleMesh cutter_here = make_cube(4., 4., 4.);    // centred on the body: it cuts a cavity

    std::vector<csg::CSGPart> parts;
    parts.emplace_back(csg::CSGPart{&body.its, csg::CSGType::Union});
    parts.emplace_back(csg::CSGPart{&cutter_away.its, csg::CSGType::Difference});

    csg::DifferenceReport away;
    auto away_mesh = csg::perform_csgmesh_booleans_mcut(range(parts), &away);
    REQUIRE(away_mesh != nullptr);
    REQUIRE(away.offered == 1);
    REQUIRE(away.not_applied == 1);

    // The body survives untouched - which is what "nothing was cut" looks like from the outside.
    TriangleMesh away_result = MeshBoolean::mcut::mcut_to_triangle_mesh(*away_mesh);
    REQUIRE_THAT(double(away_result.volume()), Catch::Matchers::WithinRel(double(body.volume()), 1e-3));

    // A cutter that does overlap is applied, and is not reported as skipped.
    parts.clear();
    parts.emplace_back(csg::CSGPart{&body.its, csg::CSGType::Union});
    parts.emplace_back(csg::CSGPart{&cutter_here.its, csg::CSGType::Difference});

    csg::DifferenceReport here;
    auto here_mesh = csg::perform_csgmesh_booleans_mcut(range(parts), &here);
    REQUIRE(here_mesh != nullptr);
    REQUIRE(here.offered == 1);
    REQUIRE(here.not_applied == 0);

    // 20^3 - 4^3 = 7936: the 4 mm cube sits inside the body, so the result is a closed cavity.
    TriangleMesh here_result = MeshBoolean::mcut::mcut_to_triangle_mesh(*here_mesh);
    REQUIRE_THAT(double(here_result.volume()), Catch::Matchers::WithinRel(7936., 1e-2));
}

TEST_CASE("A counterbore whose entrance cap is in the face comes out covered by a skin", "[MeshBoolean]")
{
    // [ORCAPORT:ME-3] This is mcut's answer to a cutter's entrance cap lying exactly IN the part's
    // face: a coplanar pair, which it resolves into a zero-thickness sheet across the whole recess.
    // The sheet is invisible to the slicer - so the print has the counterbore - while the mesh and the
    // 3D view say the surface is still solid. It is why the Holes gizmo asks every screw head for an
    // entrance margin (see the case below); if mcut ever stops doing this, that margin stops being
    // load-bearing and this case is the one to drop.
    const std::vector<TriangleMesh> result = cut_counterbore(0.);
    REQUIRE(result.size() == 1);

    // A ray from above meets the top face over the recess instead of the bore's floor and the
    // counterbore's shoulder.
    CHECK_THAT(z_span(result.front().its, 10.).second, WithinAbs(10., 1e-3));
    CHECK_THAT(z_span(result.front().its, 12.4).second, WithinAbs(10., 1e-3));
}

TEST_CASE("A counterbore whose entrance cap is outside the face cuts the recess cleanly", "[MeshBoolean]")
{
    // [ORCAPORT:ME-3] The same cutter with the entrance margin the Holes gizmo asks a screw head for:
    // nothing is coplanar, and the recess comes out open - the bore's floor and the counterbore's
    // shoulder are what a ray from above meets.
    const std::vector<TriangleMesh> result = cut_counterbore(hole_cutter_entry_margin(3.4));
    REQUIRE(result.size() == 1);

    CHECK_THAT(z_span(result.front().its, 10.).second, WithinAbs(4., 1e-3));
    CHECK_THAT(z_span(result.front().its, 12.4).second, WithinAbs(7., 1e-3));
}
