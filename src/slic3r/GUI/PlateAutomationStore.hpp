// [ORCAPORT FILE] PlateAutomationStore - AppConfig side of the local plate flow defaults
// Source: original (design in porting/notes/PA-1.md)
//
// One [app] key. It is deliberately not a print/project key: a preset, a printer profile or a 3mf
// downloaded from anywhere cannot read or write it, and updating a vendor profile cannot reset it.
#pragma once

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"

namespace Slic3r {
namespace GUI {

inline constexpr const char *PLATE_FLOW_DEFAULTS_KEY = "first_layer_flow_plates";

inline OrcaExt::PlateFlowDefaults load_plate_flow_defaults(const AppConfig &config)
{
    return OrcaExt::parse_flow_defaults(config.get("app", PLATE_FLOW_DEFAULTS_KEY));
}

inline void save_plate_flow_defaults(AppConfig &config, const OrcaExt::PlateFlowDefaults &defaults)
{
    config.set("app", PLATE_FLOW_DEFAULTS_KEY, OrcaExt::serialize_flow_defaults(defaults));
}

} // namespace GUI
} // namespace Slic3r
