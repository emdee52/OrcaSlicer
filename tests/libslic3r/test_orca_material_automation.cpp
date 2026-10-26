#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/MaterialAutomation.hpp"

#include <cmath>
#include <string>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;

// The local material table is one AppConfig string keyed by filament_type. An empty field must stay
// empty (the profile's value is then in charge), and a hand-edited string must never produce an
// out-of-range value: both are what keep the feature off by default and safe to edit.
TEST_CASE("Material table round-trips through its string encoding", "[OrcaMaterialAutomation]") {
    OrcaExt::MaterialTable table;
    OrcaExt::MaterialValues pla;
    pla.has_bridge_density = true;
    pla.bridge_density     = 120.;
    pla.has_bridge_flow    = true;
    pla.bridge_flow        = 1.05;
    pla.has_bridge_speed   = true;
    pla.bridge_speed       = 30.;
    pla.has_top_flow       = true;
    pla.top_flow           = 0.96;
    pla.has_top_width      = true;
    pla.top_width          = 110.;
    pla.top_width_percent  = true;
    pla.has_top_speed      = true;
    pla.top_speed          = 80.;
    pla.has_outer_speed    = true;
    pla.outer_speed        = 120.;
    pla.has_first_layer_flow = true;
    pla.first_layer_flow     = -10.;
    table["PLA"] = pla;

    OrcaExt::MaterialValues petg;
    petg.has_bridge_density = true;
    petg.bridge_density     = 95.;
    table["PETG"]           = petg;

    const std::string          serialized = OrcaExt::serialize(table);
    const OrcaExt::MaterialTable parsed    = OrcaExt::parse(serialized);

    REQUIRE(parsed.size() == 2);
    const OrcaExt::MaterialValues *round_trip = OrcaExt::find_material(parsed, "pla");
    REQUIRE(round_trip != nullptr);
    CHECK(round_trip->has_bridge_density);
    CHECK_THAT(round_trip->bridge_density, WithinAbs(120., 1e-9));
    CHECK(round_trip->has_bridge_flow);
    CHECK_THAT(round_trip->bridge_flow, WithinAbs(1.05, 1e-9));
    CHECK(round_trip->has_bridge_speed);
    CHECK_THAT(round_trip->bridge_speed, WithinAbs(30., 1e-9));
    CHECK(round_trip->has_top_flow);
    CHECK_THAT(round_trip->top_flow, WithinAbs(0.96, 1e-9));
    CHECK(round_trip->has_top_width);
    CHECK(round_trip->top_width_percent);
    CHECK_THAT(round_trip->top_width, WithinAbs(110., 1e-9));
    CHECK(round_trip->has_top_speed);
    CHECK_THAT(round_trip->top_speed, WithinAbs(80., 1e-9));
    CHECK(round_trip->has_outer_speed);
    CHECK_THAT(round_trip->outer_speed, WithinAbs(120., 1e-9));
    CHECK(round_trip->has_first_layer_flow);
    CHECK_THAT(round_trip->first_layer_flow, WithinAbs(-10., 1e-9));

    // An unset field stays unset, so the profile keeps its own value.
    const OrcaExt::MaterialValues *other = OrcaExt::find_material(parsed, "PETG");
    REQUIRE(other != nullptr);
    CHECK(! other->has_bridge_flow);
    CHECK(! other->has_top_width);
    CHECK(! other->has_first_layer_flow);

    CHECK(OrcaExt::parse("").empty());
    CHECK(OrcaExt::parse("junk").empty());
    CHECK(OrcaExt::parse("PLA:").empty());
}

TEST_CASE("Material table parsing is tolerant and bounded", "[OrcaMaterialAutomation]") {
    CHECK(OrcaExt::normalize_material("  pla ").compare("PLA") == 0);

    // Unknown field and malformed number are dropped; the rest of the row survives.
    const OrcaExt::MaterialTable parsed = OrcaExt::parse(" pla :bogus=1;bd=oops;bf=1.1 ");
    const OrcaExt::MaterialValues *values = OrcaExt::find_material(parsed, "PLA");
    REQUIRE(values != nullptr);
    CHECK(! values->has_bridge_density);
    CHECK(values->has_bridge_flow);
    CHECK_THAT(values->bridge_flow, WithinAbs(1.1, 1e-9));

    // Out-of-range values are clamped to the option's own range.
    const OrcaExt::MaterialTable clamped = OrcaExt::parse("PLA:bd=500;bs=0;fl=-90;tf=9");
    const OrcaExt::MaterialValues *clamped_values = OrcaExt::find_material(clamped, "PLA");
    REQUIRE(clamped_values != nullptr);
    CHECK_THAT(clamped_values->bridge_density, WithinAbs(OrcaExt::kBridgeDensityMax, 1e-9));
    CHECK_THAT(clamped_values->bridge_speed, WithinAbs(OrcaExt::kSpeedMin, 1e-9));
    CHECK_THAT(clamped_values->first_layer_flow, WithinAbs(OrcaExt::kFirstLayerFlowDeltaMin, 1e-9));
    CHECK_THAT(clamped_values->top_flow, WithinAbs(OrcaExt::kFlowRatioMax, 1e-9));

    // A mm width and a percent width are distinguishable after the round trip.
    const OrcaExt::MaterialTable widths = OrcaExt::parse("PLA:tw=0.45|PETG:tw=120%");
    CHECK_THAT(OrcaExt::find_material(widths, "PLA")->top_width, WithinAbs(0.45, 1e-9));
    CHECK(! OrcaExt::find_material(widths, "PLA")->top_width_percent);
    CHECK_THAT(OrcaExt::find_material(widths, "PETG")->top_width, WithinAbs(120., 1e-9));
    CHECK(OrcaExt::find_material(widths, "PETG")->top_width_percent);
}

