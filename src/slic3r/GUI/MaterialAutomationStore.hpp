// [ORCAPORT FILE] MaterialAutomationStore - AppConfig side of the local per-material table
// Source: original (design in porting/notes/MA-1.md)
//
// One [app] key. Deliberately not a preset or project key: a downloaded 3mf or an updated vendor
// profile can neither read nor reset it. The plate keeps the *resolved* values, so a project still
// slices identically on a machine whose table differs.
#pragma once

#include <string>

#include "libslic3r/AppConfig.hpp"
#include "libslic3r/OrcaExt/MaterialAutomation.hpp"

namespace Slic3r {
namespace GUI {

inline constexpr const char *MATERIAL_TABLE_KEY = "orca_ext_material_automation";

inline OrcaExt::MaterialTable load_material_table(const AppConfig &config)
{
    return OrcaExt::parse(config.get("app", MATERIAL_TABLE_KEY));
}

inline void save_material_table(AppConfig &config, const OrcaExt::MaterialTable &table)
{
    config.set("app", MATERIAL_TABLE_KEY, OrcaExt::serialize(table));
}

} // namespace GUI
} // namespace Slic3r
