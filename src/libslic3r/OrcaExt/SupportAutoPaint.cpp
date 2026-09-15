// [ORCAPORT FILE] SupportAutoPaint - automatic painting of support regions
// Source: preFlight v1.3.0 "Automatic painting" (fork sha f74dc69), reimplemented Orca-native.
#include "SupportAutoPaint.hpp"

#include <algorithm>
#include <cmath>
#include <set>

#include "../AABBTreeIndirect.hpp"
#include "../Layer.hpp"
#include "../Model.hpp"
#include "../Print.hpp"
#include "../TriangleMesh.hpp"
#include "../libslic3r.h"
#include "SupportPaintTypes.hpp"

namespace Slic3r {
namespace OrcaExt {

namespace {

using Tree = AABBTreeIndirect::Tree<3, float>;

// Cast `origin` along `dir` (world/centered coords) against a volume mesh given by its world
// transform; return the nearest hit face index when the ray hits a downward-facing facet.
bool nearest_facet_above(const indexed_triangle_set      &its,
                         const Tree                      &tree,
                         const Transform3d               &world,
                         const Vec3d                     &origin,
                         size_t                          &facet_out)
{
    const Transform3d inv = world.inverse();
    const Vec3d       o   = inv * origin;
    const Vec3d       d   = inv.linear() * Vec3d(0., 0., 1.);
    std::vector<igl::Hit<float>> hits;
    if (!AABBTreeIndirect::intersect_ray_all_hits(its.vertices, its.indices, tree, o, d, hits) || hits.empty())
        return false;
    const int face = hits.front().id;
    if (face < 0 || size_t(face) >= its.indices.size())
        return false;
    // Downward-facing only: the facet the support touches from below.
    const Vec3f n = its_face_normal(its, face);
    if ((world.linear() * n.cast<double>()).z() >= 0.)
        return false;
    facet_out = size_t(face);
    return true;
}

double support_height_below(const std::vector<const SupportLayer *> &layers, size_t start_idx,
                            const Point &p, double object_print_z_min)
{
    double height = 0.;
    size_t steps  = 0;
    for (size_t j = start_idx + 1; j-- > 0 && steps < 3000; ++steps) {
        const SupportLayer *sl = layers[j];
        if (sl == nullptr || sl->print_z < object_print_z_min + EPSILON)
            break;
        bool found = false;
        for (const ExPolygon &isl : sl->support_islands)
            if (isl.contains(p)) {
                found = true;
                break;
            }
        if (!found)
            break;
        height += sl->height;
    }
    return height;
}

} // namespace

std::vector<SupportAutoPaintHit> classify_support_paint(const PrintObject &object)
{
    std::vector<SupportAutoPaintHit> hits;

    const ModelObject *mo = object.model_object();
    if (mo == nullptr)
        return hits;

    std::vector<const SupportLayer *> slayers;
    slayers.reserve(object.support_layers().size());
    for (const SupportLayer *sl : object.support_layers())
        if (sl != nullptr)
            slayers.push_back(sl);
    if (slayers.empty())
        return hits;

    const double object_print_z_min = object.slicing_parameters().object_print_z_min;
    const Transform3d obj_trafo     = object.trafo_centered();

    // Build the mesh acceleration structures once per model-part volume.
    struct VolumeRay { const ModelVolume *mv; Transform3d world; const indexed_triangle_set *its; Tree tree; };
    std::vector<VolumeRay> volumes;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        VolumeRay vr;
        vr.mv    = mv;
        vr.world = obj_trafo * mv->get_matrix();
        vr.its   = &mv->mesh().its;
        vr.tree  = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(vr.its->vertices, vr.its->indices);
        volumes.push_back(std::move(vr));
    }
    if (volumes.empty())
        return hits;

    std::set<std::pair<size_t, size_t>> seen;

    for (size_t li = 0; li < slayers.size(); ++li) {
        const SupportLayer *sl = slayers[li];
        if (sl->print_z < object_print_z_min + EPSILON || sl->support_islands.empty())
            continue;

        for (const ExPolygon &island : sl->support_islands) {
            if (island.contour.points.empty())
                continue;
            const double contact_area = area(island) * SCALING_FACTOR * SCALING_FACTOR;

            // Sample the island: its contour vertices plus the centroid.
            Points samples = island.contour.points;
            Point  centroid = island.contour.centroid();
            samples.push_back(centroid);

            for (const Point &p : samples) {
                const Vec3d origin(unscale<double>(p.x()), unscale<double>(p.y()), sl->print_z);
                size_t      facet = 0;
                bool        found = false;
                size_t      volume_index = 0;
                for (size_t vi = 0; vi < volumes.size(); ++vi) {
                    if (nearest_facet_above(*volumes[vi].its, volumes[vi].tree, volumes[vi].world, origin, facet)) {
                        volume_index = vi;
                        found        = true;
                        break;
                    }
                }
                if (!found)
                    continue;
                if (!seen.emplace(volume_index, facet).second)
                    continue;

                const double height = support_height_below(slayers, li, p, object_print_z_min);
                const EnforcerBlockerType state = support_paint_classify(contact_area, height);
                if (state == EnforcerBlockerType::NONE)
                    continue;

                SupportAutoPaintHit hit;
                hit.volume_index      = volume_index;
                hit.facet_index       = facet;
                hit.state             = state;
                hit.contact_area_mm2  = contact_area;
                hit.support_height_mm = height;
                hits.push_back(hit);
            }
        }
    }
    return hits;
}

} // namespace OrcaExt
} // namespace Slic3r
