#include <catch2/catch_all.hpp>

#include "test_helpers.hpp"

#include "libslic3r/GCode.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

struct Move
{
    std::string role;
    double      f {0.};
    double      e {0.};
    int         layer {0};
};

// G1 moves that extrude, tagged with the extrusion role and the layer they belong to. F is a sticky
// modal value in G-code, so each move carries the last F seen.
std::vector<Move> extruding_moves(const std::string &gcode)
{
    std::vector<Move> out;
    std::string       role;
    double            f     = 0.;
    int               layer = -1;
    size_t            pos   = 0;
    while (pos < gcode.size()) {
        size_t end = gcode.find('\n', pos);
        if (end == std::string::npos)
            end = gcode.size();
        const std::string line = gcode.substr(pos, end - pos);
        pos = end + 1;

        if (line == ";LAYER_CHANGE") {
            ++layer;
        } else if (line.rfind(";TYPE:", 0) == 0) {
            role = line.substr(6);
        } else if (line.rfind("G1 ", 0) == 0) {
            if (const size_t f_pos = line.find('F'); f_pos != std::string::npos) {
                try {
                    f = std::stod(line.substr(f_pos + 1));
                } catch (...) {
                }
            }
            Move move;
            move.role  = role;
            move.layer = layer;
            move.f     = f;
            if (const size_t e_pos = line.find('E'); e_pos != std::string::npos) {
                try {
                    move.e = std::stod(line.substr(e_pos + 1));
                } catch (...) {
                    continue;
                }
            }
            if (std::abs(move.e) > 1e-9)
                out.push_back(move);
        }
    }
    return out;
}

// Cooling and the volumetric cap may lower moves, so the highest F in a role is the one the recipe
// asked for (the cap is disabled in the fixture below).
double max_feed(const std::vector<Move> &moves, const std::string &role)
{
    double out = 0.;
    for (const Move &move : moves)
        if (move.role == role)
            out = std::max(out, move.f);
    return out;
}

// A deterministic print: no cooling-driven slowdown, no overhang slowdown and no volumetric cap, so a
// speed recipe is the only thing that can move F. The filament is PLA, which is what the table keys
// are written against.
DynamicPrintConfig ma_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_key_value("filament_type", new ConfigOptionStrings{"PLA"});
    config.set_key_value("slow_down_for_layer_cooling", new ConfigOptionBools{false});
    config.set_key_value("slow_down_layers", new ConfigOptionInt(0));
    config.set_key_value("enable_overhang_speed", new ConfigOptionBools{false});
    config.set_key_value("slowdown_for_curled_perimeters", new ConfigOptionBools{false});
    config.set_key_value("filament_max_volumetric_speed", new ConfigOptionFloats{0.});
    return config;
}

} // namespace

