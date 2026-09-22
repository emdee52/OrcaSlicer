#ifndef libslic3r_EdgeProfiles_hpp_
#define libslic3r_EdgeProfiles_hpp_

#include "Point.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {

// Chamfer/fillet cross sections as 2D polygons in an (u, v) frame, plus the solid that a chamfer
// or fillet on a straight mesh edge is cut with. Everything here is mesh-only: the result is a
// closed manifold negative volume, which the slicer subtracts per layer.

// Number of segments on the circular arc of a fillet.
inline constexpr int EDGE_FILLET_SEGMENTS = 16;

// Right triangle with legs `size` along +u and +v. Counter-clockwise (positive area).
std::vector<Vec2d> chamfer_profile(double size);

// Tangent circular-arc lune of radius `radius`: the region between the two axes and a quarter
// circle that touches both, i.e. a square corner minus the quarter disc. Counter-clockwise.
std::vector<Vec2d> fillet_profile(double radius, int segments = EDGE_FILLET_SEGMENTS);

// Extrude a CCW, star-shaped (about the origin) profile along `w` from `z0` to `z1`, with the
// profile point (a, b) mapped to `origin + a * u + b * v`. Returns a closed manifold prism with
// outward normals; the winding is corrected against its_volume, so the handedness of (u, v, w)
// does not matter. Returns an empty mesh for a degenerate profile or range.
indexed_triangle_set extrude_profile(const std::vector<Vec2d> &profile, const Vec3d &origin,
                                     const Vec3d &u, const Vec3d &v, const Vec3d &w, double z0,
                                     double z1);

// A chamfer or fillet on the straight edge p0 -> p1, whose two adjacent faces have outward
// normals `n_a` and `n_b`. `size` is the chamfer leg / fillet radius, measured along each face.
// The solid is over-extended by `margin` past both edge ends so neighbouring negatives at corners
// overlap and subtract cleanly. Returns an empty mesh for a degenerate edge or non-positive size.
indexed_triangle_set make_edge_chamfer(const Vec3d &p0, const Vec3d &p1, const Vec3d &n_a,
                                       const Vec3d &n_b, double size, double margin = 0.);
indexed_triangle_set make_edge_fillet(const Vec3d &p0, const Vec3d &p1, const Vec3d &n_a,
                                      const Vec3d &n_b, double size, double margin = 0.,
                                      int segments = EDGE_FILLET_SEGMENTS);

} // namespace Slic3r

#endif // libslic3r_EdgeProfiles_hpp_
