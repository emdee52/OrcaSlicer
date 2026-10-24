#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/FirstLayerFlow.hpp"

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

// The upload dialog preselects a reduction for the plates that tend to over-extrude their first
// layer: the three cool plates (Cool Plate / "Smooth Cool Plate", Textured Cool Plate and Cool
// Plate (SuperTack)). Every other plate starts at the profile's flow.
TEST_CASE("The first-layer flow default follows the plate type", "[OrcaFirstLayerFlow]") {
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btPC), WithinAbs(-20., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btPCT), WithinAbs(-20., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btSuperTack), WithinAbs(-20., 1e-9));

    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btPEI), WithinAbs(0., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btEP), WithinAbs(0., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btPTE), WithinAbs(0., 1e-9));
    CHECK_THAT(OrcaExt::first_layer_flow_plate_default(BedType::btDefault), WithinAbs(0., 1e-9));

    // The preselected value is one the row can show and the clamp accepts.
    CHECK_THAT(OrcaExt::first_layer_bottom_flow_scale(OrcaExt::kFirstLayerFlowCoolPlateDefault), WithinAbs(0.8, 1e-9));
    CHECK(OrcaExt::clamp_first_layer_flow_delta(OrcaExt::kFirstLayerFlowCoolPlateDefault) == OrcaExt::kFirstLayerFlowCoolPlateDefault);
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
