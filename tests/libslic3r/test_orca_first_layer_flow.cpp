#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/FirstLayerFlow.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

// The upload dialog's "First layer flow" row reduces the bottom surface flow ratio of layer 1. A
// reduction of 0 must leave the profile's value untouched, and each percent is one hundredth of the
// emitted flow.
TEST_CASE("A first-layer flow reduction scales the profile's bottom surface ratio", "[OrcaFirstLayerFlow]") {
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(0.), WithinAbs(1., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(-5.), WithinAbs(0.95, 1e-9));
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(-20.), WithinAbs(0.80, 1e-9));

    // The profile's own value is the reference, so a non-1.0 ratio keeps its proportion.
    CHECK_THAT(OrcaExt::first_layer_bottom_effective_flow(1.0, -10.), WithinAbs(0.90, 1e-9));
    CHECK_THAT(OrcaExt::first_layer_bottom_effective_flow(0.95, -10.), WithinAbs(0.855, 1e-9));
    CHECK_THAT(OrcaExt::first_layer_bottom_effective_flow(0.95, 0.), WithinAbs(0.95, 1e-9));
}

// The upload dialog's automation preselects a reduction for a plate type only once the user
// configured that type in the plate automation popup; with nothing configured nothing changes.
TEST_CASE("The first-layer flow default follows the local per-plate automation", "[OrcaFirstLayerFlow]") {
    OrcaExt::PlateFlowDefaults defaults;
    CHECK_FALSE(OrcaExt::has_flow_default(defaults, BedType::btPC));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPC), WithinAbs(0., 1e-9));

    defaults[int(BedType::btPC)]        = -20.;
    defaults[int(BedType::btSuperTack)] = -12.;

    CHECK(OrcaExt::has_flow_default(defaults, BedType::btPC));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPC), WithinAbs(-20., 1e-9));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btSuperTack), WithinAbs(-12., 1e-9));
    CHECK_FALSE(OrcaExt::has_flow_default(defaults, BedType::btPTE));

    // A configured zero is a real choice ("print at the profile's flow") and must still count as
    // configured, so an older per-project value does not come back through it.
    defaults[int(BedType::btPTE)] = 0.;
    CHECK(OrcaExt::has_flow_default(defaults, BedType::btPTE));
    CHECK_THAT(OrcaExt::flow_default_for(defaults, BedType::btPTE), WithinAbs(0., 1e-9));
}

// The reduction only ever lowers flow: it must not amplify an over-extruding first layer, and a
// hand-edited project must not be able to starve the first layer to nothing.
TEST_CASE("First-layer flow reduction cannot amplify and never stops extrusion", "[OrcaFirstLayerFlow]") {
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(10.), WithinAbs(1., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(-200.), WithinAbs(OrcaExt::kFirstLayerFlowScaleMin, 1e-9));
    CHECK(OrcaExt::clamp_first_layer_flow_delta(10.) == OrcaExt::kFirstLayerFlowDeltaMax);
    CHECK(OrcaExt::clamp_first_layer_flow_delta(-200.) == OrcaExt::kFirstLayerFlowDeltaMin);

    // Below the floor the effective flow still follows the profile ratio, never 0.
    CHECK(OrcaExt::first_layer_bottom_effective_flow(1.0, -200.) > 0.);
}
