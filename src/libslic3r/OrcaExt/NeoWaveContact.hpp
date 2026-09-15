// [ORCAPORT FILE] NeoWaveContact - NeoWave support contact layer (Z-wave to reduce bonding)
// Source: NEOTKOCM_RELEASE_2_37.md; fork sha c3508e5a92
#ifndef slic3r_OrcaExt_NeoWaveContact_hpp_
#define slic3r_OrcaExt_NeoWaveContact_hpp_

#include <functional>
#include <string>
#include <vector>

#include "../Point.hpp"
#include "../libslic3r.h"

namespace Slic3r {

class ExtrusionEntity;
class ExtrusionPath;
class GCodeWriter;

namespace OrcaExt {
namespace NeoWaveContact {

// Wave geometry shared by both targets.
//
// The wave is one-sided (rectified sine), so it only ever moves material away from the
// interface it rests on: the object's bridge fill waves upward (target PartBottom), the
// support's top-contact interface waves downward (target SupportTop). Neither can dig into
// the other side by construction.
//
// `period_mm` of 0 means "auto" and resolves to the path line width. It is floored at
// ~0.2 mm / one line width so the micro-segment count stays bounded (a Z wave finer than a
// line width is unprintable anyway).

// Resolve the effective wave period in mm for a path of the given (scaled) width.
double resolve_period_mm(double period_mm, coord_t width_scaled);

// Maximum XY feedrate (mm/min) whose implied Z feedrate stays within `max_z_speed_mm_s`
// (mm/s) for the given period/amplitude. Callers cap the normal print feedrate with this.
double xy_feedrate_cap(double period_mm, double amplitude_mm, double max_z_speed_mm_s,
                       coord_t width_scaled);

// PartBottom target: emit `path` as G-code with an upward-only Z wave. Returns the emitted
// G-code (empty when the wave is degenerate). `point_to_gcode` maps a scaled XY point to
// G-code coordinates; `nominal_z` is the layer's nominal Z (valleys sit there).
std::string emit_part_wave(const ExtrusionPath      &path,
                           GCodeWriter               &writer,
                           double                     nominal_z,
                           double                     F,
                           double                     e_per_mm,
                           bool                       force_no_extrusion,
                           const std::function<Vec2d(const Point &)> &point_to_gcode,
                           double                     amplitude_mm,
                           double                     period_mm,
                           double                     max_z_speed_mm_s);

// Restore the nozzle to the nominal layer Z after `emit_part_wave`.
std::string restore_z(GCodeWriter &writer, double nominal_z);

// SupportTop target: rewrite a top-contact support path in place into a downward-only Z
// wave and set `z_contoured` so the existing variable-Z GCode emitter is used. Returns
// false when the path is left untouched (degenerate amplitude or too few points).
bool apply_support_wave(ExtrusionPath &path, double amplitude_mm, double period_mm);

// SupportTop target: recurse over a support toolpath tree (paths / multipaths / loops /
// collections) and wave every leaf path. Returns the number of paths modified.
int apply_support_wave(std::vector<ExtrusionEntity *> &entities, double amplitude_mm,
                       double period_mm);

} // namespace NeoWaveContact
} // namespace OrcaExt
} // namespace Slic3r

#endif
