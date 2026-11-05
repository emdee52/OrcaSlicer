// [ORCAPORT FILE] SplitParts - move selected parts of one object into their own objects
// Original work on the fork's own feature loop (MB-4); no Neotko/preFlight source.
#include "SplitParts.hpp"

#include "../Geometry.hpp"
#include "../Model.hpp"
#include "../PrintConfig.hpp"
#include "../TriangleMesh.hpp"

#include <algorithm>

namespace Slic3r {
namespace OrcaExt {

bool split_parts_to_objects(ModelObject &object, const std::vector<size_t> &vol_idxs,
                            std::vector<size_t> &new_object_idxs, std::string &error)
{
    new_object_idxs.clear();
    error.clear();

    Model *model = object.get_model();
    if (model == nullptr) {
        error = "the object does not belong to a model";
        return false;
    }
    if (vol_idxs.empty()) {
        error = "at least one part is needed";
        return false;
    }

    std::vector<size_t> sorted = vol_idxs;
    std::sort(sorted.begin(), sorted.end());
    sorted.erase(std::unique(sorted.begin(), sorted.end()), sorted.end());

    size_t model_part_count = 0;
    for (const ModelVolume *volume : object.volumes)
        if (volume->is_model_part())
            ++model_part_count;

    for (size_t vol_idx : sorted) {
        if (vol_idx >= object.volumes.size()) {
            error = "part index out of range";
            return false;
        }
        const ModelVolume *volume = object.volumes[vol_idx];
        if (!volume->is_model_part()) {
            error = "only model parts can be split into objects";
            return false;
        }
        if (volume->mesh().facets_count() < 3) {
            error = "a selected part is empty";
            return false;
        }
    }
    // An object needs at least one solid part to stay in the scene.
    if (sorted.size() >= model_part_count) {
        error = "the object would be left without a model part";
        return false;
    }

    new_object_idxs.reserve(sorted.size());
    for (size_t vol_idx : sorted) {
        const ModelVolume *volume = object.volumes[vol_idx];

        ModelObject *new_object = model->add_object();
        new_object->name = volume->name.empty() ? object.name : volume->name;
        new_object->config.assign_config(object.config);
        new_object->config.apply(volume->config, true);

        // Each copy of the source object becomes a copy of the extracted part, placed exactly
        // where it was: the instance keeps the source instance's flags and takes the composed
        // transform (instance * part), so the part needs no transform of its own.
        for (const ModelInstance *instance : object.instances) {
            ModelInstance *new_instance = new_object->add_instance(*instance);
            const Geometry::Transformation placement(instance->get_matrix() * volume->get_matrix());
            new_instance->set_transformation(placement);
            new_instance->set_assemble_transformation(placement);
        }

        // The volume is centred on creation (offset instead of raw mesh coordinates), which does
        // not touch the triangle order, so the part's painting applies as it is.
        TriangleMesh  mesh(volume->mesh());
        ModelVolume  *new_volume = new_object->add_volume(std::move(mesh), ModelVolumeType::MODEL_PART,
                                                          /*modify_to_center_geometry=*/true);
        new_volume->supported_facets.assign(volume->supported_facets);
        new_volume->seam_facets.assign(volume->seam_facets);
        new_volume->mmu_segmentation_facets.assign(volume->mmu_segmentation_facets);
        new_volume->fuzzy_skin_facets.assign(volume->fuzzy_skin_facets);
        new_volume->counterbore_bridge_facets.assign(volume->counterbore_bridge_facets);

        // A single-part object carries its settings on the object, as Split to objects does.
        new_volume->config.reset();
        new_volume->source = ModelVolume::Source();

        new_object_idxs.push_back(model->objects.size() - 1);
    }

    // Descending order keeps the remaining indices valid; when a single part is left,
    // delete_volume() folds its transform into the instances, which is the invariant a
    // one-part object needs.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it)
        object.delete_volume(*it);

    // The object is no longer the file it was loaded from.
    object.input_file.clear();
    return true;
}

} // namespace OrcaExt
} // namespace Slic3r
