#include "HoleStandards.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

// Representative tables, not a certified standard library. Screw clearances follow ISO 273
// medium / ISO 4762 socket-head and ANSI unified; counterbore/countersink diameters are the head
// dimensions for those screws, and the seat columns (cbore_d / cbore_depth / csink_pocket_d) are
// the values upstream OrcaSlicer's CAD hole table carries, kept row for row so an upstream update
// is a straight copy here. Insert and magnet values are common off-the-shelf sizes and can
// be corrected here without touching any feature code.
const std::vector<HoleStandard> &standards_table()
{
    static const std::vector<HoleStandard> table = [] {
        std::vector<HoleStandard> t;

        auto screw = [&](const char *desig, double clearance, double tap, double socket_d, double socket_k,
                         double button_d, double button_k, double csink_d,
                         double cbore_d, double cbore_depth, double csink_pocket_d, double csink_angle = 90.) {
            HoleStandard s;
            s.designation  = desig;
            s.kind         = HoleStandardKind::Screw;
            s.clearance_d  = clearance;
            s.tap_d        = tap;
            s.socket_d     = socket_d;
            s.socket_k     = socket_k;
            s.button_d     = button_d;
            s.button_k     = button_k;
            s.csink_d      = csink_d;
            s.csink_angle  = csink_angle;
            s.cbore_d        = cbore_d;
            s.cbore_depth    = cbore_depth;
            s.csink_pocket_d = csink_pocket_d;
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
        auto insert = [&](const char *desig, double d, std::vector<double> heights) {
            HoleStandard s;
            s.designation    = desig;
            s.kind           = HoleStandardKind::Insert;
            s.pocket_d       = d;
            s.insert_heights = std::move(heights);
            s.pocket_depth   = s.insert_heights.empty() ? 0. : s.insert_heights.front();
            t.push_back(std::move(s));
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
        screw("M2",   0.0, 1.70,  3.8, 2.0, 0.0, 0.0, 0.0,   0.0,  0.0,  0.0);
        screw("M2.2", 0.0, 1.90,  0.0, 0.0, 0.0, 0.0, 0.0,   0.0,  0.0,  0.0);
        screw("M2.5", 0.0, 2.20,  4.5, 2.5, 0.0, 0.0, 0.0,   0.0,  0.0,  0.0);
        // Free (clearance) and tap (plastic thread-forming) fits, with socket / button / countersunk
        // heads (DIN 912 dk/k, ISO 7380 dk/k, DIN 7991 dk) and the seat the head sits in
        // (DIN 974-1 counterbore for ISO 4762, ISO 10642 countersink seat).
        screw("M3",   3.4, 2.60,  5.5,  3.0,  5.70, 1.65,  6.0,   6.5,  3.4,  6.7);
        screw("M4",   4.5, 3.50,  7.0,  4.0,  7.60, 2.20,  8.0,   8.0,  4.4,  9.0);
        screw("M5",   5.5, 4.40,  8.5,  5.0,  9.50, 2.75, 10.0,  10.0,  5.4, 11.3);
        screw("M6",   6.6, 5.40, 10.0,  6.0, 10.50, 3.30, 12.0,  11.0,  6.4, 13.4);
        screw("M8",   9.0, 7.20, 13.0,  8.0, 14.00, 4.40, 16.0,  15.0,  8.6, 17.9);
        screw("M10", 11.0, 9.00, 16.0, 10.0, 17.50, 5.50, 20.0,  18.0, 10.6, 22.4);
        // Inch sizes: ASME B18.3 / B18.6.3 seats, 82 deg flat heads - an inch flat head is made
        // with 82, and a 90 seat under it bears only on the head rim. find_hole_standard() still
        // accepts the bare size ("#6-32", "1/4-20") without the UNC suffix.
        screw("#6-32 UNC",    3.7, 0.0,  6.4, 3.8, 0.0, 0.0,  7.1,   6.4,  3.8,  7.1, 82.);
        screw("#8-32 UNC",    4.5, 0.0,  7.9, 4.6, 0.0, 0.0,  8.4,   7.9,  4.6,  8.4, 82.);
        screw("#10-24 UNC",   5.1, 0.0,  9.5, 5.3, 0.0, 0.0,  9.8,   9.5,  5.3,  9.8, 82.);
        screw("1/4-20 UNC",   6.8, 0.0, 11.1, 6.9, 0.0, 0.0, 12.9,  11.1,  6.9, 12.9, 82.);
        screw("5/16-18 UNC",  8.4, 0.0, 13.5, 8.6, 0.0, 0.0, 16.1,  13.5,  8.6, 16.1, 82.);
        screw("3/8-16 UNC",  10.1, 0.0, 15.9, 10.2, 0.0, 0.0, 19.4,  15.9, 10.2, 19.4, 82.);

        // Hex nuts (across-flats s, height m) with the matching screw clearance bore. M2 and M2.5
        // are omitted on purpose.
        nut("M3 nut", 5.50, 2.40, 3.4);
        nut("M4 nut", 7.00, 3.20, 4.5);
        nut("M5 nut", 8.00, 4.00, 5.5);
        nut("M6 nut", 10.00, 5.00, 6.6);
        nut("M8 nut", 13.00, 6.50, 9.0);
        nut("M10 nut", 17.00, 8.00, 11.0);

        // Heat-set inserts: max outer diameter and the available heights, from a common M2-M5
        // brass kit. The chosen height maps to the (editable) pocket depth.
        insert("M2 insert", 3.0, {2.0});
        insert("M2.5 insert", 3.5, {2.5});
        insert("M3 insert", 4.2, {3.0, 5.0, 7.0});
        insert("M4 insert", 5.5, {6.0, 8.0});
        insert("M5 insert", 7.0, {8.0, 10.0});

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
    auto exact = std::find_if(table.begin(), table.end(), [&designation](const HoleStandard &s) {
        return s.designation == designation;
    });
    if (exact != table.end())
        return &*exact;
    // Bare spelling of a series-suffixed designation ("1/4-20" for "1/4-20 UNC").
    auto bare = std::find_if(table.begin(), table.end(), [&designation](const HoleStandard &s) {
        const size_t sp = s.designation.find(' ');
        return sp != std::string::npos && designation.size() == sp &&
               s.designation.compare(0, sp, designation) == 0;
    });
    return bare == table.end() ? nullptr : &*bare;
}

double hole_fit_diameter_delta(HoleStandardKind /*kind*/, double /*nominal_d*/, HoleFit fit)
{
    // Flat diameter deltas: a slip pocket is 0.16 mm larger than nominal, a tight one 0.05 mm.
    switch (fit) {
    case HoleFit::Tight: return 0.05;
    case HoleFit::Slip:  return 0.16;
    }
    return 0.16;
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
