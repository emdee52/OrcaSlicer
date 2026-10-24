#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/PlateAutomation.hpp"

#include <cmath>
#include <string>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

// The local flow defaults are per plate type, saved in AppConfig as one string. A configured 0 is a
// real choice ("print at the profile's flow"): it has to stay in the map so it can override an older
// per-project value instead of falling back to it.
TEST_CASE("Plate flow defaults round-trip through the AppConfig string", "[OrcaPlateAutomation]") {
    OrcaExt::PlateFlowDefaults defaults;
    defaults[int(BedType::btPC)]        = -20.;
    defaults[int(BedType::btPCT)]       = -12.;
    defaults[int(BedType::btSuperTack)] = 0.;

    const std::string                serialized = OrcaExt::serialize_flow_defaults(defaults);
    const OrcaExt::PlateFlowDefaults parsed     = OrcaExt::parse_flow_defaults(serialized);
    CHECK(parsed == defaults);

    CHECK(OrcaExt::parse_flow_defaults("").empty());
    CHECK(OrcaExt::parse_flow_defaults("junk").empty());
    CHECK(OrcaExt::parse_flow_defaults("x:y;:;-5").empty());

    // Out-of-range values are clamped; an unknown plate type is dropped.
    const auto clamped = OrcaExt::parse_flow_defaults("1:-99;2:10;99:-20");
    REQUIRE(clamped.count(int(BedType::btPC)) == 1);
    CHECK_THAT(clamped.at(int(BedType::btPC)), WithinAbs(OrcaExt::kFirstLayerFlowDeltaMin, 1e-9));
    CHECK_THAT(clamped.at(int(BedType::btEP)), WithinAbs(0., 1e-9));
    CHECK(clamped.count(99) == 0);
}

// The upload dialog asks "is this plate type configured?" before letting the automation own the row,
// and then looks the reduction up.
TEST_CASE("Plate flow defaults resolve per plate type", "[OrcaPlateAutomation]") {
    OrcaExt::PlateFlowDefaults defaults;
    CHECK_FALSE(OrcaExt::has_flow_default(defaults, BedType::btPC));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPC), WithinAbs(0., 1e-9));

    defaults[int(BedType::btPC)]  = -20.;
    defaults[int(BedType::btPCT)] = -12.;
    CHECK(OrcaExt::has_flow_default(defaults, BedType::btPC));
    CHECK_FALSE(OrcaExt::has_flow_default(defaults, BedType::btPTE));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPC), WithinAbs(-20., 1e-9));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPCT), WithinAbs(-12., 1e-9));

    // A configured zero counts as configured and yields the profile's flow.
    defaults[int(BedType::btPTE)] = 0.;
    CHECK(OrcaExt::has_flow_default(defaults, BedType::btPTE));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPTE), WithinAbs(0., 1e-9));
}
