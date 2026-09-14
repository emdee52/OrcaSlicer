#ifndef slic3r_OrcaExt_InstanceContact_hpp_
#define slic3r_OrcaExt_InstanceContact_hpp_

// [ORCAPORT FILE] OrcaExt InstanceContact - PerObject Support (SU-1): cross-object support
// avoidance. Builds, per object layer, the footprint of every OTHER PrintObject's instances
// (body plus already-generated support) in this object's local frame, for injection into the
// support collision volumes. Read-only; requires lslices of all objects (posSlice done).
// Source: NEOTKOCM_RELEASE_2_39.md; fork sha c3508e5a92.

#include <vector>

#include "Polygon.hpp"

namespace Slic3r {
class PrintObject;

namespace OrcaExt {

// Per-layer occupancy of the other objects' instances, indexed by this object's layer number and
// expressed in this object's local frame. Empty when the feature is inactive (toggle off,
// by-object print, or no neighbour within range) so callers can no-op at zero cost.
std::vector<Polygons> neighbor_occupancy(const PrintObject &object);

// True when cross-object support avoidance is effectively active for this object: the per-object
// toggle is on AND the plate prints by layer (in by-object mode the neighbours may not exist yet,
// so the avoidance is inert). Single source of truth for every place that reacts to the feature.
bool cross_object_active(const PrintObject &object);

} // namespace OrcaExt
} // namespace Slic3r

#endif
