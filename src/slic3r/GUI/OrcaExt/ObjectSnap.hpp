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

#include <Eigen/Core> // Matrix3d

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

// Every snap coordinate of the face the cursor is over, plus the one the grab point lands on.
struct SnapHit
{
    std::vector<SnapCandidate> candidates; // all snap coordinates of that face, in world space
    // Rotation of the hit volume's instance. Selection::translate moves an instance by R*displacement,
    // so landing a point of that instance on a world position needs the correction premultiplied by
    // R^-1; without it a rotated object settles short of the target. Identity for the mover's purpose
    // when the object is not rotated.
    Eigen::Matrix3d            rotation{ Eigen::Matrix3d::Identity() };
    // Index into `candidates` of the coordinate the cursor is on, or no_candidate when it is not on
    // any of them. The face is still reported then, so its markers show; the snap just does not
    // engage.
    static constexpr size_t    no_candidate = static_cast<size_t>(-1);
    size_t                     active{ no_candidate };
    const SnapCandidate       &point() const { return candidates[active]; }
    bool                       has_point() const { return active < candidates.size(); }
};

// Nearest snap coordinate of a flat face of any object that is NOT part of `moving`, under `mouse`
// (device pixels). `moving` lists the (object, instance) pairs being dragged, so a drag never snaps
// to itself. A face with no usable coplanar region falls back to the raw hit, so a curved surface
// still snaps point-to-point. Returns nullopt when the ray hits nothing; the hit is returned even
// when the cursor is not on any of that face's coordinates (`active` is then no_candidate) so the
// caller can show the face's spheres either way.
std::optional<SnapHit> hit_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                        const GUI::Camera &camera,
                                        const std::set<std::pair<int, int>> &moving, const Vec2d &mouse);

// Same, but for the object being dragged: the snap coordinates of the mover itself. The drag is
// anchored on the one the cursor is on instead of on the raw cursor, so grabbing a corner a few
// pixels off still lands the corner exactly on the target. Returns nullopt when the ray misses the
// dragged object entirely.
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
