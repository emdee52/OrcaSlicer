// [ORCAPORT FILE] MergeParts - fuse selected parts of one object into a single part
// Original work on the fork's own feature loop (MB-3); no Neotko/preFlight source.
#include "MergeParts.hpp"

#include "../CSGMesh/CSGMesh.hpp"
#include "../CSGMesh/PerformCSGMeshBooleans.hpp"
#include "../MeshBoolean.hpp"
#include "../Model.hpp"
#include "../PrintConfig.hpp"
#include "../TriangleMesh.hpp"
#include "../TriangleSelector.hpp"
#include "../libslic3r.h"

#include <algorithm>
#include <iterator>
#include <optional>
#include <utility>

#include <boost/log/trivial.hpp>

namespace Slic3r {
namespace OrcaExt {

namespace {

// Union of the selected parts in the object's coordinate system: every part's own transform
// applied, no instance transform. The pre-flight is the one the whole-object boolean uses
// (Plater::combine_mesh_fff): the kernel that runs is mcut, whose check only rejects an empty
// part, while the stricter CGAL predicates would refuse meshes mcut handles.
TriangleMesh union_selected_parts(const ModelObject &object, const std::vector<size_t> &vol_idxs, bool &welded)
{
    std::vector<csg::CSGPart> parts;
    parts.reserve(vol_idxs.size());
    for (size_t vol_idx : vol_idxs) {
        const ModelVolume *volume = object.volumes[vol_idx];
        parts.emplace_back(&volume->mesh().its, csg::CSGType::Union, volume->get_matrix().cast<float>());
        parts.back().name = volume->name;
    }

    welded = false;
    const Range range{std::begin(parts), std::end(parts)};
    if (std::get<0>(csg::check_csgmesh_booleans(range, /*use_mcut=*/true)) == csg::BooleanFailReason::OK) {
        try {
            MeshBoolean::mcut::McutMeshPtr union_mesh = csg::perform_csgmesh_booleans_mcut(range);
            TriangleMesh                  mesh       = MeshBoolean::mcut::mcut_to_triangle_mesh(*union_mesh);
            if (!mesh.empty()) {
                welded = true;
                return mesh;
            }
        } catch (...) {
            // Fall through to joining the parts without a boolean, as combine_mesh_fff does.
        }
    }

    BOOST_LOG_TRIVIAL(info) << "merge_parts_into_one: the boolean refused the selected parts, joining them unwelded.";
    return TriangleMesh(csg::csgmesh_merge_positive_parts(parts));
}

} // namespace

bool merge_parts_into_one(ModelObject &object, const std::vector<size_t> &vol_idxs, bool keep_painting,
                          bool &welded, std::string &error)
{
    welded = false;
    error.clear();

    if (vol_idxs.size() < 2) {
        error = "at least two parts are needed";
        return false;
    }
    for (size_t vol_idx : vol_idxs) {
        if (vol_idx >= object.volumes.size()) {
            error = "part index out of range";
            return false;
        }
        if (!object.volumes[vol_idx]->is_model_part()) {
            error = "only model parts can be merged";
            return false;
        }
    }

    std::vector<size_t> sorted = vol_idxs;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (sorted.size() < 2) {
        error = "at least two different parts are needed";
        return false;
    }

    // The lowest selected index keeps the part order stable and lends its name and per-part
    // settings to the result; the other selected parts' settings go away with those parts.
    const size_t          insert_at = sorted.front();
    const std::string     name      = object.volumes[insert_at]->name;
    DynamicPrintConfig    config    = object.volumes[insert_at]->config.get();

    // Painting is remapped from object space onto the merged mesh, so it survives the parts'
    // own transforms being baked into the union.
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    if (keep_painting) {
        for (size_t vol_idx : sorted) {
            const ModelVolume *volume = object.volumes[vol_idx];
            if (!volume->is_any_painted())
                continue;
            saved_paintings.emplace_back(volume->save_painting());
            if (saved_paintings.back())
                saved_paintings.back()->mesh.transform(volume->get_matrix(), true);
        }
    }

    TriangleMesh mesh = union_selected_parts(object, sorted, welded);
    if (mesh.empty()) {
        error = "the merged mesh is empty";
        return false;
    }

    // The selected parts are removed directly rather than with delete_volume(): that helper
    // bakes the last remaining volume's transform into the instances when the object is left
    // with one volume, which would be wrong in the middle of this replacement.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        delete object.volumes[*it];
        object.volumes.erase(object.volumes.begin() + *it);
    }

    // Geometry is already in the object's coordinates, so the merged volume keeps an identity
    // transform (no re-centering that would have to be compensated elsewhere).
    ModelVolume *merged = object.add_volume(std::move(mesh), ModelVolumeType::MODEL_PART,
                                           /*modify_to_center_geometry=*/false);
    object.volumes.pop_back();
    object.volumes.insert(object.volumes.begin() + insert_at, merged);

    merged->name = name;
    merged->config.assign_config(std::move(config));
    if (keep_painting)
        for (const std::optional<TriangleSelector::SavedPainting> &saved : saved_paintings)
            if (saved)
                merged->restore_painting(saved, true);

    object.invalidate_bounding_box();
    return true;
}

} // namespace OrcaExt
} // namespace Slic3r
