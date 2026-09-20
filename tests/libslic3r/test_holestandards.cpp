#include <catch2/catch_all.hpp>

#include "libslic3r/HoleStandards.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

TEST_CASE("A known screw standard reports its clearance and head dimensions", "[HoleStandards]")
{
    const HoleStandard *m3 = find_hole_standard("M3");
    REQUIRE(m3 != nullptr);
    CHECK(m3->kind == HoleStandardKind::Screw);
    CHECK_THAT(m3->clearance_d, WithinAbs(3.4, 1e-9));
    CHECK_THAT(m3->socket_d, WithinAbs(5.5, 1e-9));
    CHECK_THAT(m3->socket_k, WithinAbs(3.0, 1e-9));
    CHECK_THAT(m3->button_d, WithinAbs(5.7, 1e-9));
    CHECK_THAT(m3->csink_d, WithinAbs(6.0, 1e-9));
}

TEST_CASE("An unknown designation is not a standard", "[HoleStandards]")
{
    CHECK(find_hole_standard("M7") == nullptr);
    CHECK(find_hole_standard("") == nullptr);
}

TEST_CASE("Insert and magnet standards carry a pocket diameter and depth", "[HoleStandards]")
{
    const HoleStandard *insert = find_hole_standard("M3 insert");
    REQUIRE(insert != nullptr);
    CHECK(insert->kind == HoleStandardKind::Insert);
    CHECK_THAT(insert->pocket_d, WithinAbs(4.2, 1e-9));
    REQUIRE(insert->insert_heights.size() == 3);
    CHECK_THAT(insert->insert_heights[0], WithinAbs(3.0, 1e-9));
    CHECK_THAT(insert->insert_heights[1], WithinAbs(5.0, 1e-9));
    CHECK_THAT(insert->insert_heights[2], WithinAbs(7.0, 1e-9));

    const HoleStandard *magnet = find_hole_standard("6x3 magnet");
    REQUIRE(magnet != nullptr);
    CHECK(magnet->kind == HoleStandardKind::Magnet);
    CHECK_THAT(magnet->pocket_d, WithinAbs(6.0, 1e-9));
    CHECK_THAT(magnet->pocket_depth, WithinAbs(3.0, 1e-9));
}

TEST_CASE("Slip gives more pocket clearance than tight", "[HoleStandards]")
{
    CHECK_THAT(hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Tight), WithinAbs(0.05, 1e-9));
    CHECK_THAT(hole_fit_diameter_delta(HoleStandardKind::Magnet, 6.0, HoleFit::Slip), WithinAbs(0.16, 1e-9));
    CHECK(hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Tight) <
          hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Slip));
}

TEST_CASE("Screws carry both a free and a tap diameter", "[HoleStandards]")
{
    const HoleStandard *m3 = find_hole_standard("M3");
    REQUIRE(m3 != nullptr);
    CHECK_THAT(m3->clearance_d, WithinAbs(3.4, 1e-9));
    CHECK_THAT(m3->tap_d, WithinAbs(2.6, 1e-9));
    CHECK_THAT(screw_nominal_diameter(*m3, false), WithinAbs(3.4, 1e-9));
    CHECK_THAT(screw_nominal_diameter(*m3, true), WithinAbs(2.6, 1e-9));
}

TEST_CASE("Small screws are tap-only", "[HoleStandards]")
{
    const HoleStandard *m2 = find_hole_standard("M2");
    REQUIRE(m2 != nullptr);
    CHECK(m2->kind == HoleStandardKind::Screw);
    CHECK_THAT(m2->clearance_d, WithinAbs(0., 1e-9));
    CHECK_THAT(m2->tap_d, WithinAbs(1.70, 1e-9));
    // Free falls back to the tap diameter when there is no clearance fit.
    CHECK_THAT(screw_nominal_diameter(*m2, false), WithinAbs(1.70, 1e-9));
    CHECK_THAT(screw_nominal_diameter(*m2, true), WithinAbs(1.70, 1e-9));
}

TEST_CASE("Nuts expose an across-flats and height", "[HoleStandards]")
{
    const HoleStandard *m3 = find_hole_standard("M3 nut");
    REQUIRE(m3 != nullptr);
    CHECK(m3->kind == HoleStandardKind::Nut);
    CHECK_THAT(m3->across_flats, WithinAbs(5.50, 1e-9));
    CHECK_THAT(m3->pocket_depth, WithinAbs(2.40, 1e-9));
    CHECK_THAT(m3->clearance_d, WithinAbs(3.4, 1e-9)); // matching screw clearance bore
    CHECK(find_hole_standard("M2 nut") == nullptr);
    CHECK(find_hole_standard("M2.5 nut") == nullptr);
}


