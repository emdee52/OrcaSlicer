// [ORCAPORT FILE] SupportAutoPaint - automatic painting of support regions
// Source: preFlight v1.3.0 "Automatic painting" (fork sha f74dc69), reimplemented Orca-native.
#ifndef slic3r_OrcaExt_SupportAutoPaint_hpp_
#define slic3r_OrcaExt_SupportAutoPaint_hpp_

#include <cstddef>
#include <cstdint>
#include <vector>

#include "../TriangleSelector.hpp"
#include "SupportPaintTypes.hpp"

namespace Slic3r {

class ModelObject;

namespace OrcaExt {

// Parameters of one automatic-painting request, taken from the support-painting gizmo.
struct SupportAutoPaintParams
{
    // Overhang criterion, in the same convention as the gizmo's "Highlight overhangs" slider: a
    // facet is a candidate when its normal lies within this many degrees of straight down.
    float overhang_angle_deg{30.f};
    // When true, restrict to the highlighted overhang set (the angle above). When false, use every
    // downward-facing facet (90 degrees), i.e. treat any overhang as a candidate.
    bool  overhangs_only{true};
    // States the user allowed automatic painting to choose (the per-type checkboxes). An empty
    // vector means every auto-selectable registry type is eligible.
    std::vector<EnforcerBlockerType> enabled_types;
    // Regions smaller than this are never painted: a tiny overhang sliver cannot hold a support
    // tip/interface, so painting it only makes the tree engine aim branches at an unsupportable
    // spot. 0 disables the filter.
    double min_region_area_mm2{4.0};
    // Adjacent candidate facets merge into one region only when their normals differ by at most
    // this many degrees. Keeps a large flat overhang and a thin curved strip next to it as separate
    // regions, so they can be classified (and coloured) independently.
    float region_split_angle_deg{30.f};
};

// One model facet the auto-painter wants to paint, plus the region measurements that selected it.
struct SupportAutoPaintHit
{
    size_t              volume_index{0}; // index among the object's model-part volumes
    size_t              facet_index{0};  // facet index in that volume's mesh
    EnforcerBlockerType state{EnforcerBlockerType::NONE};
    SupportRegionFeatures features;
};

// Classify the object's overhang regions into paint states.
//
// This is a pure mesh analysis: it needs no slicing and no support preview. `instance_trafo` is the
// active instance matrix; the volume transform is applied on top of it. `painted` (optional) holds
// one mask per model-part volume (see TriangleSelector::painted_facet_mask) and excludes already
// painted or blocked facets from both the clustering and the output, so existing painting is never
// overwritten.
//
// The thresholds live in the OrcaExt support-paint registry (SupportPaintTypes.hpp).
std::vector<SupportAutoPaintHit> classify_support_paint(
    const ModelObject                        &model_object,
    const Transform3d                        &instance_trafo,
    const std::vector<std::vector<uint8_t>>  &painted,
    const SupportAutoPaintParams             &params);

} // namespace OrcaExt
} // namespace Slic3r

#endif // slic3r_OrcaExt_SupportAutoPaint_hpp_
