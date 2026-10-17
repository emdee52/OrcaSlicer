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
    double angle_deg{0.0};  // in-plane rotation; the usable range depends on `kind`, see below
    double spacing{6.0};    // hole / slot centre-to-centre distance, mm
    double width{2.0};      // hole / slot width, mm
    double margin{2.0};     // boundary inset from the face outline, mm
    bool   invert{false};   // true: subtract the material between the holes
};

// Wall below which adjacent holes are considered too close, in mm. Advisory only: the UI warns
// when `spacing - width` drops below this, because a sub-millimetre wall leaves no room for
// perimeters and the holes may merge. The builder does NOT enforce it - the value the user sets
// is the geometry, merged or not.
inline constexpr double PERFORATION_MIN_WALL = 1.0;

// Upper bound of the in-plane angle for a pattern made of TWO families (Grid). A family crossed with
// its own 90-degree cross is the same pair at `angle` and at `90 - angle` (mirrored), so 0..45 already
// covers every distinct pair; beyond that only repeats the same set mirrored.
inline constexpr double PERFORATION_MAX_ANGLE_DEG = 45.0;

// [ORCAPORT:PP-6] Upper bound for a pattern made of ONE family (Lines). The pair argument above does
// not apply to it: a single family at 100 deg is not the family at 80 deg, it is that family mirrored,
// and on an asymmetric face that is different geometry. 0..90 is what turns the family from horizontal
// to vertical; 90..180 would be the mirrored half, so the range stops at the vertical.
inline constexpr double PERFORATION_MAX_ANGLE_LINES_DEG = 90.0;

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
