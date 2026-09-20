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
    CHECK_THAT(m3->cbore_d, WithinAbs(6.0, 1e-9));
    CHECK_THAT(m3->cbore_depth, WithinAbs(3.4, 1e-9));
    CHECK_THAT(m3->csink_d, WithinAbs(6.3, 1e-9));
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
    CHECK_THAT(insert->pocket_d, WithinAbs(4.0, 1e-9));
    CHECK_THAT(insert->pocket_depth, WithinAbs(5.7, 1e-9));

    const HoleStandard *magnet = find_hole_standard("6x3 magnet");
    REQUIRE(magnet != nullptr);
    CHECK(magnet->kind == HoleStandardKind::Magnet);
    CHECK_THAT(magnet->pocket_d, WithinAbs(6.0, 1e-9));
    CHECK_THAT(magnet->pocket_depth, WithinAbs(3.0, 1e-9));
}

TEST_CASE("Pocket clearance grows with the fit and with the diameter", "[HoleStandards]")
{
    const double tight = hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Tight);
    const double slip  = hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Slip);
    const double epoxy = hole_fit_diameter_delta(HoleStandardKind::Insert, 4.0, HoleFit::Epoxy);
    CHECK(tight < slip);
    CHECK(slip < epoxy);

    CHECK(hole_fit_diameter_delta(HoleStandardKind::Magnet, 10.0, HoleFit::Slip) >
          hole_fit_diameter_delta(HoleStandardKind::Magnet, 3.0, HoleFit::Slip));
}
