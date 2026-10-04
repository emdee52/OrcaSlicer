#ifndef slic3r_PerforationPattern_hpp_
#define slic3r_PerforationPattern_hpp_

#include "ExPolygon.hpp"
#include "TriangleMesh.hpp"
#include "Point.hpp"

namespace Slic3r {

// Two-dimensional perforation patterns stamped into a planar face and extruded through it.
// The pattern is the solid that is subtracted (a set of through-holes), or its complement
// when `invert` is set (grooves between the holes).
enum class PerforationKind : int {
    Lines = 0,     // a single family of parallel slots at `angle`
    Cross,         // '+' : families at `angle` and `angle + 90`
    X,             // 'x' : families at `angle + 45` and `angle - 45`
    DualDiagonal,  // two families mirrored about `angle`
    Honeycomb,     // staggered hexagonal holes
};

struct PerforationParams {
    PerforationKind kind{PerforationKind::Honeycomb};
    double angle_deg{0.0};  // in-plane rotation of the pattern
    double spacing{6.0};    // hole / slot centre-to-centre distance, mm
    double width{2.0};      // hole / slot width, mm
    double margin{2.0};     // boundary inset from the face outline, mm
    bool   invert{false};   // true: subtract the material between the holes
};

// The 2D solid to subtract, in the face plane's scaled coordinates. `domain` is the face
// outline (outer contour plus holes). Returns empty when the inset domain or the pattern is
// empty. The result never has overlapping polygons.
ExPolygons make_perforation_pattern(const ExPolygons &domain, const PerforationParams &params);

// Extrude `polys` (face-plane scaled coordinates) into a closed prism. `origin` is the
// face-plane origin, `ux`/`uy` the in-plane axes and `n` the outward normal, all in object
// space and mm. The prism spans n-offsets [z0, z1] relative to `origin`.
indexed_triangle_set extrude_perforation_pattern(const ExPolygons &polys,
                                                 const Vec3d &origin,
                                                 const Vec3d &ux, const Vec3d &uy, const Vec3d &n,
                                                 double z0, double z1);

} // namespace Slic3r

#endif // slic3r_PerforationPattern_hpp_
