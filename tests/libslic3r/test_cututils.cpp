#include <catch2/catch_all.hpp>

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::Geometry;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

indexed_triangle_set single_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c)
{
    indexed_triangle_set its;
    its.vertices = { a.cast<float>(), b.cast<float>(), c.cast<float>() };
    its.indices  = { Vec3i32(0, 1, 2) };
    return its;
}

Transform3d scaling(const Vec3d &s)
{
    Transform3d t = Transform3d::Identity();
    t.linear()    = s.asDiagonal();
    return t;
}

// A plane normal must be perpendicular to two edge vectors of the (transformed) triangle and
// have unit length. This is the defining property and catches a naive `linear * normal`
// transform, which only stays perpendicular under a similarity transform.
void check_perpendicular_unit(const indexed_triangle_set &its, const Vec3d &n, const Transform3d &trafo)
{
    REQUIRE(std::abs(n.norm() - 1.0) < 1e-9);
    const Vec3d p0 = trafo * its.vertices[0].cast<double>();
    const Vec3d p1 = trafo * its.vertices[1].cast<double>();
    const Vec3d p2 = trafo * its.vertices[2].cast<double>();
    CHECK_THAT(n.dot((p1 - p0).normalized()), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.dot((p2 - p0).normalized()), WithinAbs(0.0, 1e-9));
}

} // namespace

TEST_CASE("Facet normal is returned unchanged for the identity transform", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });

    const Vec3d n = facet_normal_in_world(its, 0, Transform3d::Identity());

    CHECK_THAT(n.x(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.y(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.z(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Facet normal follows a rotation", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });
    const Transform3d        trafo = rotation_transform(Vec3d(0., PI / 2., 0.)); // +Z -> +X

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    CHECK_THAT(n.x(), WithinAbs(1.0, 1e-9));
    CHECK_THAT(n.y(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.z(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Facet normal stays perpendicular under a non-uniform scale", "[CutUtils]")
{
    // A slanted triangle: a naive `linear * normal` would no longer be perpendicular to the
    // transformed edges.
    const indexed_triangle_set its   = single_triangle({ 0., 0., 0. }, { 2., 1., 0. }, { 0.5, 0.5, 1. });
    const Transform3d        trafo   = scaling(Vec3d(1.0, 2.5, 0.4));

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    check_perpendicular_unit(its, n, trafo);

    // Exact expected value: inverse-transpose of the linear part.
    const Vec3d expected = (trafo.linear().inverse().transpose() * its_face_normal(its, 0).cast<double>()).normalized();
    CHECK_THAT(n.x(), WithinRel(expected.x(), 1e-9));
    CHECK_THAT(n.y(), WithinRel(expected.y(), 1e-9));
    CHECK_THAT(n.z(), WithinRel(expected.z(), 1e-9));
}

TEST_CASE("Facet normal stays perpendicular under a mirrored transform", "[CutUtils]")
{
    const indexed_triangle_set its   = single_triangle({ 0., 0., 0. }, { 2., 1., 0. }, { 0.5, 0.5, 1. });
    const Transform3d        trafo   = scaling(Vec3d(-1.0, 1.0, 1.0));

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    check_perpendicular_unit(its, n, trafo);
}

TEST_CASE("Out-of-range facet returns the fallback normal", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });

    CHECK(facet_normal_in_world(its, -1, Transform3d::Identity()).isApprox(Vec3d::UnitZ()));
    CHECK(facet_normal_in_world(its, 7, Transform3d::Identity()).isApprox(Vec3d::UnitZ()));
}
