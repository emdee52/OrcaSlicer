#ifndef slic3r_OrcaExt_FreeZ_hpp_
#define slic3r_OrcaExt_FreeZ_hpp_

// [ORCAPORT:AS-1] App-level "free Z" placement preference.
//
// When enabled, the GUI stops force-snapping a moved object back down to the bed, so objects
// can be floated in Z to align against each other without assembling them. Printing is done
// after assembling, so this is placement-only: it never changes slicing.

namespace Slic3r {
namespace OrcaExt {

void set_free_z(bool enabled);
bool free_z();

} // namespace OrcaExt
} // namespace Slic3r

#endif
