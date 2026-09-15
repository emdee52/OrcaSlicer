// [ORCAPORT FILE] SupportAutoPaint - automatic painting of support regions
// Source: preFlight v1.3.0 "Automatic painting" (fork sha f74dc69), reimplemented Orca-native.
#ifndef slic3r_OrcaExt_SupportAutoPaint_hpp_
#define slic3r_OrcaExt_SupportAutoPaint_hpp_

#include <cstddef>
#include <vector>

#include "../TriangleSelector.hpp"

namespace Slic3r {

class PrintObject;

namespace OrcaExt {

// One model facet the auto-painter wants to paint with a classified support type.
struct SupportAutoPaintHit
{
    size_t              volume_index{0}; // index among the object's model-part volumes
    size_t              facet_index{0};  // facet index in that volume's mesh
    EnforcerBlockerType state{EnforcerBlockerType::NONE};
    double              contact_area_mm2{0.};
    double              support_height_mm{0.};
};

// Classify the object's support contact regions into paint states.
//
// Precondition: the object has been sliced and its support generated (call
// PrintObject::generate_support_preview() first); with no support layers this returns {}.
// The caller applies the hits and must skip facets already painted as BLOCKER, so the auto-paint
// follows existing blockers. The classification thresholds live in the OrcaExt support-paint
// registry (SupportPaintTypes.hpp).
std::vector<SupportAutoPaintHit> classify_support_paint(const PrintObject &object);

} // namespace OrcaExt
} // namespace Slic3r

#endif