// A plate stores one table per key kind: only the toolpath inputs of the recipes it has on, and only
// the E/F inputs of the same recipes. The first-layer flow is not a recipe: it applies per material
// as soon as the material sets it, which is what "no toggle" means.
TEST_CASE("Resolving a plate keeps only the enabled recipes' fields", "[OrcaMaterialAutomation]") {
    OrcaExt::MaterialValues values;
    values.has_bridge_density    = true;
    values.bridge_density        = 120.;
    values.has_bridge_flow       = true;
    values.bridge_flow           = 1.05;
    values.has_bridge_speed      = true;
    values.bridge_speed          = 30.;
    values.has_top_flow          = true;
    values.top_flow              = 0.96;
    values.has_top_width         = true;
    values.top_width             = 110.;
    values.top_width_percent     = true;
    values.has_top_speed         = true;
    values.top_speed             = 80.;
    values.has_outer_speed       = true;
    values.outer_speed           = 120.;
    values.has_first_layer_flow  = true;
    values.first_layer_flow      = -10.;

    OrcaExt::MaterialTable table;
    table["PLA"] = values;

    // Free bridging only: the toolpath key carries density and flow, the G-code key only the speed.
    const OrcaExt::MaterialTable slice_free = OrcaExt::resolved_table(table, int(OrcaExt::MaterialRecipe::FreeBridging), true);
    const OrcaExt::MaterialValues *free_slice = OrcaExt::find_material(slice_free, "PLA");
    REQUIRE(free_slice != nullptr);
    CHECK(free_slice->has_bridge_density);
    CHECK(free_slice->has_bridge_flow);
    CHECK(! free_slice->has_top_width);
    CHECK(! free_slice->empty());

    const OrcaExt::MaterialTable gcode_free = OrcaExt::resolved_table(table, int(OrcaExt::MaterialRecipe::FreeBridging), false);
    const OrcaExt::MaterialValues *free_gcode = OrcaExt::find_material(gcode_free, "PLA");
    REQUIRE(free_gcode != nullptr);
    CHECK(free_gcode->has_bridge_speed);
    CHECK(! free_gcode->has_top_flow);
    CHECK(! free_gcode->has_outer_speed);
    // No toggle governs the first-layer flow, so it is always in the G-code half.
    CHECK(free_gcode->has_first_layer_flow);

    // Quality top: the width is a toolpath input, flow and speed are not.
    const OrcaExt::MaterialTable slice_top = OrcaExt::resolved_table(table, int(OrcaExt::MaterialRecipe::QualityTop), true);
    CHECK(! OrcaExt::find_material(slice_top, "PLA")->has_bridge_density);
    CHECK(OrcaExt::find_material(slice_top, "PLA")->has_top_width);

    const OrcaExt::MaterialTable gcode_top = OrcaExt::resolved_table(table, int(OrcaExt::MaterialRecipe::QualityTop), false);
    CHECK(OrcaExt::find_material(gcode_top, "PLA")->has_top_flow);
    CHECK(OrcaExt::find_material(gcode_top, "PLA")->has_top_speed);
    CHECK(! OrcaExt::find_material(gcode_top, "PLA")->has_outer_speed);

    // Outer speed only.
    const OrcaExt::MaterialTable gcode_outer = OrcaExt::resolved_table(table, int(OrcaExt::MaterialRecipe::OuterSpeed), false);
    CHECK(OrcaExt::find_material(gcode_outer, "PLA")->has_outer_speed);
    CHECK(! OrcaExt::find_material(gcode_outer, "PLA")->has_top_flow);

    // Nothing enabled still emits the first-layer flow, and nothing in the toolpath half.
    const OrcaExt::MaterialTable slice_none = OrcaExt::resolved_table(table, 0, true);
    CHECK(slice_none.empty());
    const OrcaExt::MaterialTable gcode_none = OrcaExt::resolved_table(table, 0, false);
    CHECK(OrcaExt::find_material(gcode_none, "PLA")->has_first_layer_flow);
    CHECK(! OrcaExt::find_material(gcode_none, "PLA")->has_outer_speed);
}
