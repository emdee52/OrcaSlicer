// [ORCAPORT FILE] SupportAutoPaint - automatic painting of support regions
// Source: preFlight v1.3.0 "Automatic painting" (fork sha f74dc69), reimplemented Orca-native.
#include "SupportAutoPaint.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <unordered_map>

#include "../AABBTreeIndirect.hpp"
#include "../Model.hpp"
#include "../TriangleMesh.hpp"
#include "../libslic3r.h"
#include "SupportPaintTypes.hpp"

namespace Slic3r {
namespace OrcaExt {

namespace {

constexpr double PI = 3.14159265358979323846;

using Tree = AABBTreeIndirect::Tree<3, float>;

struct VolumeData
{
    const indexed_triangle_set *its{nullptr};
    Transform3d                 world{Transform3d::Identity()};
    Tree                        tree;
};

// Disjoint-set over facets for connected-component clustering.
struct DSU
{
    std::vector<int> parent;

    void reset(size_t n)
    {
        parent.resize(n);
        for (size_t i = 0; i < n; ++i)
            parent[i] = int(i);
    }
    int find(int x)
    {
        while (parent[x] != x) {
            parent[x] = parent[parent[x]];
            x         = parent[x];
        }
        return x;
    }
    void unite(int a, int b)
    {
        a = find(a);
        b = find(b);
        if (a != b)
            parent[a] = b;
    }
};

// Distance from `world_origin` straight down to the nearest model surface, or a huge value when
// nothing is below. Used as the "support would sit in a tight/inaccessible gap" signal.
double ray_down_to_next_surface(const Vec3d &world_origin, const std::vector<VolumeData> &vdatas)
{
    double best = 1.0e30;
    for (const VolumeData &vd : vdatas) {
        if (vd.its == nullptr || vd.its->indices.empty())
            continue;
        const Transform3d inv = vd.world.inverse();
        const Vec3d       o   = inv * world_origin;
        const Vec3d       d   = inv.linear() * Vec3d(0., 0., -1.);
        if (d.squaredNorm() < 1.0e-12)
            continue;
        igl::Hit<float> hit;
        if (AABBTreeIndirect::intersect_ray_first_hit(vd.its->vertices, vd.its->indices, vd.tree, o, d, hit)) {
            const Vec3d  local_p = o + d * double(hit.t);
            const Vec3d  world_p = vd.world * local_p;
            const double dist    = world_origin.z() - world_p.z();
            if (dist > 1.0e-6 && dist < best)
                best = dist;
        }
    }
    return best;
}

} // namespace

std::vector<SupportAutoPaintHit> classify_support_paint(
    const ModelObject                       &model_object,
    const Transform3d                       &instance_trafo,
    const std::vector<std::vector<uint8_t>> &painted,
    const SupportAutoPaintParams            &params)
{
    std::vector<SupportAutoPaintHit> hits;

    // Build the per-model-part volume data, keeping the model-part index aligned with the caller's
    // TriangleSelector list. Empty parts stay in the list (with a null mesh) so indices do not shift.
    std::vector<VolumeData> vdatas;
    for (const ModelVolume *mv : model_object.volumes) {
        if (!mv->is_model_part())
            continue;
        VolumeData vd;
        const indexed_triangle_set &its = mv->mesh().its;
        if (!its.indices.empty()) {
            vd.its   = &its;
            vd.world = instance_trafo * mv->get_matrix();
            vd.tree  = AABBTreeIndirect::build_aabb_tree_over_indexed_triangle_set(its.vertices, its.indices);
        }
        vdatas.push_back(std::move(vd));
    }
    if (vdatas.empty())
        return hits;

    // Overhang criterion, mirroring GLGizmoFdmSupports::select_facets_by_angle. When the user is not
    // restricting to the highlighted set, use 90 degrees: every downward-facing facet is a candidate.
    const double theta_deg = params.overhangs_only
                                 ? std::clamp(double(params.overhang_angle_deg), 1.0, 90.0)
                                 : 90.0;
    const double theta_rad = theta_deg * PI / 180.0;

    for (size_t vi = 0; vi < vdatas.size(); ++vi) {
        const VolumeData &vd = vdatas[vi];
        if (vd.its == nullptr)
            continue;
        const indexed_triangle_set &its       = *vd.its;
        const size_t                nfacets   = its.indices.size();

        const std::vector<Vec3i32> neighbors = its_face_neighbors(its);
        const std::vector<Vec3f>   normals   = its_face_normals(its);

        // Local frame of the highlight predicate.
        Eigen::Matrix3d inv_lin = vd.world.linear().inverse();
        Vec3d           down    = (inv_lin * (-Vec3d::UnitZ())).normalized();
        Vec3d limit = (inv_lin * Vec3d(std::sin(theta_rad), 0., -std::cos(theta_rad))).normalized();
        const double dot_limit = limit.dot(down);

        // World vertices (for area / span / height / curvature / gap).
        std::vector<Vec3d> wv(its.vertices.size());
        for (size_t i = 0; i < its.vertices.size(); ++i)
            wv[i] = vd.world * its.vertices[i].cast<double>();

        const std::vector<uint8_t> *painted_mask =
            vi < painted.size() && painted[vi].size() == nfacets ? &painted[vi] : nullptr;

        std::vector<uint8_t> candidate(nfacets, 0);
        for (size_t f = 0; f < nfacets; ++f) {
            if (painted_mask != nullptr && (*painted_mask)[f] != 0)
                continue;
            if (normals[f].cast<double>().dot(down) > dot_limit)
                candidate[f] = 1;
        }

        DSU dsu;
        dsu.reset(nfacets);
        for (size_t f = 0; f < nfacets; ++f) {
            if (!candidate[f])
                continue;
            for (int n : neighbors[f])
                if (n >= 0 && size_t(n) < nfacets && candidate[size_t(n)])
                    dsu.unite(int(f), n);
        }

        std::unordered_map<int, std::vector<size_t>> components;
        for (size_t f = 0; f < nfacets; ++f)
            if (candidate[f])
                components[dsu.find(int(f))].push_back(f);

        for (const auto &entry : components) {
            const std::vector<size_t> &facets = entry.second;

            SupportRegionFeatures feat;
            double                area_sum  = 0.;
            double                best_sev  = -2.;
            Vec3d                 normal_sum = Vec3d::Zero();
            Vec3d                 bb_min(std::numeric_limits<double>::max(), std::numeric_limits<double>::max(), std::numeric_limits<double>::max());
            Vec3d                 bb_max(-std::numeric_limits<double>::max(), -std::numeric_limits<double>::max(), -std::numeric_limits<double>::max());

            for (size_t f : facets) {
                const Vec3i32 &tri = its.indices[f];
                const double   a   = 0.5 * ((wv[tri[1]] - wv[tri[0]]).cross(wv[tri[2]] - wv[tri[0]])).norm();
                area_sum += a;
                const Vec3d nw = (vd.world.linear() * normals[f].cast<double>()).normalized();
                normal_sum += nw * a;
                best_sev = std::max(best_sev, normals[f].cast<double>().dot(down));
                for (int k = 0; k < 3; ++k)
                    for (int axis = 0; axis < 3; ++axis) {
                        bb_min(axis) = std::min(bb_min(axis), wv[tri[k]](axis));
                        bb_max(axis) = std::max(bb_max(axis), wv[tri[k]](axis));
                    }
            }

            feat.area_mm2 = area_sum;
            feat.span_mm  = std::max(bb_max.x() - bb_min.x(), bb_max.y() - bb_min.y());
            feat.height_mm = bb_min.z();
            feat.curvature = area_sum > 0.
                                 ? std::clamp(1. - normal_sum.norm() / area_sum, 0., 1.)
                                 : 0.;
            const double sev     = std::clamp(best_sev, -1., 1.);
            feat.wall_angle_deg  = 90. - std::acos(sev) * 180. / PI;

            const Vec3d gap_origin((bb_min.x() + bb_max.x()) * 0.5,
                                   (bb_min.y() + bb_max.y()) * 0.5,
                                   bb_min.z() - 0.01);
            feat.gap_below_mm = ray_down_to_next_surface(gap_origin, vdatas);

            const EnforcerBlockerType state = support_paint_classify(feat, params.enabled_types);
            if (state == EnforcerBlockerType::NONE)
                continue;

            hits.reserve(hits.size() + facets.size());
            for (size_t f : facets) {
                SupportAutoPaintHit hit;
                hit.volume_index = vi;
                hit.facet_index  = f;
                hit.state        = state;
                hit.features     = feat;
                hits.push_back(hit);
            }
        }
    }

    return hits;
}

} // namespace OrcaExt
} // namespace Slic3r
