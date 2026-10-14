#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/BedTempOverride.hpp"

#include <vector>

using namespace Slic3r;

// The offset is what the upload dialog adds to the plate temperature. It must never turn a heated
// bed off (M140 S0) nor revive an unset plate temperature (0 = the filament is incompatible with
// the selected plate).
TEST_CASE("Bed temperature offset is added to the plate temperature and clamped", "[OrcaBedTemp]") {
    CHECK(OrcaExt::apply_bed_temp_offset(60, -15) == 45);
    CHECK(OrcaExt::apply_bed_temp_offset(60, 0) == 60);
    CHECK(OrcaExt::apply_bed_temp_offset(45, 15) == 60);
    CHECK(OrcaExt::apply_bed_temp_offset(0, 15) == 0);
    CHECK(OrcaExt::apply_bed_temp_offset(10, -15) == 1);
    CHECK(OrcaExt::apply_bed_temp_offset(120, 15) == OrcaExt::kBedTempCeiling);

    CHECK(OrcaExt::clamp_bed_temp_offset(-100) == OrcaExt::kBedTempOffsetMin);
    CHECK(OrcaExt::clamp_bed_temp_offset(100) == OrcaExt::kBedTempOffsetMax);
}

TEST_CASE("Effective first-layer bed temperature follows the bed temperature formula", "[OrcaBedTemp]") {
    DynamicPrintConfig cfg;
    // btPEI, the plate type whose first-layer key is hot_plate_temp_initial_layer
    cfg.set_key_value("hot_plate_temp_initial_layer", new ConfigOptionInts{50, 70});

    SECTION("a missing formula behaves as by-highest-temp") {
        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPEI, std::vector<int>{0, 1}) == 70);
    }

    SECTION("by-highest-temp uses the hottest printed filament") {
        cfg.set_key_value("bed_temperature_formula", new ConfigOptionEnum<BedTempFormula>(BedTempFormula::btfHighestTemp));

        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPEI, std::vector<int>{0}) == 50);
        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPEI, std::vector<int>{1}) == 70);
        // an empty list means nothing is sliced yet, so every configured filament counts
        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPEI, std::vector<int>{}) == 70);
    }

    SECTION("by-first-filament uses the first entry of the used list") {
        cfg.set_key_value("bed_temperature_formula", new ConfigOptionEnum<BedTempFormula>(BedTempFormula::btfFirstFilament));

        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPEI, std::vector<int>{0, 1}) == 50);
    }

    SECTION("a plate type with no configured temperature reads 0") {
        CHECK(OrcaExt::effective_first_layer_bed_temp(cfg, BedType::btPTE, std::vector<int>{0}) == 0);
    }
}

// PartPlate::get_extruders() reports 1-based filament ids (0 = unset); the config arrays are
// 0-based, so the two must not be confused when the temperature of a used filament is looked up.
TEST_CASE("Filament ids convert from the 1-based plate list to config indices", "[OrcaBedTemp]") {
    CHECK(OrcaExt::to_zero_based_filaments(std::vector<int>{1, 2}) == std::vector<int>{0, 1});
    CHECK(OrcaExt::to_zero_based_filaments(std::vector<int>{0, -1, 3}) == std::vector<int>{2});
    CHECK(OrcaExt::to_zero_based_filaments(std::vector<int>{}) == std::vector<int>{});
}
