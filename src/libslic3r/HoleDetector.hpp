#ifndef libslic3r_HoleDetector_hpp_
#define libslic3r_HoleDetector_hpp_

#include "TriangleMesh.hpp"

#include <vector>

namespace Slic3r {

// A cylindrical wall found in a triangle mesh: a hole, a bore or a boss. Produced by
// `detect_holes()`. This is a geometric observation only - it does not say whether the
// cylinder bounds a void (a hole) or a solid (a boss); the caller decides from `through`
// and the surrounding geometry.
struct DetectedHole
{
    Vec3d  axis       = Vec3d::UnitZ(); // unit vector along the cylinder; sign is canonicalized +Z-ish
    Vec3d  center     = Vec3d::Zero();  // a point on the axis, halfway along the wall
    double radius     = 0.;             // fitted cylinder radius, mm
    double depth      = 0.;             // wall extent along the axis, mm
    bool   through    = false;          // no cap closes either end (an open through-hole)
    double confidence = 0.;             // 0..1, wall fit quality times angular coverage
    std::vector<int> facets;            // triangle indices of the cylindrical wall
};

struct HoleDetectorParams
{
    // Wall patches smaller than this are ignored.
    int    min_facets           = 12;
    // Faces are grouped into one patch when their normals differ by less than this.
    double smooth_angle_deg     = 25.;
    // Candidate cylinders outside these radii are ignored.
    double min_radius           = 0.3;
    double max_radius           = 100.;
    // A patch is accepted when every wall vertex is within this of the fitted cylinder,
    // relative to the radius.
    double radial_tolerance     = 0.08;
    // The wall must wrap around the axis: the largest angular gap between consecutive wall
    // vertices must be below this.
    double max_angular_gap_deg  = 120.;
    // A face closing an end counts as a cap when the cap faces cover this fraction of the
    // hole's cross-section.
    double cap_area_fraction    = 0.5;
    // A cap face must lie within this of the wall end, relative to the radius.
    double cap_end_tolerance    = 0.25;
};

// Detect cylindrical walls in a triangle mesh. Results are ordered largest radius first.
// Pure geometry: no model, no config, no I/O. The mesh must have merged vertices so that
// faces sharing an edge share its vertex indices (true of Orca's imported and boolean
// meshes); `its_merge_vertices()` can repair a mesh that does not.
std::vector<DetectedHole> detect_holes(const indexed_triangle_set &its,
                                       const HoleDetectorParams &params = {});

} // namespace Slic3r

#endif // libslic3r_HoleDetector_hpp_
