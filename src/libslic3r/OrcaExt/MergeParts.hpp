// [ORCAPORT FILE] MergeParts - fuse selected parts of one object into a single part
// Original work on the fork's own feature loop (MB-3); no Neotko/preFlight source.
#ifndef slic3r_OrcaExt_MergeParts_hpp_
#define slic3r_OrcaExt_MergeParts_hpp_

#include <cstddef>
#include <string>
#include <vector>

namespace Slic3r {

class ModelObject;

namespace OrcaExt {

// Replace `vol_idxs` (indices into object.volumes, every one of them a MODEL_PART) with the
// union of those parts. The union is computed in the object's own coordinate system, so the
// instances, the object transform, the parts that were not selected and the object's own
// settings stay as they are; the merged part lands at the lowest selected index and takes
// that part's name and per-part settings.
//
// `welded` reports whether the boolean kernel fused the parts. When it refused, the parts are
// joined as they are instead: the object still ends up with one part, but the overlap between
// the joined parts stays unresolved. `error` explains a refusal, in which case the object is
// not modified at all.
bool merge_parts_into_one(ModelObject &object, const std::vector<size_t> &vol_idxs,
                          bool keep_painting, bool &welded, std::string &error);

} // namespace OrcaExt
} // namespace Slic3r

#endif // slic3r_OrcaExt_MergeParts_hpp_
