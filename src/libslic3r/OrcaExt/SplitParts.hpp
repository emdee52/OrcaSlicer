// [ORCAPORT FILE] SplitParts - move selected parts of one object into their own objects
// Original work on the fork's own feature loop (MB-4); no Neotko/preFlight source.
#ifndef slic3r_OrcaExt_SplitParts_hpp_
#define slic3r_OrcaExt_SplitParts_hpp_

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {

class ModelObject;

namespace OrcaExt {

// Replace each of `vol_idxs` (indices into object.volumes, every one of them a MODEL_PART) with
// its own object in the same model, at its current world position, and remove those parts from
// `object`. Everything that is not selected stays in `object`: its other parts, its instances,
// its transform and its settings.
//
// Each new object copies the source object's instances (one per copy, like "Split to objects"),
// takes the part's name and per-part settings, and carries the part's painting unchanged.
// `new_object_idxs` receives the new object indices in model order. Returns false and explains
// itself in `error` when nothing was changed - at least one model part must remain in `object`,
// or the result would be an object without a solid part.
bool split_parts_to_objects(ModelObject &object, const std::vector<size_t> &vol_idxs,
                            std::vector<size_t> &new_object_idxs, std::string &error);

} // namespace OrcaExt
} // namespace Slic3r

#endif // slic3r_OrcaExt_SplitParts_hpp_
