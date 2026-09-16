// [ORCAPORT FILE] OrcaExt InstanceContact - see InstanceContact.hpp.
// Source: NEOTKOCM_RELEASE_2_39.md; fork sha c3508e5a92.

#include "InstanceContact.hpp"

#include "libslic3r/libslic3r.h"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/PrintConfig.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {
namespace OrcaExt {

// Pair pruning margin. Perf-only: a pair of instances whose XY bboxes are farther apart than this
// can only matter to a tree branch leaning >50 mm outside its own object, so accepting that miss
// keeps plates of well-separated objects at zero cost.
static constexpr double XOBJ_PRUNE_MARGIN_MM = 50.0;

bool cross_object_active(const PrintObject &object)
{
    const Print *print = object.print();
    // By-object printing: the neighbour may not exist yet at a given Z, and the physical head
    // clearance is handled elsewhere, so gate to by-layer only.
    return object.config().support_cross_object_avoidance.value
        && print != nullptr
        && print->config().print_sequence == PrintSequence::ByLayer;
}

// [ORCAPORT:PF-10-multisupport] See header.
std::vector<Polygons> support_layers_occupancy(const PrintObject &object, const std::vector<SupportLayer *> &layers)
{
    const size_t          num_layers = object.layer_count();
    std::vector<Polygons> occupancy(num_layers);
    if (num_layers == 0 || layers.empty())
        return occupancy;

    size_t jb = 0;
    for (size_t ia = 0; ia < num_layers; ++ia) {
        const Layer *la       = object.get_layer(int(ia));
        const double a_bottom = la->bottom_z();
        const double a_top    = la->print_z;
        while (jb < layers.size() && layers[jb]->print_z <= a_bottom + EPSILON)
            ++jb;
        Polygons geo;
        for (size_t j = jb; j < layers.size(); ++j) {
            if (layers[j]->bottom_z() >= a_top - EPSILON)
                break;
            const SupportLayer *sl = layers[j];
            polygons_append(geo, to_polygons(sl->support_islands));
            polygons_append(geo, to_polygons(sl->base_areas));
            polygons_append(geo, to_polygons(sl->tree_roof_areas()));
            polygons_append(geo, to_polygons(sl->tree_roof_1st_layer()));
            polygons_append(geo, to_polygons(sl->tree_floor_areas()));
        }
        if (!geo.empty())
            occupancy[ia] = union_(geo);
    }
    return occupancy;
}

std::vector<Polygons> neighbor_occupancy(const PrintObject &object)
{
    // [ORCAPORT:PF-10-multisupport] Earlier passes' supports are obstacles for every later pass too,
    // independent of the PerObject Support toggle.
    std::vector<Polygons> occupancy = object.support_pass_obstacles();
    bool                  any       = false;
    for (Polygons &polys : occupancy)
        if (!polys.empty()) {
            polys = union_(polys);
            any   = true;
        }

    const Print *print = object.print();
    if (print == nullptr || object.layer_count() == 0)
        return occupancy;

    // 2D bbox of an object's local geometry, accumulated once from the per-island bboxes.
    auto object_bbox = [](const PrintObject &po) {
        BoundingBox bb;
        for (const Layer *layer : po.layers())
            for (const BoundingBox &b : layer->lslices_bboxes)
                bb.merge(b);
        return bb;
    };

    const BoundingBox a_bbox = object_bbox(object);
    if (!cross_object_active(object) || !a_bbox.defined)
        return any ? occupancy : std::vector<Polygons>{};
    const coord_t margin = scale_(XOBJ_PRUNE_MARGIN_MM);

    const size_t num_layers = object.layer_count();
    if (occupancy.size() < num_layers)
        occupancy.resize(num_layers);

    for (const PrintObject *other : print->objects()) {
        if (other == &object || other->layer_count() == 0)
            continue;

        // The neighbour's ALREADY GENERATED support is an obstacle too (tree-vs-tree entanglement).
        // Deterministic because opted-in objects generate their support serially in plate order.
        // SupportLayer::lslices only carries the brim/skirt outline, so gather the real printed
        // geometry: support_islands (classic) plus the tree base/roof/floor areas.
        std::vector<std::pair<const Layer *, Polygons>> b_support;
        if (other->is_step_done(posSupportMaterial))
            for (const SupportLayer *sl : other->support_layers()) {
                Polygons geo = to_polygons(sl->support_islands);
                polygons_append(geo, to_polygons(sl->base_areas));
                polygons_append(geo, to_polygons(sl->tree_roof_areas()));
                polygons_append(geo, to_polygons(sl->tree_roof_1st_layer()));
                polygons_append(geo, to_polygons(sl->tree_floor_areas()));
                if (!geo.empty())
                    b_support.emplace_back(sl, std::move(geo));
            }

        // XY deltas (B-local -> A-local: local_B + shift_B - shift_A) that survive bbox pruning.
        // Conservative multi-instance union: every copy of A sees every copy of B, because the
        // support is generated once per object and replicated per instance.
        BoundingBox b_bbox = object_bbox(*other);
        for (const auto &sl : b_support)
            b_bbox.merge(get_extents(sl.second));
        if (!b_bbox.defined)
            continue;
        std::vector<Point> deltas;
        for (const PrintInstance &ai : object.instances())
            for (const PrintInstance &bi : other->instances()) {
                const Point  delta = bi.shift - ai.shift;
                BoundingBox b_in_a = b_bbox;
                b_in_a.translate(delta.x(), delta.y());
                b_in_a.offset(margin);
                if (b_in_a.overlap(a_bbox))
                    deltas.push_back(delta);
            }
        if (deltas.empty())
            continue;

        // Z mapping by real [bottom_z, print_z) range overlap (adaptive layer heights - never by
        // index). Both stacks ascend, so a single forward cursor over B suffices. Runs once over
        // B's body layers and once over B's generated support layers.
        auto accumulate_stack = [&](size_t stack_size, auto &&layer_at, auto &&geo_at) {
            size_t jb = 0;
            for (size_t ia = 0; ia < num_layers; ++ia) {
                const Layer *la       = object.get_layer(int(ia));
                const double a_bottom = la->bottom_z();
                const double a_top    = la->print_z;
                while (jb < stack_size && layer_at(jb)->print_z <= a_bottom + EPSILON)
                    ++jb;
                Polygons b_local;
                for (size_t j = jb; j < stack_size; ++j) {
                    if (layer_at(j)->bottom_z() >= a_top - EPSILON)
                        break;
                    polygons_append(b_local, geo_at(j));
                }
                if (b_local.empty())
                    continue;
                b_local = union_(b_local);
                for (const Point &delta : deltas) {
                    Polygons shifted = b_local;
                    for (Polygon &poly : shifted)
                        poly.translate(delta);
                    polygons_append(occupancy[ia], std::move(shifted));
                }
                any = true;
            }
        };
        accumulate_stack(other->layer_count(),
            [&](size_t j) { return other->get_layer(int(j)); },
            [&](size_t j) { return to_polygons(other->get_layer(int(j))->lslices); });
        if (!b_support.empty())
            accumulate_stack(b_support.size(),
                [&](size_t j) { return b_support[j].first; },
                [&](size_t j) -> const Polygons & { return b_support[j].second; });
    }

    if (!any)
        return {};
    for (Polygons &polys : occupancy)
        if (!polys.empty())
            polys = union_(polys);
    return occupancy;
}

} // namespace OrcaExt
} // namespace Slic3r
