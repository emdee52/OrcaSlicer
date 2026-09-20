#include <catch2/catch_all.hpp>

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/TriangleMesh.hpp"

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
