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
// for `depth` straddling `hole.center`, with the apex pointing along `up_dir` projected into
// the hole's cross-section (default: world +Z). Pass the bed-up direction expressed in the
// same frame as the hole when the object is rotated. Returns an empty mesh for a vertical
// hole, where a teardrop has no meaning.
indexed_triangle_set its_make_teardrop_for_hole(const DetectedHole &hole, double depth,
                                                double angle_deg = 45.,
                                                int    segments  = HOLE_SHAPE_SEGMENTS,
                                                const Vec3d &up_dir = Vec3d::UnitZ());

// --- Bore / pocket primitives -----------------------------------------------------------------
// All are closed solids extruded along `axis`, with their entrance at `entry` (a point on the
// axis) growing in the +axis direction. They are rotationally symmetric, so no up direction is
// needed. Use them as negative volumes (bores) or, for the tube, a positive volume.

// Straight cylindrical bore.
indexed_triangle_set its_make_bore(double diameter, double depth,
                                   const Vec3d &axis, const Vec3d &entry,
                                   int segments = HOLE_SHAPE_SEGMENTS);

// Clearance bore with a counterbore (larger cylindrical recess) at the entrance.
indexed_triangle_set its_make_counterbore(double clearance_d, double cbore_d, double cbore_depth,
                                          double depth, const Vec3d &axis, const Vec3d &entry,
                                          int segments = HOLE_SHAPE_SEGMENTS);

// Clearance bore with a countersink at the entrance. The sink depth is derived from
// `csink_angle` (included angle, degrees) so the cone meets the clearance diameter.
indexed_triangle_set its_make_countersink(double clearance_d, double csink_d, double csink_angle,
                                          double depth, const Vec3d &axis, const Vec3d &entry,
                                          int segments = HOLE_SHAPE_SEGMENTS);

// Positive annulus (tube): fills the gap between an existing `outer_d` wall and a new
// `inner_d` bore, i.e. shrinks a hole. `outer_d` should overlap the existing wall.
indexed_triangle_set its_make_tube(double outer_d, double inner_d, double depth,
                                   const Vec3d &axis, const Vec3d &entry,
                                   int segments = HOLE_SHAPE_SEGMENTS);

// Hexagonal nut pocket (across-flats) with a coaxial clearance bore through it.
indexed_triangle_set its_make_nut_pocket(double across_flats, double pocket_depth,
                                         double clearance_d, double bore_depth,
                                         const Vec3d &axis, const Vec3d &entry,
                                         int segments = HOLE_SHAPE_SEGMENTS);

// --- Rim dress-up -----------------------------------------------------------------------------
// A chamfer or fillet on the circular edge where a hole meets a flat face. Both are closed rings
// (torus topology) whose inner radius is `hole_radius`; `size` is the chamfer leg / fillet radius
// and therefore the ring's radial and axial extent. Placed like the bores above: origin at `entry`
// (a point on the axis at the face), growing in the +axis direction into the material. Use as
// negative volumes; they only meet material for r >= `hole_radius`.

indexed_triangle_set its_make_rim_chamfer(double hole_radius, double size, const Vec3d &axis,
                                          const Vec3d &entry, int segments = HOLE_SHAPE_SEGMENTS);

indexed_triangle_set its_make_rim_fillet(double hole_radius, double size, const Vec3d &axis,
                                         const Vec3d &entry, int segments = HOLE_SHAPE_SEGMENTS);

// Distance from `entry` to the far side of `its` along `dir`, plus `margin`. `dir` must point into
// the material. Used to make a through-hole reach the opposite wall exactly. Returns 0 when the ray
// leaves without hitting anything, so the caller can fall back to a fixed depth.
double hole_through_depth(const indexed_triangle_set &its, const Vec3d &entry, const Vec3d &dir,
                          double margin = 0.5);

// --- Cavity fill ------------------------------------------------------------------------------
// A positive plug that fills a depression (engraving, watermark, pocket). Grows the facet region
// geodesically from the seed facets (on the cavity floor or walls) out to `radius` around their
// seed points (mesh space), stopping where the surface turns over a convex ridge (the cavity rim),
// then returns the convex hull of the region's vertices. The hull lies inside the solid everywhere
// except across the concavity, so a positive volume combined with the object fills the cavity
// without a boolean. Returns an empty mesh when no concave junction was crossed, so painting a flat
// or convex wall adds nothing.
indexed_triangle_set cavity_fill_hull(const indexed_triangle_set &its,
                                      const std::vector<Vec3d> &seed_points,
                                      const std::vector<int> &seed_facets, double radius);
indexed_triangle_set cavity_fill_hull(const indexed_triangle_set &its, const Vec3d &seed_point,
                                      int seed_facet, double radius);

} // namespace Slic3r

#endif // libslic3r_HoleShapes_hpp_
