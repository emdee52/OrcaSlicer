#include <catch2/catch_all.hpp>

#include "test_helpers.hpp"

#include "libslic3r/GCode.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <cmath>
#include <string>
#include <utility>
#include <vector>

using namespace Slic3r;
using Catch::Matchers::WithinRel;

namespace {

struct FlowSample { int layer; double e; };

// E values of the G1 moves whose extrusion role is "Bottom surface", tagged with the layer they
// belong to (0-based, counted from the ;LAYER_CHANGE markers).
std::vector<FlowSample> bottom_surface_flow(const std::string &gcode)
{
    std::vector<FlowSample> out;
    int    layer     = -1;
    bool   in_bottom = false;
    size_t pos       = 0;
    while (pos < gcode.size()) {
        size_t end = gcode.find('\n', pos);
        if (end == std::string::npos)
            end = gcode.size();
        const std::string line = gcode.substr(pos, end - pos);
        pos = end + 1;

        if (line == ";LAYER_CHANGE") {
            ++layer;
            in_bottom = false;
        } else if (line.rfind(";TYPE:", 0) == 0) {
            in_bottom = line == ";TYPE:Bottom surface";
        } else if (in_bottom && line.rfind("G1 ", 0) == 0) {
            const size_t e_pos = line.rfind('E');
            if (e_pos != std::string::npos) {
                const double e = std::stod(line.substr(e_pos + 1));
                if (std::abs(e) > 1e-9)
                    out.push_back({layer, e});
            }
        }
    }
    return out;
}

} // namespace

// The rows in the upload dialog only ever lower the flow of what sits on the plate. Layers 2+ keep
// the profile's bottom surface flow, and no coordinate moves.
TEST_CASE("First-layer flow reduction scales only the first layer's bottom surface extrusion", "[OrcaFirstLayerFlow]") {
    Print                    print;
    Model                    model;
    DynamicPrintConfig       config = DynamicPrintConfig::full_print_config();
    Test::init_print({Test::cube(10.)}, print, model, config);

    const std::string            plain    = Test::gcode(print);
    const std::vector<FlowSample> baseline = bottom_surface_flow(plain);
    REQUIRE(!baseline.empty());

    DynamicPrintConfig reduced = config;
    reduced.set_key_value("initial_layer_bottom_surface_flow_delta", new ConfigOptionFloat(-10.));
    print.apply(model, reduced);
    const std::vector<FlowSample> changed = bottom_surface_flow(Test::gcode(print));

    // The same paths, in the same order: the reduction scales the E value, never the geometry.
    REQUIRE(changed.size() == baseline.size());
    size_t first_layer_samples = 0;
    for (size_t i = 0; i < baseline.size(); ++i) {
        CAPTURE(i, baseline[i].layer);
        REQUIRE(changed[i].layer == baseline[i].layer);
        if (baseline[i].layer == 0) {
            ++first_layer_samples;
            CHECK_THAT(changed[i].e / baseline[i].e, WithinRel(0.9, 1e-3));
        } else {
            CHECK(changed[i].e == baseline[i].e);
        }
    }
    CHECK(first_layer_samples > 0);

    // The auto flag is the dialog's business: it decides what the row preselects and writes the
    // delta, so on its own it must not move a single E value (the default bed type is a cool plate,
    // so an emission-time default would show up right here).
    DynamicPrintConfig manual = config;
    manual.set_key_value("initial_layer_bottom_surface_flow_auto", new ConfigOptionBool(false));
    print.apply(model, manual);
    const std::vector<FlowSample> manual_flow = bottom_surface_flow(Test::gcode(print));
    REQUIRE(manual_flow.size() == baseline.size());
    for (size_t i = 0; i < baseline.size(); ++i)
        CHECK(manual_flow[i].e == baseline[i].e);
}

// The reduction is a G-code-export-only setting: the upload dialog writes it after slicing, and
// the export must rerun without invalidating a single slice step. This is the "no re-slice" claim.
TEST_CASE("First-layer flow reduction invalidates only G-code export", "[OrcaFirstLayerFlow]") {
    Print              print;
    Model              model;
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    Test::init_print({Test::cube(10.)}, print, model, config);

    // A full export marks every step done, so an invalidated step is visible afterwards.
    Test::gcode(print);
    REQUIRE(print.objects().size() == 1);
    const PrintObject *object = print.objects().front();
    REQUIRE(print.is_step_done(psGCodeExport));
    REQUIRE(object->is_step_done(posSlice));
    REQUIRE(object->is_step_done(posInfill));

    DynamicPrintConfig reduced = config;
    reduced.set_key_value("initial_layer_bottom_surface_flow_delta", new ConfigOptionFloat(-10.));
    // The dialog writes both keys on OK, so the apply carries both.
    reduced.set_key_value("initial_layer_bottom_surface_flow_auto", new ConfigOptionBool(false));
    const PrintBase::ApplyStatus status = print.apply(model, reduced);

    CHECK(status == PrintBase::APPLY_STATUS_INVALIDATED);
    CHECK_FALSE(print.is_step_done(psGCodeExport));
    CHECK(object->is_step_done(posSlice));
    CHECK(object->is_step_done(posPerimeters));
    CHECK(object->is_step_done(posInfill));
}
