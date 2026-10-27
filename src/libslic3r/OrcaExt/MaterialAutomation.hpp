// [ORCAPORT FILE] OrcaExt MaterialAutomation - per-material overrides for bridging, the top surface,
// the first layer and the outer wall
// Source: original (design in porting/notes/MA-1.md)
//
// One local table keyed by filament_type, edited in the print dialog, lets a material carry its own
// bridge/top-surface recipe instead of a separate process profile. The GUI resolves the table against
// the plate's enabled recipes into two plate-level keys: `material_automation` (toolpath inputs, so a
// change re-runs perimeters) and `material_automation_gcode` (E/F only, so a change re-exports G-code
// without re-slicing). This header owns the encoding, the ranges and the resolution, free of UI, so
// the behaviour is unit-tested rather than eyeballed.
//
// Keys are stored under the filament_type name because that is what a swap of a filament changes; a
// value is never written into a profile, so turning a recipe off restores the profile's own numbers.
#ifndef slic3r_OrcaExt_MaterialAutomation_hpp_
#define slic3r_OrcaExt_MaterialAutomation_hpp_

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <map>
#include <string>

#include "FirstLayerFlow.hpp" // clamp_first_layer_flow_delta

namespace Slic3r {
namespace OrcaExt {

// One bit per recipe. The plate owns the mask (project data); the table holds the numbers.
enum class MaterialRecipe : int {
    FreeBridging = 1 << 0, // external bridges only: density, flow, speed
    QualityTop   = 1 << 1, // top surface: flow, line width, speed
    OuterSpeed   = 1 << 2, // outer wall speed
};

constexpr int kAllMaterialRecipes =
    int(MaterialRecipe::FreeBridging) | int(MaterialRecipe::QualityTop) | int(MaterialRecipe::OuterSpeed);

inline bool material_recipe_enabled(int mask, MaterialRecipe recipe)
{
    return (mask & int(recipe)) != 0;
}

// Ranges mirror the option definitions in PrintConfig.cpp, so a hand-edited value is bounded the same
// way the settings page bounds it.
constexpr double kBridgeDensityMin = 10.0, kBridgeDensityMax = 125.0;
constexpr double kFlowRatioMin = 0.0, kFlowRatioMax = 2.0;
constexpr double kSpeedMin = 1.0, kSpeedMax = 1000.0;
constexpr double kLineWidthMmMax = 10.0, kLineWidthPercentMax = 1000.0;
constexpr double kTopWidthDefaultMin = 0.0;

inline double clamp_material_bridge_density(double v) { return std::clamp(v, kBridgeDensityMin, kBridgeDensityMax); }
inline double clamp_material_flow(double v) { return std::clamp(v, kFlowRatioMin, kFlowRatioMax); }
inline double clamp_material_speed(double v) { return std::clamp(v, kSpeedMin, kSpeedMax); }
inline double clamp_material_line_width(double v, bool percent)
{
    return std::clamp(v, kTopWidthDefaultMin, percent ? kLineWidthPercentMax : kLineWidthMmMax);
}

// What one material sets. `has_*` says whether the user filled the field in: an unset field leaves
// the profile's value alone, which is what keeps the table empty-at-default a no-op.
struct MaterialValues
{
    bool   has_bridge_density {false};
    double bridge_density {100.}; // percent
    bool   has_bridge_flow {false};
    double bridge_flow {1.};
    bool   has_bridge_speed {false};
    double bridge_speed {0.}; // mm/s
    bool   has_top_flow {false};
    double top_flow {1.};
    bool   has_top_width {false};
    double top_width {0.};
    bool   top_width_percent {false}; // "%" of the nozzle diameter, else mm
    bool   has_top_speed {false};
    double top_speed {0.}; // mm/s
    bool   has_outer_speed {false};
    double outer_speed {0.}; // mm/s
    bool   has_first_layer_flow {false};
    double first_layer_flow {0.}; // percent delta, -50..0

