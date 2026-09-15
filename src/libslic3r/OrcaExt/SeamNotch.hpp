// [ORCAPORT FILE] SeamNotch - Nip/Tuck seam shaping for external perimeters
// Source: preFlight v1.3.0 (github.com/oozebot/preFlight), GCode.cpp:3979-4830; AGPLv3
#ifndef slic3r_OrcaExt_SeamNotch_hpp_
#define slic3r_OrcaExt_SeamNotch_hpp_

#include "../ExtrusionEntity.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {
namespace OrcaExt {
namespace SeamNotch {

// Re-implementation of preFlight's Nip/Tuck seam notch against Orca's extrusion paths.
//
// Nip/Tuck reshapes the external perimeter AT the seam (it does not move the seam):
//   - Nip/Tuck   : V-notch at the start and the end of the loop
//   - Nip        : notch only the start (the end is clipped instead)
//   - Tuck       : notch only the end (the start is clipped instead)
//   - Alternating: Nip on even layers, Tuck on odd
//
// `paths` must be the loop's extrusion paths with the seam at the first point of the front
// path and the loop end at the last point of the back path (i.e. after GCode::extrude_loop's
// clip_end). `loop_ccw` is loop.polygon().is_counter_clockwise(). Returns true if modified.
bool apply(ExtrusionPaths &paths, bool loop_ccw, bool is_hole, SeamNotchType type,
           SeamNotchTarget target, double notch_width_factor, double corner_threshold_deg,
           int layer_index);

} // namespace SeamNotch
} // namespace OrcaExt
} // namespace Slic3r

#endif
