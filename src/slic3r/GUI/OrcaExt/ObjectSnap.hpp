#ifndef slic3r_OrcaExt_ObjectSnap_hpp_
#define slic3r_OrcaExt_ObjectSnap_hpp_

// [ORCAPORT:SNAP-8] OrcaExt ObjectSnap - object-to-object point snap while dragging a volume with
// Alt held: the grabbed point is moved onto the nearest snap coordinate (corner, edge midpoint or
// quarter, face centre or quarter) of another object's flat face under the cursor. Assembling
// multi-part prints is the use case. GUI-only: it decides where a dragged selection lands and never
// touches libslic3r/Print. Unlike Snap & Drag (AS-3) it needs no free-Z and no app key: the Alt
// gesture is itself the opt-in.

#include <optional>
#include <set>
#include <utility>

#include "libslic3r/CutUtils.hpp" // FaceSnapKind
#include "libslic3r/Point.hpp"

namespace Slic3r {

class GLVolumeCollection;
class Model;

namespace GUI { struct Camera; }

namespace OrcaExt {
namespace Gui {
namespace ObjectSnap {

struct SnapTarget
{
    Vec3d        world{ Vec3d::Zero() }; // world position of the snapped coordinate
    FaceSnapKind kind{ FaceSnapKind::None };
};

// Nearest snap coordinate of a flat face of any object that is NOT part of `moving`, under `mouse`
// (device pixels). `moving` lists the (object, instance) pairs being dragged, so a drag never snaps
// to itself. A face with no usable coplanar region falls back to the raw hit, so a curved surface
// still snaps point-to-point. Returns nullopt when the ray hits nothing or the nearest coordinate is
// farther than the face's own stick radius (the same adaptive radius the Holes and Cut gizmos use).
std::optional<SnapTarget> target_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                              const GUI::Camera &camera,
                                              const std::set<std::pair<int, int>> &moving, const Vec2d &mouse);

} // namespace ObjectSnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r

#endif
