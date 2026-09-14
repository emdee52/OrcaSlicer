// [ORCAPORT FILE] OrcaExt GravitySnap - see GravitySnap.hpp.
// Source: NEOTKOCM_RELEASE_2_39.md, 2_40.md, 2_43.md; fork shas c3508e5a92, e2cfcff6d9, 79a3cae5c6.

#include "GravitySnap.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/MeshUtils.hpp"

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/AppConfig.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/ExPolygon.hpp"
#include "libslic3r/Geometry/ConvexHull.hpp"
#include "libslic3r/OrcaExt/FreeZ.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/TriangleMesh.hpp"

namespace Slic3r {
namespace OrcaExt {
namespace Gui {
namespace GravitySnap {

namespace {

AppConfig *app_config()
{
    return Slic3r::GUI::wxGetApp().app_config;
}

// One instance's world-space footprint (2D convex hull, scaled units) and Z extent (unscaled mm),
// built from every non-modifier, non-wipe-tower GLVolume that belongs to it.
struct InstanceFootprint
{
    Polygon hull;
    double  min_z = 0.0;
    double  max_z = 0.0;
    bool    valid = false;
};

InstanceFootprint compute_footprint(const GLVolumeCollection &volumes, int object_idx, int instance_idx)
{
    InstanceFootprint fp;
    Points           pts;
    BoundingBoxf3    bbox;
    bool             has_any = false;

    for (const GLVolume *v : volumes.volumes) {
        if (v == nullptr || v->is_wipe_tower || v->is_modifier)
            continue;
        if (v->object_idx() != object_idx || v->instance_idx() != instance_idx)
            continue;

        const TriangleMesh *hull = v->convex_hull();
        if (hull == nullptr || hull->its.vertices.empty())
            continue;

        const Transform3d matrix = v->world_matrix();
        pts.reserve(pts.size() + hull->its.vertices.size());
        for (const auto &vertex : hull->its.vertices) {
            const Vec3d p = matrix * vertex.cast<double>();
            pts.emplace_back(coord_t(scale_(p.x())), coord_t(scale_(p.y())));
        }

        if (has_any)
            bbox.merge(v->transformed_convex_hull_bounding_box());
        else
            bbox = v->transformed_convex_hull_bounding_box();
        has_any = true;
    }

    if (!has_any || pts.empty())
        return fp;

    fp.hull  = Slic3r::Geometry::convex_hull(pts);
    fp.min_z = bbox.min.z();
    fp.max_z = bbox.max.z();
    fp.valid = !fp.hull.empty();
    return fp;
}

// Up to 5 sample points (world mm) inside `poly`: its centroid plus the midpoint between the
// centroid and up to 4 of its own vertices. `poly` is convex (an intersection of two convex hulls),
// so centroid + 0.25 * (vertex - centroid) stays inside it.
std::vector<Vec2d> sample_points_mm(const ExPolygon &poly)
{
    std::vector<Vec2d> pts;
    if (poly.contour.points.empty())
        return pts;

    const Point c_scaled = poly.contour.centroid();
    const Vec2d c(unscale_(c_scaled.x()), unscale_(c_scaled.y()));
    pts.push_back(c);

    const Points &verts = poly.contour.points;
    const size_t  step  = std::max<size_t>(1, verts.size() / 4);
    for (size_t i = 0; i < verts.size() && pts.size() < 5; i += step) {
        const Vec2d v(unscale_(verts[i].x()), unscale_(verts[i].y()));
        pts.push_back(c + 0.25 * (v - c));
    }
    return pts;
}

// Real surface Z (world mm) under `pts_mm`, sampled by raycasting straight down through the
// instance's actual mesh. `above_z` must be at or above the instance's own flat top so every ray
// starts outside the mesh.
//
// No raycaster at all -> fall back to `flat_fallback` so the feature degrades to flat-top rather
// than going inert. Raycasters present but no sample hits -> nullopt: that XY spot is a hole in
// the geometry and must NOT be treated as a floor. Otherwise the HIGHEST real hit wins.
std::optional<double> sample_real_top_z(const GLVolumeCollection &volumes, int object_idx,
                                        int instance_idx, const std::vector<Vec2d> &pts_mm,
                                        double above_z, double flat_fallback,
                                        std::vector<Vec3d> *out_hits = nullptr)
{
    bool   any_raycaster = false;
    bool   found_hit     = false;
    double best          = -std::numeric_limits<double>::infinity();

    for (const GLVolume *v : volumes.volumes) {
        if (v == nullptr || v->is_wipe_tower || v->is_modifier)
            continue;
        if (v->object_idx() != object_idx || v->instance_idx() != instance_idx)
            continue;
        if (!v->mesh_raycaster)
            continue;
        any_raycaster = true;

        const Transform3d world = v->world_matrix();
        const Transform3d inv   = world.inverse();
        const Vec3d       local_dir = (inv.linear() * Vec3d(0.0, 0.0, -1.0)).normalized();
        const AABBMesh   &aabb      = v->mesh_raycaster->get_aabb_mesh();

        for (const Vec2d &p : pts_mm) {
            const Vec3d                   world_source(p.x(), p.y(), above_z);
            const AABBMesh::hit_result    hit = aabb.query_ray_hit(inv * world_source, local_dir);
            if (!hit.is_hit())
                continue;

            const Vec3d  world_hit = world * hit.position();
            const double world_z   = world_hit.z();
            if (out_hits != nullptr)
                out_hits->push_back(world_hit);
            if (!found_hit || world_z > best) {
                best      = world_z;
                found_hit = true;
            }
        }
    }

    if (found_hit)
        return best;
    return any_raycaster ? std::nullopt : std::optional<double>(flat_fallback);
}

} // namespace

bool enabled()
{
    if (!OrcaExt::free_z())
        return false;
    const AppConfig *ac = app_config();
    return ac != nullptr && ac->get_bool("orca_ext_snap_drag");
}

// DEFAULT ON: read "absent == true" here rather than seeding AppConfig defaults, which also run on
// empty storage before the ini is merged and would be indistinguishable from a user-written key.
bool bed_is_floor()
{
    const AppConfig *ac = app_config();
    if (ac == nullptr)
        return true;
    return ac->has("orca_ext_snap_drag_bed") ? ac->get_bool("orca_ext_snap_drag_bed") : true;
}

// DEFAULT OFF, so a plain get_bool() (absent == false) is already the right default.
bool move_as_group()
{
    const AppConfig *ac = app_config();
    return ac != nullptr && ac->get_bool("orca_ext_snap_drag_group");
}

bool &panel_open()
{
    static bool s_open = false;
    return s_open;
}

// The panel and its magnet are always reachable; the panel itself states that AS-1 free-Z is
// required and dims the controls when it is off.
bool plate_icon_available()
{
    return true;
}

std::optional<FloorHit> floor_z_for_instance(const GLVolumeCollection &volumes, int object_idx,
                                             int instance_idx,
                                             const std::set<std::pair<int, int>> &moving,
                                             double engage_ratio)
{
    const InstanceFootprint mine = compute_footprint(volumes, object_idx, instance_idx);
    if (!mine.valid)
        return std::nullopt;

    const double mine_area = std::abs(mine.hull.area());
    if (mine_area <= 0.0)
        return std::nullopt;

    std::set<std::pair<int, int>> seen;
    bool                          found = false;
    FloorHit                      best;

    for (const GLVolume *v : volumes.volumes) {
        if (v == nullptr || v->is_wipe_tower || v->is_modifier)
            continue;

        const std::pair<int, int> id(v->object_idx(), v->instance_idx());
        if (id.first == object_idx && id.second == instance_idx)
            continue; // never my own instance
        if (moving.count(id) != 0)
            continue; // part of the same drag: never rest on your own group
        if (!seen.insert(id).second)
            continue; // multi-volume instance already evaluated

        const InstanceFootprint cand = compute_footprint(volumes, id.first, id.second);
        if (!cand.valid)
            continue;

        const ExPolygons inter = intersection_ex(Polygons{mine.hull}, Polygons{cand.hull});
        double          inter_area = 0.0;
        for (const ExPolygon &ep : inter)
            inter_area += ep.area();

        const double overlap_ratio = inter_area / mine_area;
        if (overlap_ratio < engage_ratio)
            continue;

        // Sample the candidate's REAL geometry inside the largest overlap region, so a hollow box
        // (tall rim, low interior floor) resolves to the actual surface under the overlap.
        const ExPolygon *biggest_inter      = nullptr;
        double           biggest_inter_area = 0.0;
        for (const ExPolygon &ep : inter) {
            const double a = ep.area();
            if (biggest_inter == nullptr || a > biggest_inter_area) {
                biggest_inter      = &ep;
                biggest_inter_area = a;
            }
        }

        std::vector<Vec3d>          cand_hits;
        const std::optional<double> cand_top = (biggest_inter != nullptr)
            ? sample_real_top_z(volumes, id.first, id.second, sample_points_mm(*biggest_inter),
                                cand.max_z + 1.0, cand.max_z, &cand_hits)
            : std::optional<double>(cand.max_z);

        // Real geometry has a hole exactly under the overlap: this candidate is not a floor here.
        if (!cand_top.has_value())
            continue;

        // Highest overlapping surface wins, so dropping over a stack rests on the tallest thing
        // under the footprint rather than sinking into a shorter neighbour.
        if (!found || *cand_top > best.z) {
            best.z        = *cand_top;
            best.is_bed   = false;
            best.obj_idx  = id.first;
            best.inst_idx = id.second;
            best.contact  = (biggest_inter != nullptr) ? *biggest_inter : ExPolygon(mine.hull);
            best.samples  = std::move(cand_hits);
            found         = true;
        }
    }

    // The bed as a last-place candidate (only when Allow Bed is on). Its Z is 0, so "highest wins"
    // means it can never outrank a real object. The recognised zone is the instance's footprint.
    if (!found && bed_is_floor()) {
        best.z        = 0.0;
        best.is_bed   = true;
        best.obj_idx  = -1;
        best.inst_idx = -1;
        best.contact  = ExPolygon(mine.hull);
        best.samples.clear();
        found = true;
    }

    if (!found)
        return std::nullopt;
    best.z = std::max(best.z, 0.0);
    return best;
}

std::optional<std::pair<int, int>> support_in_group(const GLVolumeCollection &volumes, int object_idx,
                                                    int instance_idx,
                                                    const std::set<std::pair<int, int>> &group,
                                                    double engage_ratio, double max_gap)
{
    const InstanceFootprint mine = compute_footprint(volumes, object_idx, instance_idx);
    if (!mine.valid)
        return std::nullopt;
    const double mine_area = std::abs(mine.hull.area());
    if (mine_area <= 0.0)
        return std::nullopt;

    std::optional<std::pair<int, int>> best;
    double                             best_top = -std::numeric_limits<double>::infinity();

    for (const std::pair<int, int> &id : group) {
        if (id.first == object_idx && id.second == instance_idx)
            continue;

        const InstanceFootprint cand = compute_footprint(volumes, id.first, id.second);
        if (!cand.valid)
            continue;

        // Must be under me and close enough to count as contact. EPS_ABOVE tolerates the sub-micron
        // overshoot a previous snap leaves behind.
        constexpr double EPS_ABOVE = 0.05;
        const double     gap       = mine.min_z - cand.max_z;
        if (cand.max_z > mine.min_z + EPS_ABOVE || gap > max_gap)
            continue;

        const ExPolygons inter = intersection_ex(Polygons{mine.hull}, Polygons{cand.hull});
        double           inter_area = 0.0;
        for (const ExPolygon &ep : inter)
            inter_area += ep.area();
        if (inter_area / mine_area < engage_ratio)
            continue;

        if (!best.has_value() || cand.max_z > best_top) {
            best_top = cand.max_z;
            best     = id;
        }
    }
    return best;
}

Polygon instance_footprint(const GLVolumeCollection &volumes, int object_idx, int instance_idx)
{
    return compute_footprint(volumes, object_idx, instance_idx).hull;
}

} // namespace GravitySnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r
