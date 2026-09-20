#include "HoleStandards.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

// Representative tables, not a certified standard library. Screw clearances follow ISO 273
// medium / ISO 4762 socket-head and ANSI unified; counterbore/countersink diameters are the head
// dimensions for those screws. Insert and magnet values are common off-the-shelf sizes and can
// be corrected here without touching any feature code.
const std::vector<HoleStandard> &standards_table()
{
    static const std::vector<HoleStandard> table = [] {
        std::vector<HoleStandard> t;

        auto screw = [&](const char *desig, double clearance, double tap, double cbore_d, double cbore_depth, double csink_d) {
            HoleStandard s;
            s.designation   = desig;
            s.kind          = HoleStandardKind::Screw;
            s.clearance_d   = clearance;
            s.tap_d         = tap;
            s.cbore_d       = cbore_d;
            s.cbore_depth   = cbore_depth;
            s.csink_d       = csink_d;
            t.push_back(s);
        };
        auto pocket = [&](const char *desig, HoleStandardKind kind, double d, double depth) {
            HoleStandard s;
            s.designation   = desig;
            s.kind          = kind;
            s.pocket_d      = d;
            s.pocket_depth  = depth;
            t.push_back(s);
        };
        auto nut = [&](const char *desig, double across_flats, double height, double clearance) {
            HoleStandard s;
            s.designation   = desig;
            s.kind          = HoleStandardKind::Nut;
            s.across_flats  = across_flats;
            s.pocket_depth  = height;
            s.clearance_d   = clearance; // through hole for the screw
            t.push_back(s);
        };

        // Tap-only sizes (too small for a free fit). Tap = lower 50%-thread steel drill.
        screw("M2", 0.0, 1.70, 0.0, 0.0, 0.0);
        screw("M2.2", 0.0, 1.90, 0.0, 0.0, 0.0);
        screw("M2.5", 0.0, 2.20, 0.0, 0.0, 0.0);
        // Free (clearance) and tap (plastic thread-forming) fits.
        screw("M3", 3.4, 2.60, 6.0, 3.4, 6.3);
        screw("M4", 4.5, 3.50, 8.0, 4.4, 8.4);
        screw("M5", 5.5, 4.40, 10.0, 5.4, 10.4);
        screw("M6", 6.6, 5.40, 11.0, 6.8, 12.6);
        screw("M8", 9.0, 7.20, 15.0, 8.8, 17.3);
        screw("M10", 11.0, 9.00, 18.0, 11.0, 20.0);
        screw("#6-32", 3.7, 0.0, 8.8, 4.2, 8.7);
        screw("#8-32", 4.4, 0.0, 9.9, 5.1, 10.2);
        screw("1/4-20", 6.9, 0.0, 14.4, 7.2, 14.7);
        screw("5/16-18", 8.8, 0.0, 17.0, 8.2, 17.3);
        screw("3/8-16", 10.5, 0.0, 19.6, 9.5, 19.8);

        // Hex nuts (across-flats s, height m) with the matching screw clearance bore. M2 and M2.5
        // are omitted on purpose.
        nut("M3 nut", 5.50, 2.40, 3.4);
        nut("M4 nut", 7.00, 3.20, 4.5);
        nut("M5 nut", 8.00, 4.00, 5.5);
        nut("M6 nut", 10.00, 5.00, 6.6);
        nut("M8 nut", 13.00, 6.50, 9.0);
        nut("M10 nut", 17.00, 8.00, 11.0);

        // Heat-set inserts (OD x length), Ruthex-style brass.
        pocket("M2 insert", HoleStandardKind::Insert, 3.2, 4.0);
        pocket("M2.5 insert", HoleStandardKind::Insert, 3.6, 4.5);
        pocket("M3 insert", HoleStandardKind::Insert, 4.0, 5.7);
        pocket("M4 insert", HoleStandardKind::Insert, 5.6, 8.1);
        pocket("M5 insert", HoleStandardKind::Insert, 6.4, 9.5);
        pocket("M6 insert", HoleStandardKind::Insert, 8.0, 12.7);
        pocket("M8 insert", HoleStandardKind::Insert, 10.0, 12.7);

        // Round magnets (diameter x thickness).
        pocket("3x2 magnet", HoleStandardKind::Magnet, 3.0, 2.0);
        pocket("4x2 magnet", HoleStandardKind::Magnet, 4.0, 2.0);
        pocket("5x2 magnet", HoleStandardKind::Magnet, 5.0, 2.0);
        pocket("6x2 magnet", HoleStandardKind::Magnet, 6.0, 2.0);
        pocket("6x3 magnet", HoleStandardKind::Magnet, 6.0, 3.0);
        pocket("8x2 magnet", HoleStandardKind::Magnet, 8.0, 2.0);
        pocket("8x3 magnet", HoleStandardKind::Magnet, 8.0, 3.0);
        pocket("10x2 magnet", HoleStandardKind::Magnet, 10.0, 2.0);
        pocket("10x3 magnet", HoleStandardKind::Magnet, 10.0, 3.0);
        pocket("12x3 magnet", HoleStandardKind::Magnet, 12.0, 3.0);

        return t;
    }();
    return table;
}

} // namespace

const std::vector<HoleStandard> &hole_standards() { return standards_table(); }

const HoleStandard *find_hole_standard(const std::string &designation)
{
    const std::vector<HoleStandard> &table = standards_table();
    auto it = std::find_if(table.begin(), table.end(), [&designation](const HoleStandard &s) {
        return s.designation == designation;
    });
    return it == table.end() ? nullptr : &*it;
}

double hole_fit_diameter_delta(HoleStandardKind kind, double nominal_d, HoleFit fit)
{
    // Press-fit inserts need less clearance than glue-in magnets.
    const double rel = (kind == HoleStandardKind::Insert) ? 0.005 : 0.010;
    const double d   = std::max(0., nominal_d);
    switch (fit) {
    case HoleFit::Tight: return 0.05 + rel * d;
    case HoleFit::Slip:  return 0.20 + rel * d;
    case HoleFit::Epoxy: return 0.30 + rel * d;
    }
    return 0.20 + rel * d;
}

double screw_nominal_diameter(const HoleStandard &s, bool tap)
{
    if (s.kind != HoleStandardKind::Screw)
        return 0.;
    if (tap && s.tap_d > 0.)
        return s.tap_d;
    if (s.clearance_d > 0.)
        return s.clearance_d;
    return s.tap_d;
}

} // namespace Slic3r
