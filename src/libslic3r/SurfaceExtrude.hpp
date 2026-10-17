// [ORCAPORT FILE] SurfaceExtrude - turn a flat face of a mesh into a solid
// Source: NEOTKOCM_RELEASE_2_48.md; fork sha 2c41b67aa3
//
// Geometry only, no UI. Four steps, in order:
//   1. coplanar_region() - grow a flat region outwards from one picked facet, stopping where the
//      surface stops being that plane (normal within acos(cos_thresh) of the seed's).
//   2. region_outline()  - project that region to XY in a frame where the face points +Z, and union
//      the triangles into a polygon outline, holes kept.
//   3. outline_holes()   - optionally keep only the holes, each one filled (e.g. the circle of a
//      screw hole, to grow into a plug).
//   4. extrude_outline() - build a closed prism from the outline, grown or shrunk by an offset,
//      from z0 to z0 + height.
//
// The caller is expected to rotate the face normal to +Z before calling (a pure rotation, so
// nothing is distorted) and to rotate the result back afterwards; see GLGizmoExtrude.
//
// The fork's `s363c` half - a debug-only curved extrusion with tension nodes - is deliberately not
// ported (it was marked hidden and undocumented on purpose).
#ifndef slic3r_SurfaceExtrude_hpp_
#define slic3r_SurfaceExtrude_hpp_

#include <cstddef>
#include <vector>

#include "ExPolygon.hpp"
#include "Point.hpp"
#include "TriangleMesh.hpp"

namespace Slic3r {
namespace SurfaceExtrude {

// Facets connected to `seed` whose normal stays within acos(cos_thresh) of the seed's normal.
// `normals` / `neighbors` come from its_face_normals() / its_face_neighbors().
std::vector<int> coplanar_region(const std::vector<Vec3f>   &normals,
                                 const std::vector<Vec3i32> &neighbors,
                                 int                         seed,
                                 float                       cos_thresh,
                                 size_t                      max_facets = 40000);

// The region projected onto XY after `trafo` (scaled coordinates), holes kept.
// `mean_z` receives the average Z of the region's vertices after `trafo`.
ExPolygons region_outline(const indexed_triangle_set &its,
                          const std::vector<int>     &region,
                          const Transform3d          &trafo,
                          double                     *mean_z = nullptr);

// The holes of `outline`, each one as a filled shape (counter-clockwise, no holes of its own).
ExPolygons outline_holes(const ExPolygons &outline);

// Closed prism of `outline` grown/shrunk by `offset_mm`, from z0 to z0 + height.
// Empty when the offset eats the whole shape or height <= 0.
indexed_triangle_set extrude_outline(const ExPolygons &outline,
                                     double            offset_mm,
                                     double            z0,
                                     double            height);

} // namespace SurfaceExtrude
} // namespace Slic3r

#endif // slic3r_SurfaceExtrude_hpp_
