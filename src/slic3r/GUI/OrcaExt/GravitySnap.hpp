#ifndef slic3r_OrcaExt_GravitySnap_hpp_
#define slic3r_OrcaExt_GravitySnap_hpp_

// [ORCAPORT FILE] OrcaExt GravitySnap - Snap & Drag: while dragging, rest an object on the real
// surface found under its XY footprint (footprint overlap, not the raycast under the cursor).
// GUI-only: it decides where a dragged instance visually lands and never touches libslic3r/Print.
// Source: NEOTKOCM_RELEASE_2_39.md, 2_40.md, 2_43.md; fork shas c3508e5a92, e2cfcff6d9, 79a3cae5c6.

#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/Polygon.hpp"

namespace Slic3r {
class GLVolumeCollection;

namespace OrcaExt {
namespace Gui {
namespace GravitySnap {

// Snap & Drag is enabled only when free-Z placement (AS-1) is on and the app key is set. Without
// free-Z an object is force-dropped to the bed by do_move, so the feature would be pointless.
bool enabled();

// Whether the build plate counts as a floor at Z=0 (app key, absent means true). Only meaningful
// while enabled() is true.
bool bed_is_floor();

// "Move selection as one block" (app key, absent means false): a multi-instance drag moves rigidly
// instead of resolving stacks member by member.
bool move_as_group();

// Is the Snap & Drag options panel showing? Mutable reference, transient UI state (no ini key).
// The magnet icon in the plate column flips it; GLCanvas3D::_render_overlays reads it.
bool &panel_open();

// Should the magnet icon exist in the plate column? Always true: the panel is the one home for the
// three preferences, independent of any selection-context menu.
bool plate_icon_available();

// Result of a floor query. `contact` is the footprint zone the decision was made on, and
// `samples` the real raycast hits that produced `z` (empty for a bed hit).
struct FloorHit
{
    double             z        = 0.0;
    bool               is_bed   = false;
    int                obj_idx  = -1;
    int                inst_idx = -1;
    ExPolygon          contact;
    std::vector<Vec3d> samples;
};

// Real floor Z (world mm) under instance (object_idx, instance_idx) given every GLVolume in the
// scene. `moving` lists the pairs being dragged together and is excluded as candidate floors, so a
// dragged group never rests on itself. `engage_ratio` in (0, 1] is the minimum fraction of the
// queried instance's own footprint area that must overlap a candidate for it to count.
//
// The candidate's top is sampled by raycasting its real mesh, so a hollow box resolves to the
// actual surface under the overlap. Returns the HIGHEST qualifying candidate top, or std::nullopt
// when nothing qualifies. IMPORTANT: nullopt means "leave it exactly where it is", NOT "drop it to
// the bed" - with bed_is_floor() the plate arrives as a real FloorHit instead.
std::optional<FloorHit> floor_z_for_instance(const GLVolumeCollection &volumes,
                                             int object_idx, int instance_idx,
                                             const std::set<std::pair<int, int>> &moving,
                                             double engage_ratio);

// Which other member of the same drag instance (object_idx, instance_idx) is standing on, if any:
// the highest member whose top is within `max_gap` below this instance's bottom and whose footprint
// overlaps by at least `engage_ratio`. Uses the flat convex-hull top.
std::optional<std::pair<int, int>> support_in_group(const GLVolumeCollection &volumes,
                                                    int object_idx, int instance_idx,
                                                    const std::set<std::pair<int, int>> &group,
                                                    double engage_ratio, double max_gap);

// World-space 2D convex-hull footprint (scaled units) of an instance, or an empty Polygon when it
// has no volumes.
Polygon instance_footprint(const GLVolumeCollection &volumes, int object_idx, int instance_idx);

} // namespace GravitySnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r

#endif