    bool empty() const
    {
        return ! has_bridge_density && ! has_bridge_flow && ! has_bridge_speed && ! has_top_flow &&
               ! has_top_width && ! has_top_speed && ! has_outer_speed && ! has_first_layer_flow;
    }

    bool has_slice_fields() const { return has_bridge_density || has_bridge_flow || has_top_width; }
};

// Keyed by filament_type. A map keeps the pointers returned by lookups stable while it lives.
using MaterialTable = std::map<std::string, MaterialValues>;

inline std::string normalize_material(std::string name)
{
    while (! name.empty() && std::isspace(static_cast<unsigned char>(name.front())))
        name.erase(name.begin());
    while (! name.empty() && std::isspace(static_cast<unsigned char>(name.back())))
        name.pop_back();
    std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return char(std::toupper(c)); });
    return name;
}

inline const MaterialValues *find_material(const MaterialTable &table, const std::string &filament_type)
{
    if (filament_type.empty())
        return nullptr;
    auto it = table.find(normalize_material(filament_type));
    return it == table.end() ? nullptr : &it->second;
}

// The plate stores two strings. The slice key keeps only the toolpath inputs of the enabled recipes;
// the G-code key keeps the E/F inputs. The first-layer flow has no recipe: it applies per material as
// soon as the material has a value, which keeps the plate's own first-layer automation intact.
inline MaterialValues resolved_values(const MaterialValues &in, int recipes, bool slice_key)
{
    MaterialValues out;
    if (slice_key) {
        if (material_recipe_enabled(recipes, MaterialRecipe::FreeBridging)) {
            out.has_bridge_density   = in.has_bridge_density;
            out.bridge_density       = in.bridge_density;
            out.has_bridge_flow      = in.has_bridge_flow;
            out.bridge_flow          = in.bridge_flow;
        }
        if (material_recipe_enabled(recipes, MaterialRecipe::QualityTop)) {
            out.has_top_width   = in.has_top_width;
            out.top_width       = in.top_width;
            out.top_width_percent = in.top_width_percent;
        }
    } else {
        if (material_recipe_enabled(recipes, MaterialRecipe::FreeBridging)) {
            out.has_bridge_speed = in.has_bridge_speed;
            out.bridge_speed     = in.bridge_speed;
        }
        if (material_recipe_enabled(recipes, MaterialRecipe::QualityTop)) {
            out.has_top_flow  = in.has_top_flow;
            out.top_flow      = in.top_flow;
            out.has_top_speed = in.has_top_speed;
            out.top_speed     = in.top_speed;
        }
        if (material_recipe_enabled(recipes, MaterialRecipe::OuterSpeed)) {
            out.has_outer_speed = in.has_outer_speed;
            out.outer_speed     = in.outer_speed;
        }
        out.has_first_layer_flow = in.has_first_layer_flow;
        out.first_layer_flow     = in.first_layer_flow;
    }
    return out;
}

inline MaterialTable resolved_table(const MaterialTable &table, int recipes, bool slice_key)
{
    MaterialTable out;
    for (const auto &[name, values] : table) {
        MaterialValues resolved = resolved_values(values, recipes, slice_key);
        if (! resolved.empty())
            out.emplace(name, resolved);
    }
    return out;
}

// "PLA:bd=120;bf=1.05|PETG:bd=125" - materials separated by '|', fields by ';', name and value by
// '='. Tolerant on read: unknown fields, unknown materials are dropped, numbers are clamped, so a
// hand-edited or stale value can never crash the dialog or the slicer.
inline std::string format_number(double value)
{
    // Trim trailing zeros so the stored string is stable across a save/load round trip.
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "%.4f", value);
    std::string text(buffer);
    while (text.size() > 1 && text.back() == '0' && text[text.size() - 2] != '.')
        text.pop_back();
    if (! text.empty() && text.back() == '.')
        text.pop_back();
    return text;
}

