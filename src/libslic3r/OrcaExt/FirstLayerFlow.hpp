// [ORCAPORT FILE] FirstLayerFlow - print-time reduction of the first layer's bottom surface flow
// Source: original (design in porting/notes/BF-1.md)
//
// A cool plate can over-extrude its first layer even with a correct Z offset. The upload dialog
// offers a percentage reduction of the bottom surface flow applied to the first layer only. The
// value lives in the project config (like curr_bed_type, so it never dirties a preset), and the key
// is in the "G-code export only" invalidation bucket, so changing it re-exports the G-code and
// keeps the slice result: no re-slice, and no post-processing script.
//
// This header is the single place the reduction is bounded and turned into a multiplier, so the
// G-code emission and the dialog readout can never disagree.
#ifndef slic3r_OrcaExt_FirstLayerFlow_hpp_
#define slic3r_OrcaExt_FirstLayerFlow_hpp_

#include <algorithm>

#include "../PrintConfig.hpp"

namespace Slic3r {
namespace OrcaExt {

// Percent reduction. The dialog offers -20 / -15 / -10 / -5; the wider bound is defence, not UI.
// Reduction only: a positive delta would amplify the first-layer squish that causes the problem
// this exists for.
constexpr double kFirstLayerFlowDeltaMin = -50.0;
constexpr double kFirstLayerFlowDeltaMax = 0.0;
// Floor on the multiplier, so a hand-edited project cannot ask for (near) zero flow and print air.
constexpr double kFirstLayerFlowScaleMin = 0.5;
// What the upload dialog preselects for a plate that tends to over-extrude its first layer.
constexpr double kFirstLayerFlowCoolPlateDefault = -20.0;

// The three cold, smooth/rough build surfaces in BedType's list, which is what a cool plate is in
// the plate picker: Cool Plate ("Smooth Cool Plate"), Textured Cool Plate and Cool Plate
// (SuperTack).
inline bool is_cool_plate(BedType bed_type)
{
    return bed_type == btPC || bed_type == btPCT || bed_type == btSuperTack;
}

// Reduction the dialog preselects for `bed_type`: the cool-plate default, or none.
inline double first_layer_flow_plate_default(BedType bed_type)
{
    return is_cool_plate(bed_type) ? kFirstLayerFlowCoolPlateDefault : 0.0;
}

inline double clamp_first_layer_flow_delta(double delta)
{
    return std::clamp(delta, kFirstLayerFlowDeltaMin, kFirstLayerFlowDeltaMax);
}

// Percent delta -> multiplier on bottom_solid_infill_flow_ratio: 0 -> 1.0, -20 -> 0.8.
inline double first_layer_bottom_flow_scale(double delta)
{
    return std::max(kFirstLayerFlowScaleMin, 1.0 + clamp_first_layer_flow_delta(delta) / 100.0);
}

// The bottom surface flow ratio the G-code generator uses on the first layer. `profile_ratio` is
// bottom_solid_infill_flow_ratio, which may be any value the profile authored.
inline double first_layer_bottom_effective_flow(double profile_ratio, double delta)
{
    return profile_ratio * first_layer_bottom_flow_scale(delta);
}

} // namespace OrcaExt
} // namespace Slic3r

#endif /* slic3r_OrcaExt_FirstLayerFlow_hpp_ */
