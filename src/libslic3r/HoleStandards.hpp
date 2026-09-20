#ifndef libslic3r_HoleStandards_hpp_
#define libslic3r_HoleStandards_hpp_

#include <string>
#include <vector>

namespace Slic3r {

// Common mechanical hole/pocket standards, shared by the CAD Hole feature and the mesh
// bore/pocket tool. All dimensions are millimetres so they feed straight into either backend.
enum class HoleStandardKind {
    Screw,  // clearance/tap bore, optionally with a counterbore / countersink for the head
    Nut,    // hex nut pocket (across-flats) with a coaxial clearance bore
    Insert, // heat-set threaded insert pocket
    Magnet, // round magnet pocket
};

struct HoleStandard {
    std::string      designation; // "M3", "#8-32", "M3 insert", "6x3 magnet"
    HoleStandardKind kind{ HoleStandardKind::Screw };

    // Screw: through/blind clearance diameter for a free (non-threaded) fit. 0 when the screw is
    // only meant to be tapped into plastic.
    double clearance_d{ 0. };
    // Screw: thread-forming (tap) diameter for plastic - the lower 50%-thread steel tap drill.
    double tap_d{ 0. };
    // Screw head features, per head type (0 when the standard carries none).
    double socket_d{ 0. };  // socket-head cap screw head diameter (DIN 912 dk)
    double socket_k{ 0. };  // socket-head cap screw head height (DIN 912 k)
    double button_d{ 0. };  // button-head socket screw head diameter (ISO 7380 dk)
    double button_k{ 0. };  // button-head socket screw head height (ISO 7380 k)
    double csink_d{ 0. };   // countersunk head diameter (DIN 7991 dk)
    double csink_angle{ 90. };

    // Insert / magnet: nominal pocket outer diameter and depth.
    double pocket_d{ 0. };
    double pocket_depth{ 0. };

    // Nut: across-flats width (s). The pocket depth is `pocket_depth` (nut height, m) and the
    // coaxial clearance bore uses `clearance_d`.
    double across_flats{ 0. };
};

// All standards, in table order.
const std::vector<HoleStandard> &hole_standards();

// Case-sensitive designation lookup; nullptr when unknown.
const HoleStandard *find_hole_standard(const std::string &designation);

// Pocket fit, expressed as a diameter delta added to the nominal pocket diameter.
enum class HoleFit { Tight, Slip };

// Extra diameter (mm) for a pocket of nominal diameter `nominal_d` with the given fit. Scales
// slightly with the diameter so larger pockets get proportionally more clearance.
double hole_fit_diameter_delta(HoleStandardKind kind, double nominal_d, HoleFit fit);

// A screw's nominal diameter for a free fit (tap == false) or a thread-forming tap fit
// (tap == true); falls back to whichever exists. Returns 0 for non-screw standards.
double screw_nominal_diameter(const HoleStandard &s, bool tap);

} // namespace Slic3r

#endif // libslic3r_HoleStandards_hpp_
