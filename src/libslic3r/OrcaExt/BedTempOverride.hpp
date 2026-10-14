// [ORCAPORT FILE] BedTempOverride - bed-temperature readout and print-time offset
// Source: original (design in porting/notes/BT-1.md)
//
// The upload dialog offers an additive bed-temperature offset (+-5/10/15) so a plate
// temperature can be nudged right before a print. The value lives in the project config (like
// curr_bed_type, so it never dirties a preset), and every bed-temperature key is in the
// "G-code export only" invalidation bucket, so changing it re-exports the G-code and keeps the
// slice result: no re-slice, and no post-processing script.
//
// This header is the single place the offset is bounded and the single place the effective
// first-layer temperature is computed, so the G-code emission and the two readouts (the plate
// label and the dialog) can never disagree.
#ifndef slic3r_OrcaExt_BedTempOverride_hpp_
#define slic3r_OrcaExt_BedTempOverride_hpp_

#include <algorithm>
#include <string>
#include <vector>

#include "../PrintConfig.hpp"

namespace Slic3r {
namespace OrcaExt {

// The six dialog buttons reach +-15; the wider bound is defence, not UI.
constexpr int kBedTempOffsetMin = -30;
constexpr int kBedTempOffsetMax = 30;
// No per-printer bed ceiling reaches the slicer (MachineObject::get_bed_temperature_limit is
// network-printer-only), so base + offset is clamped into a generic printable range instead.
// Named with a k-prefix on purpose: DeviceManager.hpp already defines a BED_TEMP_LIMIT macro.
constexpr int kBedTempCeiling = 120;

inline int clamp_bed_temp_offset(int offset)
{
    return std::clamp(offset, kBedTempOffsetMin, kBedTempOffsetMax);
}

// Base plate temperature plus the dialog offset. A base of 0 is the slicer's "this filament
// does not support this plate" marker, so it is left untouched rather than nudged into a small
// positive temperature. A heated base is never allowed to reach 0: M140 S0 switches the bed off
// on most firmwares.
inline int apply_bed_temp_offset(int base_temp, int offset)
{
    if (base_temp <= 0)
        return base_temp;
    return std::clamp(base_temp + offset, 1, kBedTempCeiling);
}

// PartPlate::get_extruders() and the slice filaments are 1-based filament ids; the config arrays
// are 0-based. Convert once, dropping the "unset" 0/negative entries.
inline std::vector<int> to_zero_based_filaments(const std::vector<int> &one_based)
{
    std::vector<int> out;
    out.reserve(one_based.size());
    for (int f : one_based)
        if (f > 0)
            out.push_back(f - 1);
    return out;
}

// "degC" as explicit UTF-8 bytes so callers do not depend on the source/execution charset.
inline std::string bed_temp_unit_utf8()
{
    return "\xC2\xB0" "C";
}

// The first-layer plate temperature the slicer will command: the *_plate_temp_initial_layer of
// the first used filament, or the highest among the used filaments when bed_temperature_formula
// is "by highest temp" (the default, so a PLA+PETG plate runs at PETG's temperature). Mirrors
// GCode::get_highest_bed_temperature() so the readouts show what will actually be emitted.
// `filaments` are 0-based filament indices; an empty list means "nothing sliced yet", in which
// case every configured filament is considered.
inline int effective_first_layer_bed_temp(const DynamicPrintConfig &config, BedType bed_type,
                                          const std::vector<int> &filaments)
{
    const ConfigOptionInts *temps = config.option<ConfigOptionInts>(get_bed_temp_1st_layer_key(bed_type));
    if (temps == nullptr || temps->values.empty())
        return 0;

    const BedTempFormula formula = config.option<ConfigOptionEnum<BedTempFormula>>("bed_temperature_formula") != nullptr
        ? config.opt_enum<BedTempFormula>("bed_temperature_formula")
        : BedTempFormula::btfHighestTemp;

    int temp = 0;
    if (formula == BedTempFormula::btfHighestTemp) {
        for (int f : filaments)
            if (f >= 0 && f < (int) temps->values.size())
                temp = std::max(temp, (int) temps->get_at(f));
        if (filaments.empty())
            for (int t : temps->values)
                temp = std::max(temp, t);
    } else {
        const int f = filaments.empty() ? 0 : filaments.front();
        temp = (f >= 0 && f < (int) temps->values.size()) ? (int) temps->get_at(f) : 0;
    }
    return temp;
}

} // namespace OrcaExt
} // namespace Slic3r

#endif /* slic3r_OrcaExt_BedTempOverride_hpp_ */
