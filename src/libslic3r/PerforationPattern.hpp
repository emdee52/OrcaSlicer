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
    Lines = 0,  // a single family of parallel slots at `angle`
    Grid,       // two orthogonal families at `angle` and `angle + 90` (Cross at 0, X at 45)
    Honeycomb,  // staggered hexagonal holes (isotropic)
    Circles,    // round holes on a staggered grid (isotropic)
};

struct PerforationParams {
    PerforationKind kind{PerforationKind::Honeycomb};
    double angle_deg{0.0};  // in-plane rotation, 0..45 deg
    double spacing{6.0};    // hole / slot centre-to-centre distance, mm
    double width{2.0};      // hole / slot width, mm
    double margin{2.0};     // boundary inset from the face outline, mm
    bool   invert{false};   // true: subtract the material between the holes
};

// Smallest material wall kept between adjacent holes, in mm. The pattern builder shrinks
// `width` so that holes can never merge into a continuous slot (which would produce one big
// void instead of a perforation). A fixed physically-motivated minimum, not a parameter.
inline constexpr double PERFORATION_MIN_WALL = 1.0;

// Upper bound of the in-plane angle. A single line family plus its 90-degree cross covers
// every orientation within 0..45; beyond that only repeats the same set mirrored.
inline constexpr double PERFORATION_MAX_ANGLE_DEG = 45.0;

// `spacing` for which `width` leaves a wall of at least PERFORATION_MIN_WALL. Clamps `width`
// if it is too large; returns the effective width. Shared by the builder and the UI so the
// slider ranges and the geometry cannot disagree.
double perforation_effective_width(double spacing, double width);

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
