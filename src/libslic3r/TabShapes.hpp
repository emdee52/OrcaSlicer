#ifndef libslic3r_TabShapes_hpp_
#define libslic3r_TabShapes_hpp_

// [ORCAPORT:TB-1] The geometry of a mounting tab: a plate lying in a picked face's plane, extruded
// along its normal, with a screw passage (a drilled hole or an open slot) and an optional boss.
//
// Geometry only - no UI, no HoleStandards and no volume bookkeeping. The caller resolves the screw
// standard into a TabScrew and owns the ModelVolumes; this file owns the shape.
//
//   FRAME. Everything here is in ONE unit - the caller's units, which the gizmo takes as "mm in the
//   volume's own mesh space" (it divides the panel's mm by the volume's uniform scale first):
//     +x   outward, from the root edge through the passage to the tip
//     +y   the width direction
//     +z   out of the surface (the face normal); z = 0 IS the surface
//   The passage - the drilled hole, or the slot's closed end - is centred on the ORIGIN, so the
//   click that places a tab and the fields that move it only move the passage, and every other
//   dimension is measured from it. That is what makes "align it where you want on that face" one
//   gesture instead of five.
//
// Two invariants exist because this tree has already paid for the bugs that break them:
//   * every solid here reaches `bite` BELOW z = 0. A solid that ends flush with the face is a
//     zero-thickness contact for the union, which is ME-3's sliver seen from the other side.
//   * the screw cutter's entrance cap starts ABOVE the tab's top - `make_tab_screw` does it, and the
//     same note explains why a cap lying in the face skins the recess it opens.
//
// The outline is one parametric shape, not a menu: the only real choice in the plate is its TIP, and
// the Taper row shapes everything else. The flank runs at exactly `taper_deg` from the root corner,
// and the tip is a circle tangent to it whose radius follows from the taper - so "taper 12 degrees"
// means 12 degrees and the tip width is the value that answers to it. TabTip::Flat instead cuts the
// flanks at the tip, which gives a straight end (a trapezoid once it is tapered).

#include "ExPolygon.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// What the far end of the tab does.
enum class TabTip { Round, Flat };

// How the screw reaches the tab: a drilled hole, or an open slot from the tip so the tab can be
// slid onto a screw that is already in place.
enum class TabPassage { Hole, Slot };

enum class TabSeat { None, Counterbore, Countersink };

struct TabSpec
{
    // Outline, in the frame above. All values are >= 0 and are clamped by tab_outline().
    double root_w{14.};      // tab width at the root edge
    double root_len{6.};     // passage centre -> root edge
    double tip_len{10.};     // passage centre -> the tab's outermost point
    double taper_deg{12.};   // flank angle away from the axis, each side
    TabTip tip{TabTip::Round};
    double min_wall{2.};     // material kept around the passage

    TabPassage passage{TabPassage::Hole};
    double     passage_r{1.7}; // screw clearance radius (hole diameter / 2)
    double     slot_w{0.};     // 0 -> 2 * passage_r

    double thickness{3.};      // plate height above the surface (z = 0 .. thickness)
    // How far the root reaches INTO the material. It is a constant in the tool, not a field: the
    // slicer unions the tab with the part per layer, so a root edge that only TOUCHES the face is a
    // pinch vertex there - the tab's walls and the part's meet at a point and the root snaps off.
    // Entering by a millimetre of material is what makes the junction real.
    double weld{1.0};
    double boss_d{0.};         // 0 -> no boss
    double boss_h{0.};
};

// The screw passage as a negative volume. `depth` is measured down from the tab's top and the tool
// always takes it through the tab (and a hair past its far face, the same overshoot the entrance
// gets): a mounting tab's hole is a clearance hole for a screw that bites whatever is below it, and
// a floor exactly flush with that surface would be the coplanar pair ME-3 documents.
struct TabScrew
{
    double  d{3.4};          // clearance bore diameter
    TabSeat seat{TabSeat::None};
    double  seat_d{6.};      // counterbore diameter / countersink mouth diameter
    double  seat_depth{3.};  // counterbore: recess depth below the top
    double  seat_angle{90.}; // countersink: INCLUDED angle, degrees
    double  depth{0.};       // bore depth below the tab's top; <= 0 cuts nothing
};

// What the outline resolved to. `clamped` means one of the minimum-material rules moved the shape,
// which the panel reports rather than hiding.
struct TabOutline
{
    ExPolygons poly;        // one contour, no holes (a slot opens through the tip)
    double     tip_w{0.};   // width of the tip the shape actually got
    double     taper_deg{0.}; // the flank angle actually used (the clamp can lower it)
    double     slot_w{0.};  // slot width actually used, 0 unless the passage is a slot
    bool       clamped{false};
};

// The footprint in the frame above. Empty when the spec cannot make a tab at all.
TabOutline tab_outline(const TabSpec &spec);

// The plate's top surface: `thickness`, plus the boss when there is one. The screw is entered from
// here, so it is also what the panel means by "depth below the top".
double tab_top(const TabSpec &spec);

// The plate and the (optional) boss as positive solids, in the frame. Both reach `bite` below z = 0,
// so neither meets the surface or the other one flush; the boss is a plain cylinder that passes
// through the plate rather than starting on its top face.
indexed_triangle_set make_tab_plate(const TabSpec &spec);
indexed_triangle_set make_tab_boss(const TabSpec &spec);

// The screw as one negative solid: the seat at the top and the bore below it. Empty for a slot (the
// slot is part of the outline, so it is not cut) or when nothing would be removed.
indexed_triangle_set make_tab_screw(const TabSpec &spec, const TabScrew &screw);

// How the mesh sits under a footprint: the fraction of the sampled footprint that has the surface
// directly below it, and how far that surface falls away from the plane. `max_drop` is how far below
// the plane a hit still counts as the surface rather than the object's far side, so it is also the
// limit on a bite that can still reach it. `frame_to_its` maps the frame above into the mesh.
struct TabSurfaceProbe
{
    double coverage{0.};
    double fall_away{0.};
};
TabSurfaceProbe tab_probe_surface(const indexed_triangle_set &its, const Transform3d &frame_to_its,
                                  const ExPolygons &footprint, double max_drop, int nx = 13, int ny = 9);

// --- Placement ------------------------------------------------------------------------------------
// A tab is a MOUNTING tab, so it is placed PARALLEL TO THE BED with its ROOT attached to the face that
// was clicked: on a wall that is a foot standing out sideways, screwed down from above, not a plate
// lying in the wall's own plane (the first cut got that wrong and it read as a slab stuck to the part).
//
// `face_normal` is the picked facet's normal (out of the material, unit), `click` the picked point and
// `centre` the object's own centre, all in object space; `root_len` / `bite` are in object units. A
// face within ~25 degrees of horizontal keeps the plate in its own plane, which is the same thing for
// a bottom or top face and is what a foot under a part wants; anything steeper is a WALL and gets a
// plate parallel to the bed, with its root edge on the face and the screw `root_len` out from it.
struct TabFrame
{
    Transform3d to_obj;     // the tab's frame -> object space: +x outward, +z the plate's normal
    bool        wall{false};// the face was treated as a wall (the plate is parallel to the bed)
    Vec3d       axis{ Vec3d::UnitX() }; // the frame's +x (for the panel's readout)
};
TabFrame tab_frame(const Vec3d &face_normal, const Vec3d &click, const Vec3d &centre, double root_len,
                   double weld, double angle_deg);

} // namespace Slic3r

#endif // libslic3r_TabShapes_hpp_
