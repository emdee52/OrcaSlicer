// [ORCAPORT FILE] PlateAutomation - the local per-plate-type first-layer flow defaults
// Source: original (design in porting/notes/PA-1.md)
//
// The upload dialog's "First layer flow" row follows a per-plate-type default that is personal and
// machine-local: it lives in AppConfig, never in a preset and never in a 3mf, so a downloaded project
// or an updated vendor profile cannot carry or clobber it. A value chosen in the print dialog is a
// per-project override and takes precedence (see PrintHostDialogs).
//
// This header owns the map and its AppConfig encoding, free of UI, so the behaviour is unit-tested
// rather than eyeballed. The flow math itself lives in FirstLayerFlow.hpp.
#ifndef slic3r_OrcaExt_PlateAutomation_hpp_
#define slic3r_OrcaExt_PlateAutomation_hpp_

#include <cstdlib>
#include <map>
#include <string>

#include "../PrintConfig.hpp"
#include "FirstLayerFlow.hpp" // clamp_first_layer_flow_delta

namespace Slic3r {
namespace OrcaExt {

// Keyed by the BedType's integer value (an unknown or future type is simply an unused row). The map
// starts empty: with nothing configured, the row shows whatever the project stored.
using PlateFlowDefaults = std::map<int, double>; // percent, -50..0

inline bool valid_bed_type_key(int key)
{
    return key > int(BedType::btDefault) && key < int(BedType::btCount);
}

inline double flow_default_for(const PlateFlowDefaults &defaults, BedType bed_type)
{
    auto it = defaults.find(int(bed_type));
    return it == defaults.end() ? 0. : clamp_first_layer_flow_delta(it->second);
}

// True when the user configured the flow automation for this plate type, even at 0 ("print at the
// profile's flow"). Only a configured type lets the automation own the row; otherwise an older
// per-project value keeps showing, so projects saved before this feature do not lose it.
inline bool has_flow_default(const PlateFlowDefaults &defaults, BedType bed_type)
{
    return defaults.find(int(bed_type)) != defaults.end();
}

// "0:-20;2:-12" (BedType:percent). A configured 0 is kept: it is what makes the plate's own
// automation override an older per-project value instead of falling back to it.
inline std::string serialize_flow_defaults(const PlateFlowDefaults &defaults)
{
    std::string out;
    for (const auto &[key, percent] : defaults) {
        if (! valid_bed_type_key(key))
            continue;
        if (! out.empty())
            out += ';';
        out += std::to_string(key) + ':' + std::to_string(clamp_first_layer_flow_delta(percent));
    }
    return out;
}

// Tolerant: unknown keys, unparsable numbers and empty entries are dropped, values are clamped, so a
// hand-edited or stale value can never crash the dialog.
inline PlateFlowDefaults parse_flow_defaults(const std::string &text)
{
    PlateFlowDefaults out;
    size_t            pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find(';', pos);
        const std::string entry = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        const size_t colon = entry.find(':');
        if (colon == std::string::npos)
            continue;
        try {
            const int    key     = std::stoi(entry.substr(0, colon));
            const double percent = std::stod(entry.substr(colon + 1));
            if (! valid_bed_type_key(key))
                continue;
            out[key] = clamp_first_layer_flow_delta(percent);
        } catch (...) {
            // Malformed entry: ignore it and keep the rest.
        }
    }
    return out;
}

} // namespace OrcaExt
} // namespace Slic3r

#endif /* slic3r_OrcaExt_PlateAutomation_hpp_ */
