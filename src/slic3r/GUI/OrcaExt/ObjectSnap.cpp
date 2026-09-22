// [ORCAPORT:SNAP-8] OrcaExt ObjectSnap - see ObjectSnap.hpp.

#include "ObjectSnap.hpp"

#include <limits>
#include <vector>

#include "slic3r/GUI/3DScene.hpp"                // GLVolume, GLVolumeCollection
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"  // coplanar_region, build_face_snap_points, world_to_screen
#include "slic3r/GUI/MeshUtils.hpp"              // MeshRaycaster::unproject_on_mesh

#include "libslic3r/Model.hpp"

namespace Slic3r {
namespace OrcaExt {
namespace Gui {
namespace ObjectSnap {

namespace {

// Same stick radius the Holes and Cut gizmos give nearest_face_snap.
constexpr double SNAP_MIN_PX = 8.0;
constexpr double SNAP_MAX_PX = 48.0;

// Candidate cache for the face currently under the cursor. Canvas callbacks are single-threaded, so
// the static is safe here; it exists to keep its_face_normals / its_face_neighbors off the per-frame
// path, the same reason GLGizmoHoles caches its (mv, facet, region, points) quadruple.
struct FaceCache
{
    const ModelVolume *       mv{ nullptr };
    size_t                    facet{ size_t(-1) };
    GUI::FaceRegionCache      region_cache;
    std::vector<FaceSnapPoint> points;
};

const std::vector<FaceSnapPoint> &candidates_for(const ModelVolume *mv, size_t facet)
{
    static FaceCache cache;
    if (cache.mv != mv || cache.facet != facet) {
        cache.mv     = mv;
        cache.facet  = facet;
        cache.points = GUI::build_face_snap_points(mv, GUI::coplanar_region(mv, facet, cache.region_cache));
    }
    return cache.points;
}

} // namespace

std::optional<SnapTarget> target_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                              const GUI::Camera &camera,
                                              const std::set<std::pair<int, int>> &moving, const Vec2d &mouse)
{
    // Closest visible volume under the cursor, over every object that is not being dragged. Same
    // rule as GLGizmosCommon's raycast_object_face, but the selection is not the target set here.
    const GLVolume *hit_volume = nullptr;
    size_t          hit_facet  = 0;
    Vec3d           hit_world  = Vec3d::Zero();
    double          closest    = std::numeric_limits<double>::max();

    for (const GLVolume *v : volumes.volumes) {
        if (v == nullptr || v->mesh_raycaster == nullptr || v->is_wipe_tower || v->is_modifier ||
            v->is_sla_pad() || v->is_sla_support())
            continue;
        if (moving.count({ v->object_idx(), v->instance_idx() }) != 0)
            continue;

        Vec3f  hit, normal;
        size_t facet = 0;
        if (!v->mesh_raycaster->unproject_on_mesh(mouse, v->world_matrix(), camera, hit, normal, nullptr, &facet))
            continue;

        const Vec3d  p = v->world_matrix() * hit.cast<double>();
        const double d = (camera.get_position() - p).squaredNorm();
        if (d < closest) {
            closest    = d;
            hit_volume = v;
            hit_facet  = facet;
            hit_world  = p;
        }
    }
    if (hit_volume == nullptr)
        return std::nullopt;

    const int obj_idx = hit_volume->object_idx();
    const int vol_idx = hit_volume->volume_idx();
    if (obj_idx < 0 || obj_idx >= int(model.objects.size()))
        return std::nullopt;
    const ModelObject *mo = model.objects[obj_idx];
    if (mo == nullptr || vol_idx < 0 || vol_idx >= int(mo->volumes.size()))
        return std::nullopt;

    const Transform3d world   = hit_volume->world_matrix();
    const auto        project = [&](const Vec3d &p) { return GUI::world_to_screen(camera, world * p); };

    // Curved or otherwise ungroupable face: the raw hit is the one candidate. Kept in the volume's
    // own mesh space so the projection and the resulting world position share one transform.
    std::vector<FaceSnapPoint>        fallback;
    const std::vector<FaceSnapPoint> *pts = &candidates_for(mo->volumes[vol_idx], hit_facet);
    if (pts->empty()) {
        fallback.push_back({ FaceSnapKind::None, world.inverse() * hit_world });
        pts = &fallback;
    }

    FaceSnapPoint best;
    if (!nearest_face_snap(*pts, project, mouse, best, SNAP_MIN_PX, SNAP_MAX_PX, nullptr))
        return std::nullopt;

    SnapTarget out;
    out.world = world * best.pos;
    out.kind  = best.kind;
    return out;
}

} // namespace ObjectSnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r
