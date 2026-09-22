#ifndef slic3r_OrcaExt_ObjectSnap_hpp_
#define slic3r_OrcaExt_ObjectSnap_hpp_

// [ORCAPORT:SNAP-8] OrcaExt ObjectSnap - object-to-object point snap while dragging a volume with
// Alt held: the grabbed point is moved onto the nearest snap coordinate (corner, edge midpoint or
// quarter, face centre or quarter) of another object's flat face under the cursor. Assembling
// multi-part prints is the use case. GUI-only: it decides where a dragged selection lands and never
// touches libslic3r/Print. Unlike Snap & Drag (AS-3) it needs no free-Z and no app key: the Alt
// gesture is itself the opt-in.

#include <cstddef>
#include <optional>
#include <set>
#include <utility>
#include <vector>

#include "libslic3r/CutUtils.hpp" // FaceSnapKind
#include "libslic3r/Point.hpp"
#include "slic3r/GUI/GLModel.hpp" // marker spheres

namespace Slic3r {

class GLVolumeCollection;
class Model;

namespace GUI { struct Camera; }

namespace OrcaExt {
namespace Gui {
namespace ObjectSnap {

struct SnapCandidate
{
    Vec3d        world{ Vec3d::Zero() }; // world position of the snapped coordinate
    FaceSnapKind kind{ FaceSnapKind::None };
};

// The chosen coordinate plus every coordinate of the face it came from, so the caller can show them.
struct SnapHit
{
    SnapCandidate              point;      // the coordinate the grab point lands on
    std::vector<SnapCandidate> candidates; // all snap coordinates of that face, in world space
};

// Nearest snap coordinate of a flat face of any object that is NOT part of `moving`, under `mouse`
// (device pixels). `moving` lists the (object, instance) pairs being dragged, so a drag never snaps
// to itself. A face with no usable coplanar region falls back to the raw hit, so a curved surface
// still snaps point-to-point. Returns nullopt when the ray hits nothing or the nearest coordinate is
// farther than the face's own stick radius (the same adaptive radius the Holes and Cut gizmos use).
std::optional<SnapHit> hit_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                        const GUI::Camera &camera,
                                        const std::set<std::pair<int, int>> &moving, const Vec2d &mouse);

// Same, but for the object being dragged: the nearest snap coordinate of the mover itself. The drag
// is anchored on that coordinate instead of on the raw cursor, so grabbing a corner a few pixels off
// still lands the corner exactly on the target. Returns nullopt when the cursor is off the dragged
// object or no coordinate is inside its stick radius.
std::optional<SnapHit> mover_hit_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                              const GUI::Camera &camera,
                                              const std::set<std::pair<int, int>> &moving, const Vec2d &mouse);

// Which object a marker set belongs to, so the dragged object's spheres are told apart from the
// target's at a glance.
enum class MarkerRole
{
    Target, // where the grab point will land
    Mover,  // the dragged object's own coordinates
};

// The candidate spheres shown while Alt is held, one colour per feature kind (same palette as the
// Holes gizmo) and a larger one on the active coordinate. Pure render state: it never feeds back
// into the snap decision.
class Markers
{
public:
    void set(const std::vector<SnapCandidate> &candidates, size_t active, MarkerRole role = MarkerRole::Target);
    void clear();
    bool is_visible() const { return m_visible; }
    void render(const GUI::Camera &camera);

private:
    GUI::GLModel m_kind[5]; // indexed by int(kind) - 1
    GUI::GLModel m_active;
    bool         m_visible{ false };
};

} // namespace ObjectSnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r

#endif
