// [ORCAPORT FILE] PlateAutomation - local, per-plate-type automation for the first layer
// Source: original (design in porting/notes/PA-1.md)
//
// Two things the upload dialog otherwise asks the user to repeat by hand:
//
//   - a reduction of the first layer's bottom surface flow, per plate type (PA-1/BF-2), and
//   - a bed-temperature offset chosen from the size of the largest part on the plate,
//     per plate type (PA-1/BT-2).
//
// Both are personal, machine-local preferences: they live in AppConfig, never in a preset and never
// in a 3mf, so a downloaded project or an updated vendor profile cannot carry or clobber them. A
// value chosen in the print dialog is a per-project override and takes precedence.
//
// This header owns the data model (buckets, maps, AppConfig encoding) and the classification, all
// free of UI, so the behaviour is unit-tested rather than eyeballed.
#ifndef slic3r_OrcaExt_PlateAutomation_hpp_
#define slic3r_OrcaExt_PlateAutomation_hpp_

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdlib>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "../Model.hpp"
#include "../PrintConfig.hpp"
#include "BedTempOverride.hpp"  // clamp_bed_temp_offset
#include "FirstLayerFlow.hpp"   // clamp_first_layer_flow_delta

namespace Slic3r {
namespace OrcaExt {

// Size buckets, smallest first. Warping is a height problem first and an area problem second: a thin
// part has no lever arm and no gradient across its thickness, so it never needs bed heat, while a tall
// part accumulates enough contraction to lift a large footprint. The classifier is therefore a
// height x footprint matrix, and `Tall` is decided by height alone.
enum class PartSizeBucket : int {
    Small = 0,
    SmallMedium,
    Medium,
    MediumLarge,
    Large,
    Tall,
    Count
};
constexpr int kPartSizeBucketCount = int(PartSizeBucket::Count);

// Height bands, in mm: under kThinPartHeightMm nothing warps; from there to kTallPartHeightMm the
// footprint bands decide. The 20 and 50 mm edges are the interior rows of the matrix.
constexpr double kThinPartHeightMm = 3.0;
constexpr double kMidPartHeightMm  = 20.0;
constexpr double kHighPartHeightMm = 50.0;
constexpr double kTallPartHeightMm = 100.0;
// Footprint columns, as a percentage of the plate's area (percentages so they travel to another bed
// size): under 3%, 3-15%, over 15%.
constexpr std::array<double, 2> kPartSizeAreaPctCuts {3.0, 15.0};

// Editable bands. The six numbers are saved in AppConfig, so they can be tuned at runtime instead of
// rebuilt; the defaults are the values above. Keep them ascending - valid_part_size_limits() is the
// gate, and a stored value that fails it falls back to the defaults rather than mis-classifying.
struct PartSizeLimits {
    double fp_small_pct {kPartSizeAreaPctCuts[0]};
    double fp_mid_pct {kPartSizeAreaPctCuts[1]};
    double h_thin_mm {kThinPartHeightMm};
    double h_mid_mm {kMidPartHeightMm};
    double h_high_mm {kHighPartHeightMm};
    double h_tall_mm {kTallPartHeightMm};
};

constexpr double kMinFootprintPct = 0.1;
constexpr double kMaxFootprintPct = 100.0;
constexpr double kMinHeightMm     = 0.1;
constexpr double kMaxHeightMm     = 500.0;

inline bool valid_part_size_limits(const PartSizeLimits &limits)
{
    if (!(limits.fp_small_pct >= kMinFootprintPct && limits.fp_small_pct < limits.fp_mid_pct
          && limits.fp_mid_pct <= kMaxFootprintPct))
        return false;
    return limits.h_thin_mm >= kMinHeightMm && limits.h_thin_mm < limits.h_mid_mm
        && limits.h_mid_mm < limits.h_high_mm && limits.h_high_mm < limits.h_tall_mm
        && limits.h_tall_mm <= kMaxHeightMm;
}

// "3,15;3,20,50,100" - footprint percentages, then height rows in mm.
inline std::string serialize_part_size_limits(const PartSizeLimits &limits)
{
    auto number = [](double value) {
        std::string text = std::to_string(value); // 3.000000
        text.erase(text.find_last_not_of('0') + 1);
        if (! text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    };
    return number(limits.fp_small_pct) + "," + number(limits.fp_mid_pct) + ";" + number(limits.h_thin_mm)
        + "," + number(limits.h_mid_mm) + "," + number(limits.h_high_mm) + "," + number(limits.h_tall_mm);
}

// Tolerant: anything malformed, out of order or out of range yields the defaults.
inline PartSizeLimits parse_part_size_limits(const std::string &text)
{
    PartSizeLimits limits;
    const size_t   split = text.find(';');
    if (split == std::string::npos)
        return limits;

    auto read_list = [](const std::string &list, std::vector<double> &out) {
        size_t pos = 0;
        while (pos <= list.size()) {
            const size_t      end = list.find(',', pos);
            const std::string item = list.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
            try {
                out.push_back(std::stod(item));
            } catch (...) {
                return;
            }
            if (end == std::string::npos)
                break;
            pos = end + 1;
        }
    };

    std::vector<double> fp;
    std::vector<double> heights;
    read_list(text.substr(0, split), fp);
    read_list(text.substr(split + 1), heights);
    if (fp.size() != 2 || heights.size() != 4)
        return limits;

    limits.fp_small_pct = fp[0];
    limits.fp_mid_pct   = fp[1];
    limits.h_thin_mm    = heights[0];
    limits.h_mid_mm     = heights[1];
    limits.h_high_mm    = heights[2];
    limits.h_tall_mm    = heights[3];
    return valid_part_size_limits(limits) ? limits : PartSizeLimits {};
}

// The bucket a single part falls in.
//
//              footprint:  under 3%       3-15%          over 15%
//   under 3 mm             Small          Small          Small
//   3-20 mm                Small          Small-medium   Small-medium
//   20-50 mm               Medium         Medium         Medium-large
//   50-100 mm              Medium-large   Large          Large
//   100 mm and up          Tall           Tall           Tall
//
// `bed_area_mm2` is the plate's own area; a plate with no usable area falls back to Medium rather
// than guessing an extreme. The bands above are the `limits` defaults.
inline PartSizeBucket classify_part_size(double footprint_mm2, double bed_area_mm2, double height_mm,
                                        const PartSizeLimits &limits = PartSizeLimits {})
{
    if (height_mm < limits.h_thin_mm)
        return PartSizeBucket::Small;
    if (height_mm >= limits.h_tall_mm)
        return PartSizeBucket::Tall;
    if (!(bed_area_mm2 > 0.) || !(footprint_mm2 > 0.))
        return PartSizeBucket::Medium;

    const double pct        = 100. * footprint_mm2 / bed_area_mm2;
    const bool   small_area = pct < limits.fp_small_pct;
    const bool   mid_area   = pct <= limits.fp_mid_pct;

    if (height_mm < limits.h_mid_mm)
        return small_area ? PartSizeBucket::Small : PartSizeBucket::SmallMedium;
    if (height_mm < limits.h_high_mm)
        return mid_area ? PartSizeBucket::Medium : PartSizeBucket::MediumLarge;
    // The top row before Tall: a small footprint is a slender part, which lifts sooner than a broad
    // one.
    return small_area ? PartSizeBucket::MediumLarge : PartSizeBucket::Large;
}

// The bucket that decides the plate's bed temperature: the highest any part lands in, because the
// temperature serves the whole plate. A first-layer test next to a real part gets the real part's
// bucket, and a big flat patch no longer outvotes a tall one. `deciding_*` report the part that set
// the bucket, for the popup's readout.
inline PartSizeBucket plate_part_bucket(const std::vector<std::pair<double, double>> &footprints, double bed_area_mm2,
                                        const PartSizeLimits &limits = PartSizeLimits {},
                                        double *deciding_height_mm = nullptr, double *deciding_pct = nullptr)
{
    PartSizeBucket bucket = PartSizeBucket::Small;
    for (const auto &[footprint, height] : footprints) {
        const PartSizeBucket part = classify_part_size(footprint, bed_area_mm2, height, limits);
        if (int(part) > int(bucket)) {
            bucket = part;
            if (deciding_height_mm != nullptr)
                *deciding_height_mm = height;
            if (deciding_pct != nullptr)
                *deciding_pct = bed_area_mm2 > 0. ? 100. * footprint / bed_area_mm2 : 0.;
        }
    }
    return bucket;
}

// Footprint (mm2, from the instance's 2D convex hull) and height (mm, from the instance bounding
// box) of one object instance. The hull rather than the bounding box: a part turned 45 degrees has
// up to twice the bounding box for the same material, which would put it in the wrong band.
inline std::pair<double, double> instance_footprint(const ModelObject &object, size_t instance_idx)
{
    if (instance_idx >= object.instances.size())
        return {0., 0.};
    const ModelInstance &instance = *object.instances[instance_idx];
    const Polygon        hull     = object.convex_hull_2d(instance.get_transformation().get_matrix());
    // Hull points are in scaled coordinates; SCALING_FACTOR is millimetres per scaled unit, so the
    // area converts with its square (the same conversion Plater.cpp uses for polygon areas).
    const double area_mm2 = std::abs(hull.area()) * SCALING_FACTOR * SCALING_FACTOR;
    return {area_mm2, object.instance_bounding_box(instance).size().z()};
}

// Local automation, keyed by the BedType's integer value (an unknown or future type is simply an
// unused row). Both maps start empty: with nothing configured, nothing changes.
using PlateFlowDefaults    = std::map<int, double>;                                 // percent, -50..0
using PlateBedTempDefaults = std::map<int, std::array<int, kPartSizeBucketCount>>;  // degC offset

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

// True when the user configured the bed-temperature automation for this plate type, i.e. when the
// automation owns the row instead of an older per-project offset.
inline bool has_bed_temp_default(const PlateBedTempDefaults &defaults, BedType bed_type)
{
    return defaults.find(int(bed_type)) != defaults.end();
}

inline int bed_temp_default_for(const PlateBedTempDefaults &defaults, BedType bed_type, PartSizeBucket bucket)
{
    auto it = defaults.find(int(bed_type));
    if (it == defaults.end() || int(bucket) < 0 || int(bucket) >= kPartSizeBucketCount)
        return 0;
    return clamp_bed_temp_offset(it->second[int(bucket)]);
}

// AppConfig encoding. One string each so the whole map is written atomically and stays readable in
// OrcaSlicer.conf:
//   flow:     "0:-20;2:-12"                      (BedType:percent)
//   bed temp: "0:-15,-10,-5,0,0,5;5:0,0,0,0,0,0" (BedType:six offsets in bucket order)
// Parsing is tolerant: unknown keys, unparsable numbers and short rows are dropped, values are
// clamped, so a hand-edited or stale value can never crash the dialog.
inline std::string serialize_flow_defaults(const PlateFlowDefaults &defaults)
{
    std::string out;
    for (const auto &[key, percent] : defaults) {
        if (! valid_bed_type_key(key))
            continue;
        if (! out.empty())
            out += ';';
        // A configured 0 ("no reduction") is kept: it is what makes the plate's own automation
        // override an older per-project value instead of falling back to it.
        out += std::to_string(key) + ':' + std::to_string(clamp_first_layer_flow_delta(percent));
    }
    return out;
}

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

inline std::string serialize_bed_temp_defaults(const PlateBedTempDefaults &defaults)
{
    std::string out;
    for (const auto &[key, offsets] : defaults) {
        if (! valid_bed_type_key(key))
            continue;
        if (! out.empty())
            out += ';';
        out += std::to_string(key) + ':';
        for (int i = 0; i < kPartSizeBucketCount; ++i) {
            if (i > 0)
                out += ',';
            out += std::to_string(clamp_bed_temp_offset(offsets[i]));
        }
    }
    return out;
}

inline PlateBedTempDefaults parse_bed_temp_defaults(const std::string &text)
{
    PlateBedTempDefaults out;
    size_t               pos = 0;
    while (pos < text.size()) {
        const size_t end = text.find(';', pos);
        const std::string entry = text.substr(pos, end == std::string::npos ? std::string::npos : end - pos);
        pos = end == std::string::npos ? text.size() : end + 1;
        const size_t colon = entry.find(':');
        if (colon == std::string::npos)
            continue;
        int    key = 0;
        try {
            key = std::stoi(entry.substr(0, colon));
        } catch (...) {
            continue;
        }
        if (! valid_bed_type_key(key))
            continue;
        std::array<int, kPartSizeBucketCount> offsets {};
        const std::string                     values = entry.substr(colon + 1);
        size_t                                vpos   = 0;
        int                                   index  = 0;
        while (vpos <= values.size() && index < kPartSizeBucketCount) {
            const size_t      vend  = values.find(',', vpos);
            const std::string value = values.substr(vpos, vend == std::string::npos ? std::string::npos : vend - vpos);
            if (! value.empty()) {
                try {
                    offsets[index] = clamp_bed_temp_offset(std::stoi(value));
                } catch (...) {
                    offsets[index] = 0;
                }
            }
            ++index;
            if (vend == std::string::npos)
                break;
            vpos = vend + 1;
        }
        out[key] = offsets;
    }
    return out;
}

// The plate area the footprint bands are a percentage of. The plate reports millimetres.
inline double plate_area_mm2(const Vec2d &plate_size)
{
    return std::max(0., plate_size.x()) * std::max(0., plate_size.y());
}

} // namespace OrcaExt
} // namespace Slic3r

#endif /* slic3r_OrcaExt_PlateAutomation_hpp_ */
