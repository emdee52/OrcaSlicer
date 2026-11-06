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

// Union of the positive parts and subtraction of the negative ones, in the object's coordinate
// system: every volume's own transform applied, no instance transform. Positives are pushed first
// so the difference always has something to cut. The pre-flight is the one the whole-object
// boolean uses (Plater::combine_mesh_fff): the kernel that runs is mcut, whose check only rejects
// an empty part, while the stricter CGAL predicates would refuse meshes mcut handles.
//
// On a kernel refusal the positives are joined unwelded (as combine_mesh_fff does); `welded`
// tells which happened. With negatives in the selection the caller must not use that fallback -
// the cuts would be lost.
TriangleMesh union_and_subtract_parts(const ModelObject &object, const std::vector<size_t> &positives,
                                      const std::vector<size_t> &negatives, bool &welded)
{
    std::vector<csg::CSGPart> parts;
    parts.reserve(positives.size() + negatives.size());
    for (size_t vol_idx : positives) {
        const ModelVolume *volume = object.volumes[vol_idx];
        parts.emplace_back(&volume->mesh().its, csg::CSGType::Union, volume->get_matrix().cast<float>());
        parts.back().name = volume->name;
    }
    for (size_t vol_idx : negatives) {
        const ModelVolume *volume = object.volumes[vol_idx];
        parts.emplace_back(&volume->mesh().its, csg::CSGType::Difference, volume->get_matrix().cast<float>());
        parts.back().name = volume->name;
    }

    welded = false;
    const Range range{std::begin(parts), std::end(parts)};
    if (std::get<0>(csg::check_csgmesh_booleans(range, /*use_mcut=*/true)) == csg::BooleanFailReason::OK) {
        try {
            MeshBoolean::mcut::McutMeshPtr boolean_mesh = csg::perform_csgmesh_booleans_mcut(range);
            TriangleMesh                  mesh         = MeshBoolean::mcut::mcut_to_triangle_mesh(*boolean_mesh);
            if (!mesh.empty()) {
                welded = true;
                return mesh;
            }
        } catch (...) {
            // Fall through to joining the positive parts without a boolean.
        }
    }

    BOOST_LOG_TRIVIAL(info) << "merge_parts_into_one: the boolean refused the selected parts, joining the positives unwelded.";
    return TriangleMesh(csg::csgmesh_merge_positive_parts(parts));
}

} // namespace

bool merge_parts_into_one(ModelObject &object, const std::vector<size_t> &vol_idxs, bool keep_painting,
                          bool &welded, std::string &error)
{
    welded = false;
    error.clear();

    std::vector<size_t> sorted = vol_idxs;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());
    if (sorted.empty()) {
        error = "at least one part is needed";
        return false;
    }

    // Model parts are unioned, negative volumes are the difference tools; anything else
    // (modifiers, support enforcers/blockers, Precise Seam volumes) has no CSG meaning.
    std::vector<size_t> positives, negatives;
    for (size_t vol_idx : sorted) {
        if (vol_idx >= object.volumes.size()) {
            error = "part index out of range";
            return false;
        }
        const ModelVolume *volume = object.volumes[vol_idx];
        if (volume->is_model_part())
            positives.emplace_back(vol_idx);
        else if (volume->is_negative_volume())
            negatives.emplace_back(vol_idx);
        else {
            error = "only model parts and negative volumes can be merged";
            return false;
        }
    }
    if (positives.empty()) {
        error = "a positive part is needed";
        return false;
    }
    // Nothing to do with one positive alone; negatives alone have nothing to cut.
    if (positives.size() < 2 && negatives.empty()) {
        error = "at least two model parts, or a model part and a negative volume, are needed";
        return false;
    }

    // The lowest selected positive keeps the part order stable and lends its name and per-part
    // settings to the result; the other selected parts' settings go away with those parts.
    const size_t       insert_at = positives.front();
    const std::string  name      = object.volumes[insert_at]->name;
    DynamicPrintConfig config    = object.volumes[insert_at]->config.get();

    // Painting is remapped from object space onto the merged mesh, so it survives the parts'
    // own transforms being baked into the boolean.
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    if (keep_painting) {
        for (size_t vol_idx : positives) {
            const ModelVolume *volume = object.volumes[vol_idx];
            if (!volume->is_any_painted())
                continue;
            saved_paintings.emplace_back(volume->save_painting());
            if (saved_paintings.back())
                saved_paintings.back()->mesh.transform(volume->get_matrix(), true);
        }
    }

    TriangleMesh mesh = union_and_subtract_parts(object, positives, negatives, welded);
    if (mesh.empty()) {
        error = "the merged mesh is empty";
        return false;
    }
    // A difference that could not run would leave the cutters' holes missing; keep everything as
    // it is rather than consume the cutters for nothing.
    if (!welded && !negatives.empty()) {
        error = "the boolean kernel refused the selected parts; nothing was changed";
        return false;
    }

    // The selected volumes are removed directly rather than with delete_volume(): that helper
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
