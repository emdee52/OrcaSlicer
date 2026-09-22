// [ORCAPORT:SNAP-8] OrcaExt ObjectSnap - see ObjectSnap.hpp.

#include "ObjectSnap.hpp"

#include <algorithm>
#include <limits>
#include <vector>

#include <glad/gl.h>

#include "slic3r/GUI/3DScene.hpp"                // GLVolume, GLVolumeCollection
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLShader.hpp"               // GLShaderProgram
#include "slic3r/GUI/GUI.hpp"                    // glsafe
#include "slic3r/GUI/GUI_App.hpp"                // wxGetApp
#include "slic3r/GUI/Gizmos/GLGizmosCommon.hpp"  // coplanar_region, build_face_snap_points, world_to_screen
#include "slic3r/GUI/MeshUtils.hpp"              // MeshRaycaster::unproject_on_mesh

#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"            // its_make_sphere / its_merge / its_translate

namespace Slic3r {
namespace OrcaExt {
namespace Gui {
namespace ObjectSnap {

namespace {

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

// Radius of the drawn candidate spheres, in world units. Shared with Markers::set so the picture and
// the pick agree: what you see is what you can grab.
double candidate_radius(const std::vector<SnapCandidate> &candidates)
{
    double diag = 0.0;
    for (size_t i = 0; i < candidates.size(); ++i)
        for (size_t j = i + 1; j < candidates.size(); ++j)
            diag = std::max(diag, (candidates[i].world - candidates[j].world).norm());
    // Face-relative like the Holes gizmo; the floor keeps a lone raw hit (curved face) visible and the
    // ceiling keeps a huge face from sprouting giant blobs.
    return std::clamp(0.012 * diag, 0.3, 1.5);
}

// Pick the sphere the cursor is actually on: the candidate whose drawn disc comes closest to the
// cursor, plus a small grab margin. A pure screen-distance test (the shared nearest_face_snap) can
// hand the pick to a sphere tens of pixels away, which is what made aiming at one edge of a face
// grab a feature of the neighbouring face and land the object offset.
bool pick_by_sphere(const std::vector<SnapCandidate> &candidates, const GUI::Camera &camera, double radius,
                    const Vec2d &mouse, size_t &active)
{
    constexpr double GRAB_MARGIN_PX = 8.0;
    const Vec3d      dir_right      = camera.get_dir_right().normalized();

    double best_score = std::numeric_limits<double>::max();
    bool   found      = false;
    for (size_t i = 0; i < candidates.size(); ++i) {
        const Vec2d center = GUI::world_to_screen(camera, candidates[i].world);
        const Vec2d edge   = GUI::world_to_screen(camera, candidates[i].world + dir_right * radius);
        if (!std::isfinite(center.x()) || !std::isfinite(center.y()) || !std::isfinite(edge.x()) || !std::isfinite(edge.y()))
            continue; // behind the camera
        const double score = (mouse - center).norm() - (edge - center).norm();
        if (score > GRAB_MARGIN_PX)
            continue;
        // Strictly closer wins, so candidates emitted first (higher FaceSnapKind priority) hold ties.
        if (score < best_score - 1e-9) {
            best_score = score;
            active     = i;
            found      = true;
        }
    }
    return found;
}

// Closest visible volume under the cursor whose (object, instance) is or is not in the drag set,
// and its nearest snap coordinate. Same rule as GLGizmosCommon's raycast_object_face, except that
// the target set is the drag set here rather than the selection.
std::optional<SnapHit> hit_filtered(const Model &model, const GLVolumeCollection &volumes,
                                    const GUI::Camera &camera,
                                    const std::set<std::pair<int, int>> &moving, const Vec2d &mouse,
                                    bool want_moving)
{
    const GLVolume *hit_volume = nullptr;
    size_t          hit_facet  = 0;
    Vec3d           hit_world  = Vec3d::Zero();
    double          closest    = std::numeric_limits<double>::max();

    for (const GLVolume *v : volumes.volumes) {
        if (v == nullptr || v->mesh_raycaster == nullptr || v->is_wipe_tower || v->is_modifier ||
            v->is_sla_pad() || v->is_sla_support())
            continue;
        if ((moving.count({ v->object_idx(), v->instance_idx() }) != 0) != want_moving)
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

    const Transform3d world = hit_volume->world_matrix();

    // Curved or otherwise ungroupable face: the raw hit is the one candidate. Kept in the volume's
    // own mesh space so it goes through the same transform as the grouped ones.
    std::vector<FaceSnapPoint>        fallback;
    const std::vector<FaceSnapPoint> *pts = &candidates_for(mo->volumes[vol_idx], hit_facet);
    if (pts->empty()) {
        fallback.push_back({ FaceSnapKind::None, world.inverse() * hit_world });
        pts = &fallback;
    }

    // Hand the whole face back, in world space, so the caller can draw every coordinate; `active` is
    // the one the grab point lands on.
    SnapHit out;
    out.candidates.reserve(pts->size());
    for (const FaceSnapPoint &p : *pts)
        out.candidates.push_back({ world * p.pos, p.kind });
    if (out.candidates.empty()) // cannot happen: the fallback always holds one point
        return std::nullopt;

    size_t active = 0;
    if (!pick_by_sphere(out.candidates, camera, candidate_radius(out.candidates), mouse, active))
        return std::nullopt;

    out.point = out.candidates[active];
    return out;
}

} // namespace

std::optional<SnapHit> hit_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                        const GUI::Camera &camera,
                                        const std::set<std::pair<int, int>> &moving, const Vec2d &mouse)
{
    return hit_filtered(model, volumes, camera, moving, mouse, false);
}

std::optional<SnapHit> mover_hit_under_cursor(const Model &model, const GLVolumeCollection &volumes,
                                              const GUI::Camera &camera,
                                              const std::set<std::pair<int, int>> &moving, const Vec2d &mouse)
{
    return hit_filtered(model, volumes, camera, moving, mouse, true);
}

void Markers::set(const std::vector<SnapCandidate> &candidates, size_t active, MarkerRole role)
{
    clear();
    if (candidates.empty())
        return;

    const double radius = candidate_radius(candidates);

    static const ColorRGBA KIND_COLOR[5] = {
        ColorRGBA(1.00f, 0.30f, 0.85f, 1.0f), // corner
        ColorRGBA(0.35f, 0.90f, 0.75f, 1.0f), // edge midpoint
        ColorRGBA(0.30f, 0.80f, 1.00f, 1.0f), // face centre
        ColorRGBA(0.60f, 0.95f, 0.45f, 1.0f), // edge quarter
        ColorRGBA(0.55f, 0.72f, 1.00f, 1.0f), // face quarter
    };

    indexed_triangle_set acc[5];
    for (const SnapCandidate &c : candidates) {
        const int k = int(c.kind) - 1;
        if (k < 0 || k >= 5)
            continue;
        indexed_triangle_set sphere = its_make_sphere(radius, PI / 12.0);
        its_translate(sphere, c.world.cast<float>());
        its_merge(acc[k], sphere);
    }
    for (int k = 0; k < 5; ++k) {
        if (!acc[k].indices.empty()) {
            m_kind[k].init_from(acc[k]);
            m_kind[k].set_color(KIND_COLOR[k]);
        }
    }

    if (active < candidates.size()) {
        indexed_triangle_set sphere = its_make_sphere(radius * 1.4, PI / 12.0);
        its_translate(sphere, candidates[active].world.cast<float>());
        m_active.init_from(sphere);
        m_active.set_color(role == MarkerRole::Mover ? ColorRGBA(1.00f, 0.55f, 0.10f, 1.0f)
                                                     : ColorRGBA(1.00f, 1.00f, 1.00f, 1.0f));
    }
    m_visible = true;
}

void Markers::clear()
{
    for (GUI::GLModel &m : m_kind)
        m.reset();
    m_active.reset();
    m_visible = false;
}

void Markers::render(const GUI::Camera &camera)
{
    if (!m_visible)
        return;

    GLShaderProgram *shader = GUI::wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();
    shader->set_uniform("view_model_matrix", camera.get_view_matrix());
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    // Depth test off so the candidates stay readable through the object they belong to.
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    for (GUI::GLModel &m : m_kind)
        m.render();
    m_active.render();
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));

    shader->stop_using();
}

} // namespace ObjectSnap
} // namespace Gui
} // namespace OrcaExt
} // namespace Slic3r
