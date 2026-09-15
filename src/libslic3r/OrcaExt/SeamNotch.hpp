// [ORCAPORT FILE] SeamNotch - Nip/Tuck seam shaping for external perimeters
// Source: preFlight v1.3.0 (github.com/oozebot/preFlight), GCode.cpp:3979-4830; AGPLv3
//
// Orca-native re-implementation. Deviations from preFlight, both deliberate:
//  - Approach A: the loop's seam endpoints stay at nominal and only the interior points
//    inside the taper are pushed in, so the external loop stays closed. This keeps Orca's
//    seam detector happy (it records a seam only when the loop end returns within 0.25 mm of
//    its start, GCodeProcessor.cpp:5416) and avoids the open-wall "gap + hook" defect.
//  - Inner trim: instead of cutting a gap out of the inner perimeter (preFlight splits its
//    smooth_path), the inner wall is nudged away from the notch, so the inner loop also stays
//    continuous.
#ifndef slic3r_OrcaExt_SeamNotch_hpp_
#define slic3r_OrcaExt_SeamNotch_hpp_

#include "../ExtrusionEntity.hpp"
#include "../PrintConfig.hpp"

namespace Slic3r {
namespace OrcaExt {
namespace SeamNotch {

// Result of notching one external perimeter; fed to trim_inner for its first inner wall.
struct ExternalNotch {
    bool   applied = false;
    Point  seam;         // scaled, the loop start (seam)
    Vec2d  push;         // unit inward direction (into the solid)
    double depth  = 0.;  // scaled
    double taper  = 0.;  // scaled
    double width  = 0.;  // scaled external bead width
};

// Notch the external perimeter at its seam. `paths` must be the loop's extrusion paths with
// the seam at paths.front().first_point() and the loop end at paths.back().last_point()
// (after GCode::extrude_loop's clip_end). `loop_ccw` = loop.polygon().is_counter_clockwise().
ExternalNotch apply_external(ExtrusionPaths &paths, bool loop_ccw, bool is_hole, SeamNotchType type,
                             SeamNotchTarget target, double notch_width_factor,
                             double corner_threshold_deg, double width_fallback_mm, int layer_index);

// Relieve the first inner perimeter near the notch so the pushed-in outer bead has room.
// Only the points within the notch taper of the projected V-leg are nudged along the notch
// direction, so the inner loop stays closed. Returns true if modified.
bool trim_inner(ExtrusionPaths &paths, const ExternalNotch &notch, double inner_width);

} // namespace SeamNotch
} // namespace OrcaExt
} // namespace Slic3r

#endif
