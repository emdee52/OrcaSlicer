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

        auto screw = [&](const char *desig, double clearance, double cbore_d, double cbore_depth, double csink_d) {
            HoleStandard s;
            s.designation   = desig;
            s.kind          = HoleStandardKind::Screw;
            s.clearance_d   = clearance;
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

        screw("M3", 3.4, 6.0, 3.4, 6.3);
        screw("M4", 4.5, 8.0, 4.4, 8.4);
        screw("M5", 5.5, 10.0, 5.4, 10.4);
        screw("M6", 6.6, 11.0, 6.8, 12.6);
        screw("M8", 9.0, 15.0, 8.8, 17.3);
        screw("M10", 11.0, 18.0, 11.0, 20.0);
        screw("#6-32", 3.7, 8.8, 4.2, 8.7);
        screw("#8-32", 4.4, 9.9, 5.1, 10.2);
        screw("1/4-20", 6.9, 14.4, 7.2, 14.7);
        screw("5/16-18", 8.8, 17.0, 8.2, 17.3);
        screw("3/8-16", 10.5, 19.6, 9.5, 19.8);

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

} // namespace Slic3r