// The E/F half of the table: a material's speeds and first-layer reduction replace the profile's at
// export time, and the change re-exports the G-code without invalidating a slice step.
TEST_CASE("Material recipes drive speeds and the first layer at export only", "[OrcaMaterialAutomation]") {
    Print              print;
    Model              model;
    DynamicPrintConfig config = ma_config();
    Test::init_print({Test::cube(10.)}, print, model, config);

    const std::vector<Move> baseline = extruding_moves(Test::gcode(print));
    REQUIRE(! baseline.empty());
    const double plain_top  = max_feed(baseline, "Top surface");
    const double plain_wall = max_feed(baseline, "Outer wall");
    REQUIRE(plain_top > 0.);
    REQUIRE(plain_wall > 0.);
    CAPTURE(plain_top, plain_wall);
    CHECK(plain_top != 33. * 60.);
    CHECK(plain_wall != 44. * 60.);

    // A key for a material the plate does not use must not touch anything.
    DynamicPrintConfig other_material = config;
    other_material.set_key_value("material_automation_gcode", new ConfigOptionString("PETG:ts=33;os=44;fl=-10"));
    print.apply(model, other_material);
    const std::vector<Move> untouched = extruding_moves(Test::gcode(print));
    REQUIRE(untouched.size() == baseline.size());
    for (size_t i = 0; i < baseline.size(); ++i) {
        CHECK(untouched[i].f == baseline[i].f);
        CHECK(untouched[i].e == baseline[i].e);
    }

    DynamicPrintConfig recipe = config;
    recipe.set_key_value("material_automation_gcode", new ConfigOptionString("PLA:ts=33;os=44;fl=-10"));
    const PrintBase::ApplyStatus status = print.apply(model, recipe);
    CHECK(status == PrintBase::APPLY_STATUS_INVALIDATED);
    // Only the export is invalidated: the slice result is reused.
    REQUIRE(print.objects().size() == 1);
    CHECK(print.objects().front()->is_step_done(posSlice));
    CHECK(print.objects().front()->is_step_done(posInfill));

    const std::vector<Move> changed = extruding_moves(Test::gcode(print));
    CHECK_THAT(max_feed(changed, "Top surface"), WithinAbs(33. * 60., 1.));
    CHECK_THAT(max_feed(changed, "Outer wall"), WithinAbs(44. * 60., 1.));

    // The first-layer reduction scales the E of the first layer's bottom surface only.
    REQUIRE(changed.size() == baseline.size());
    size_t first_layer_samples = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        CAPTURE(i, baseline[i].role, baseline[i].layer);
        if (baseline[i].role == "Bottom surface" && baseline[i].layer == 0) {
            ++first_layer_samples;
            CHECK_THAT(changed[i].e / baseline[i].e, WithinRel(0.9, 1e-3));
        } else {
            CHECK(changed[i].e == baseline[i].e);
        }
    }
    CHECK(first_layer_samples > 0);
}

namespace {

// The top-surface extrusion width the region resolves for this object.
double top_surface_width(const Print &print, const PrintObject &object)
{
    return print.get_print_region(0).flow(object, frTopSolidInfill, object.config().layer_height).width();
}

} // namespace

// The toolpath half: the top-surface width is a slice input, so it must invalidate perimeters (not a
// full re-slice of the meshes) and reach the region's resolved flow.
TEST_CASE("A toolpath recipe re-runs perimeters and changes the top-surface width", "[OrcaMaterialAutomation]") {
    Print              print;
    Model              model;
    DynamicPrintConfig config = ma_config();
    Test::init_print({Test::cube(10.)}, print, model, config);

    const std::vector<Move> baseline = extruding_moves(Test::gcode(print));
    REQUIRE(! baseline.empty());
    const double plain_width = top_surface_width(print, *print.objects().front());
    REQUIRE(plain_width > 0.);

    DynamicPrintConfig widened = config;
    widened.set_key_value("material_automation", new ConfigOptionString("PLA:tw=120%"));
    const PrintBase::ApplyStatus status = print.apply(model, widened);
    CHECK(status == PrintBase::APPLY_STATUS_INVALIDATED);
    REQUIRE(print.objects().size() == 1);
    // A wider line, and the mesh is not re-sliced.
    CHECK(print.objects().front()->is_step_done(posSlice));
    CHECK(! print.objects().front()->is_step_done(posPerimeters));
    const double wide_width = top_surface_width(print, *print.objects().front());
    CHECK(wide_width > plain_width);

    // Back to an empty table: the profile's own values, exactly as before the recipe.
    DynamicPrintConfig empty = config;
    empty.set_key_value("material_automation", new ConfigOptionString(""));
    empty.set_key_value("material_automation_gcode", new ConfigOptionString(""));
    print.apply(model, empty);
    const std::vector<Move> restored = extruding_moves(Test::gcode(print));
    REQUIRE(restored.size() == baseline.size());
    for (size_t i = 0; i < baseline.size(); ++i) {
        CHECK(restored[i].f == baseline[i].f);
        CHECK(restored[i].e == baseline[i].e);
    }
}
