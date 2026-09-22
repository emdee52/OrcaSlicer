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

// One straight run of a closed feature loop, from `p` to `q`, with `u` lying in the bordered face
// (pointing away from the crease) and `v` in the neighbouring face. Runs are merged along a flat
// side, and the frame belongs to the run rather than to a vertex, so the cross section stays
// perpendicular to the boundary and one turn of the boundary produces exactly one miter.
struct LoopFrame
{
    Vec3d p{ Vec3d::Zero() };
    Vec3d q{ Vec3d::Zero() };
    Vec3d u{ Vec3d::Zero() };
    Vec3d v{ Vec3d::Zero() };
};

// Boundary loops of the coplanar face patch that contains `seed_face`, in world space after
// `trafo` (normals are transformed with its inverse transpose). A patch can carry several loops,
// e.g. a top face with holes; each loop is a closed ring of segment frames, and degenerate loops
// are dropped, so the result may be empty. `normal_tol` is the component-wise normal match that
// grows the patch.
std::vector<std::vector<LoopFrame>> its_face_patch_loops(const indexed_triangle_set &its,
                                                         const Transform3d &trafo, int seed_face,
                                                         float normal_tol = 1e-3f);

// As above, but when the patch of `seed_face` carries no loop the search is repeated for the facets
// in the rings around it, up to `max_rings` rings out, and the loops of the first ring that has any
// are returned. This is what makes a rim pickable from the smooth band that borders it (a bevel, or
// the rounded side of a low boss), where the patch under the cursor has no rim of its own. Loops of
// several patches in the same ring are concatenated, so the caller picks the one it wants;
// `max_rings <= 0` reduces to `its_face_patch_loops`.
std::vector<std::vector<LoopFrame>> its_face_patch_loops_around(const indexed_triangle_set &its,
                                                                const Transform3d &trafo,
                                                                int seed_face, int max_rings = 2,
                                                                float normal_tol = 1e-3f);

// Sweep a CCW profile around a closed loop: profile point (a, b) of segment i is placed at
// `p + a * u + b * v` at one end and at `q + a * u + b * v` at the other, and consecutive segments
// are bridged at their shared vertex by a miter ring, so the boundary comes out mitred where the
// frame turns. The result is a closed torus-topology solid with outward normals (the winding is
// corrected against its_volume). Returns an empty mesh for a degenerate loop or profile.
indexed_triangle_set sweep_loop(const std::vector<LoopFrame> &loop, const std::vector<Vec2d> &profile);

// Chamfer / fillet ring around a closed loop; `size` is the chamfer leg / fillet radius, as for
// a straight edge. Returns an empty mesh for a degenerate loop or non-positive size.
indexed_triangle_set make_loop_chamfer(const std::vector<LoopFrame> &loop, double size);
indexed_triangle_set make_loop_fillet(const std::vector<LoopFrame> &loop, double size,
                                      int segments = EDGE_FILLET_SEGMENTS);

} // namespace Slic3r

#endif // libslic3r_EdgeProfiles_hpp_
