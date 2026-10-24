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
// This header owns the data model (buckets, maps, AppConfig encoding), the size matrix and the
// classification, all free of UI, so the behaviour is unit-tested rather than eyeballed.
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
// part accumulates enough contraction to lift a large footprint.
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

// The matrix axes: six height rows (the first is the flat gate) and five footprint columns.
constexpr int kPartSizeHeightRows    = 6;
constexpr int kPartSizeFootprintCols = 5;

// Editable bands, saved in AppConfig so they can be tuned at runtime instead of rebuilt. Keep them
// ascending - valid_part_size_limits() is the gate, and a stored value that fails it falls back to
// the defaults rather than mis-classifying.
struct PartSizeLimits {
    // Interior edges of the five footprint bands, as a share of the plate's area.
    std::array<double, kPartSizeFootprintCols - 1> fp_pct {3., 25., 50., 75.};
    // Height row edges in mm: the flat gate (a big part above it is Large whatever its height), then
    // thin, mid, high and tall.
    std::array<double, kPartSizeHeightRows - 1> h_mm {1., 3., 20., 50., 100.};
};

constexpr double kMinFootprintPct = 0.1;
constexpr double kMaxFootprintPct = 100.0;
constexpr double kMinHeightMm     = 0.1;
constexpr double kMaxHeightMm     = 500.0;

inline bool valid_part_size_limits(const PartSizeLimits &limits)
{
    if (limits.fp_pct.front() < kMinFootprintPct || limits.fp_pct.back() > kMaxFootprintPct)
        return false;
    for (size_t i = 1; i < limits.fp_pct.size(); ++i)
        if (!(limits.fp_pct[i] > limits.fp_pct[i - 1]))
            return false;

    if (limits.h_mm.front() < kMinHeightMm || limits.h_mm.back() > kMaxHeightMm)
        return false;
    for (size_t i = 1; i < limits.h_mm.size(); ++i)
        if (!(limits.h_mm[i] > limits.h_mm[i - 1]))
            return false;
    return true;
}

// The classifier as data: rows are the height bands (below the flat gate first), columns the
// footprint bands from the smallest share of the bed to the biggest. Thresholds are inclusive at the
// top of each band, so a part exactly at an edge belongs to the higher band.
//
// A part in the biggest footprint column is Large from the flat gate up to the tall band, with no
// height row in between: a print that covers most of the bed needs bed heat for adhesion and for the
// contraction of a long span, however flat it is, and dropping it into Small would be wrong.
constexpr PartSizeBucket kPartSizeMatrix[kPartSizeHeightRows][kPartSizeFootprintCols] = {
    // < fp[0]                 fp[0]-fp[1]              fp[1]-fp[2]              fp[2]-fp[3]             > fp[3]
    {PartSizeBucket::Small,       PartSizeBucket::Small,       PartSizeBucket::Small,       PartSizeBucket::Small,       PartSizeBucket::Small},      // below the flat gate
    {PartSizeBucket::Small,       PartSizeBucket::Small,       PartSizeBucket::SmallMedium, PartSizeBucket::SmallMedium, PartSizeBucket::Large},      // flat gate .. thin
    {PartSizeBucket::Small,       PartSizeBucket::SmallMedium, PartSizeBucket::SmallMedium, PartSizeBucket::Medium,      PartSizeBucket::Large},      // thin .. mid
    {PartSizeBucket::Medium,      PartSizeBucket::Medium,      PartSizeBucket::MediumLarge, PartSizeBucket::MediumLarge, PartSizeBucket::Large},      // mid .. high
    {PartSizeBucket::MediumLarge, PartSizeBucket::Large,       PartSizeBucket::Large,       PartSizeBucket::Large,       PartSizeBucket::Large},      // high .. tall
    {PartSizeBucket::Tall,        PartSizeBucket::Tall,        PartSizeBucket::Tall,        PartSizeBucket::Tall,        PartSizeBucket::Tall},       // tall and up
};

// Row of the matrix a height falls in, 0 = below the flat gate.
inline int part_size_height_row(double height_mm, const PartSizeLimits &limits = PartSizeLimits {})
{
    for (int row = 0; row < kPartSizeHeightRows - 1; ++row)
        if (height_mm < limits.h_mm[row])
            return row;
    return kPartSizeHeightRows - 1;
}

// Column of the matrix a footprint share falls in, 0 = smallest.
inline int part_size_footprint_column(double footprint_pct, const PartSizeLimits &limits = PartSizeLimits {})
{
    for (int column = 0; column < kPartSizeFootprintCols - 1; ++column)
        if (footprint_pct < limits.fp_pct[column])
            return column;
    return kPartSizeFootprintCols - 1;
}

// The bucket a single part falls in. `bed_area_mm2` is the plate's own area; a plate with no usable
// area, or a part with no footprint, falls back to Medium rather than guessing an extreme.
inline PartSizeBucket classify_part_size(double footprint_mm2, double bed_area_mm2, double height_mm,
                                        const PartSizeLimits &limits = PartSizeLimits {})
{
    if (!(bed_area_mm2 > 0.) || !(footprint_mm2 > 0.))
        return PartSizeBucket::Medium;
    const double pct = 100. * footprint_mm2 / bed_area_mm2;
    return kPartSizeMatrix[part_size_height_row(height_mm, limits)][part_size_footprint_column(pct, limits)];
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

// "3,25,50,75;1,3,20,50,100" - footprint edges in percent, then height edges in mm. A value written
// by an older build (two footprint edges and four heights) does not match the counts and falls back
// to the defaults, which is the safe direction.
inline std::string serialize_part_size_limits(const PartSizeLimits &limits)
{
    auto number = [](double value) {
        std::string text = std::to_string(value);
        text.erase(text.find_last_not_of('0') + 1);
        if (! text.empty() && text.back() == '.')
            text.pop_back();
        return text;
    };
    std::string out;
    for (size_t i = 0; i < limits.fp_pct.size(); ++i)
        out += (i > 0 ? "," : "") + number(limits.fp_pct[i]);
    out += ';';
    for (size_t i = 0; i < limits.h_mm.size(); ++i)
        out += (i > 0 ? "," : "") + number(limits.h_mm[i]);
    return out;
}

// Tolerant: anything malformed, out of order, out of range or of the wrong length yields the
// defaults.
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
    if (fp.size() != limits.fp_pct.size() || heights.size() != limits.h_mm.size())
        return limits;

    std::copy(fp.begin(), fp.end(), limits.fp_pct.begin());
    std::copy(heights.begin(), heights.end(), limits.h_mm.begin());
    return valid_part_size_limits(limits) ? limits : PartSizeLimits {};
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
