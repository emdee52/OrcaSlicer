#ifndef libslic3r_HoleShapes_hpp_
#define libslic3r_HoleShapes_hpp_

#include "HoleDetector.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// Number of segments around the circular part of a hole shape.
inline constexpr int HOLE_SHAPE_SEGMENTS = 64;

// A teardrop prism: a circle of `radius` whose top cap is replaced by two lines meeting on
// the vertical axis at `angle_deg` from vertical, extruded along +Z for `depth`.
//
// Local frame: cross-section in XY with the apex pointing +Y, extrusion along +Z, z in
// [0, depth]. Non-convex, but star-shaped about the circle centre, so the caps fan safely.
indexed_triangle_set its_make_teardrop(double radius, double depth,
                                       double angle_deg = 45.,
                                       int    segments  = HOLE_SHAPE_SEGMENTS);

// `its_make_teardrop` placed on a detected hole: centred on the hole axis, extruded along it
// for `depth` straddling `hole.center`, with the apex pointing up (world +Z projected into
// the hole's cross-section). Returns an empty mesh for a vertical hole, where a teardrop has
// no meaning.
indexed_triangle_set its_make_teardrop_for_hole(const DetectedHole &hole, double depth,
                                                double angle_deg = 45.,
                                                int    segments  = HOLE_SHAPE_SEGMENTS);

} // namespace Slic3r

#endif // libslic3r_HoleShapes_hpp_
