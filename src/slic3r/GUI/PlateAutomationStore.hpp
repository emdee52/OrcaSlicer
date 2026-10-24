// [ORCAPORT FILE] PlateAutomationStore - AppConfig side of the local plate automation
// Source: original (design in porting/notes/PA-1.md)
//
// Two [app] keys, one per automation. They are deliberately not print/project keys: a preset, a
// printer profile or a 3mf downloaded from anywhere cannot read or write them, and updating a vendor
// profile cannot reset them.
#pragma once

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/OrcaExt/PlateAutomation.hpp"

namespace Slic3r {
namespace GUI {

inline constexpr const char *PLATE_FLOW_DEFAULTS_KEY     = "first_layer_flow_plates";
inline constexpr const char *PLATE_BED_TEMP_DEFAULTS_KEY = "first_layer_bed_temp_buckets";

inline OrcaExt::PlateFlowDefaults load_plate_flow_defaults(const AppConfig &config)
{
    return OrcaExt::parse_flow_defaults(config.get("app", PLATE_FLOW_DEFAULTS_KEY));
}

inline void save_plate_flow_defaults(AppConfig &config, const OrcaExt::PlateFlowDefaults &defaults)
{
    config.set("app", PLATE_FLOW_DEFAULTS_KEY, OrcaExt::serialize_flow_defaults(defaults));
}

inline OrcaExt::PlateBedTempDefaults load_plate_bed_temp_defaults(const AppConfig &config)
{
    return OrcaExt::parse_bed_temp_defaults(config.get("app", PLATE_BED_TEMP_DEFAULTS_KEY));
}

inline void save_plate_bed_temp_defaults(AppConfig &config, const OrcaExt::PlateBedTempDefaults &defaults)
{
    config.set("app", PLATE_BED_TEMP_DEFAULTS_KEY, OrcaExt::serialize_bed_temp_defaults(defaults));
}

} // namespace GUI
} // namespace Slic3r