inline std::string serialize(const MaterialTable &table)
{
    std::string out;
    for (const auto &[name, values] : table) {
        if (values.empty())
            continue;
        std::string row;
        auto        add = [&row](const char *key, double value) {
            if (! row.empty())
                row += ';';
            row += key;
            row += '=';
            row += format_number(value);
        };
        if (values.has_bridge_density) add("bd", clamp_material_bridge_density(values.bridge_density));
        if (values.has_bridge_flow) add("bf", clamp_material_flow(values.bridge_flow));
        if (values.has_bridge_speed) add("bs", clamp_material_speed(values.bridge_speed));
        if (values.has_top_flow) add("tf", clamp_material_flow(values.top_flow));
        if (values.has_top_width) {
            if (! row.empty())
                row += ';';
            row += "tw=";
            row += format_number(clamp_material_line_width(values.top_width, values.top_width_percent));
            if (values.top_width_percent)
                row += '%';
        }
        if (values.has_top_speed) add("ts", clamp_material_speed(values.top_speed));
        if (values.has_outer_speed) add("os", clamp_material_speed(values.outer_speed));
        if (values.has_first_layer_flow) add("fl", clamp_first_layer_flow_delta(values.first_layer_flow));
        if (row.empty())
            continue;
        if (! out.empty())
            out += '|';
        out += normalize_material(name);
        out += ':';
        out += row;
    }
    return out;
}

inline MaterialTable parse(const std::string &text)
{
    MaterialTable out;
    size_t        pos = 0;
    while (pos < text.size()) {
        const size_t row_end = text.find('|', pos);
        const std::string row = text.substr(pos, row_end == std::string::npos ? std::string::npos : row_end - pos);
        pos = row_end == std::string::npos ? text.size() : row_end + 1;

        const size_t colon = row.find(':');
        if (colon == std::string::npos)
            continue;
        const std::string name = normalize_material(row.substr(0, colon));
        if (name.empty())
            continue;

        MaterialValues values;
        size_t         field_pos = colon + 1;
        while (field_pos < row.size()) {
            const size_t field_end = row.find(';', field_pos);
            const std::string field =
                row.substr(field_pos, field_end == std::string::npos ? std::string::npos : field_end - field_pos);
            field_pos = field_end == std::string::npos ? row.size() : field_end + 1;

            const size_t equals = field.find('=');
            if (equals == std::string::npos)
                continue;
            const std::string key   = field.substr(0, equals);
            std::string       value = field.substr(equals + 1);
            const bool        percent = ! value.empty() && value.back() == '%';
            if (percent)
                value.pop_back();
            double number = 0.;
            try {
                number = std::stod(value);
            } catch (...) {
                continue; // malformed number: drop the field, keep the row
            }
            if (! std::isfinite(number))
                continue;

            if (key == "bd") {
                values.has_bridge_density = true;
                values.bridge_density     = clamp_material_bridge_density(number);
            } else if (key == "bf") {
                values.has_bridge_flow = true;
                values.bridge_flow     = clamp_material_flow(number);
            } else if (key == "bs") {
                values.has_bridge_speed = true;
                values.bridge_speed     = clamp_material_speed(number);
            } else if (key == "tf") {
                values.has_top_flow = true;
                values.top_flow     = clamp_material_flow(number);
            } else if (key == "tw") {
                values.has_top_width      = true;
                values.top_width          = clamp_material_line_width(number, percent);
                values.top_width_percent  = percent;
            } else if (key == "ts") {
                values.has_top_speed = true;
                values.top_speed     = clamp_material_speed(number);
            } else if (key == "os") {
                values.has_outer_speed = true;
                values.outer_speed     = clamp_material_speed(number);
            } else if (key == "fl") {
                values.has_first_layer_flow = true;
                values.first_layer_flow     = clamp_first_layer_flow_delta(number);
            }
        }
        if (! values.empty())
            out[name] = values;
    }
    return out;
}

} // namespace OrcaExt
} // namespace Slic3r

#endif /* slic3r_OrcaExt_MaterialAutomation_hpp_ */
