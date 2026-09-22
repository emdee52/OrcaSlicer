#include "EdgeProfiles.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

double profile_signed_area(const std::vector<Vec2d> &pts)
{
    double a = 0.;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2d &p = pts[i];
        const Vec2d &q = pts[(i + 1) % pts.size()];
        a += p(0) * q(1) - q(0) * p(1);
    }
    return 0.5 * a;
}

// Unit in-plane direction, perpendicular to the edge `w`, pointing into the face with normal
// `n_face`. `n_other` is the outward normal of the opposite face: the direction must lean away
// from it, which picks the sign. Returns false for a degenerate or flat crease.
bool face_inward_dir(const Vec3d &w, const Vec3d &n_face, const Vec3d &n_other, Vec3d &out)
{
    const Vec3d c = w.cross(n_face);
    const double len = c.norm();
    if (len < 1e-9)
        return false;
    const Vec3d unit = c / len;
    const double align = unit.dot(n_other);
    if (std::abs(align) < 1e-6)
        return false;
    out = (align < 0. ? unit : -unit);
    return true;
}

} // namespace

std::vector<Vec2d> chamfer_profile(double size)
{
    if (size <= 0.)
        return {};
    return { Vec2d(0., 0.), Vec2d(size, 0.), Vec2d(0., size) };
}

std::vector<Vec2d> fillet_profile(double radius, int segments)
{
    if (radius <= 0.)
        return {};
    const int arc_segs = std::max(4, segments);
    std::vector<Vec2d> pts;
    pts.reserve(size_t(2 + arc_segs));
    pts.emplace_back(0., 0.);
    pts.emplace_back(radius, 0.);
    // Quarter arc of the circle centred at (radius, radius), from -90 to -180 degrees.
    for (int i = 1; i <= arc_segs; ++i) {
        const double phi = -0.5 * PI - 0.5 * PI * double(i) / double(arc_segs);
        pts.emplace_back(radius + radius * std::cos(phi), radius + radius * std::sin(phi));
    }
    return pts;
}

indexed_triangle_set extrude_profile(const std::vector<Vec2d> &profile, const Vec3d &origin,
                                     const Vec3d &u, const Vec3d &v, const Vec3d &w, double z0,
                                     double z1)
{
    indexed_triangle_set its;
    const int m = int(profile.size());
    if (m < 3 || z1 - z0 <= 0.)
        return its;

    its.vertices.reserve(size_t(2 * m));
    its.indices.reserve(size_t(4 * m - 4));
    for (const Vec2d &p : profile) {
        const Vec3d base = origin + p(0) * u + p(1) * v;
        its.vertices.emplace_back((base + z0 * w).cast<float>()); // index 2i
        its.vertices.emplace_back((base + z1 * w).cast<float>()); // index 2i+1
    }
    for (int i = 0; i < m; ++i) {
        const int k  = (i + 1) % m;
        const int b_i = 2 * i, b_k = 2 * k, t_i = 2 * i + 1, t_k = 2 * k + 1;
        its.indices.emplace_back(b_i, b_k, t_k); // side
        its.indices.emplace_back(b_i, t_k, t_i);
    }
    for (int i = 1; i < m - 1; ++i) { // caps fan from profile vertex 0
        const int k = i + 1;
        its.indices.emplace_back(2 * 0, 2 * k, 2 * i);     // cap at z0
        its.indices.emplace_back(2 * 0 + 1, 2 * i + 1, 2 * k + 1); // cap at z1
    }
    // The frame may be left-handed; force outward normals via the signed volume.
    if (its_volume(its) < 0.f)
        its_flip_triangles(its);
    return its;
}

indexed_triangle_set make_edge_chamfer(const Vec3d &p0, const Vec3d &p1, const Vec3d &n_a,
                                       const Vec3d &n_b, double size, double margin)
{
    return make_edge_fillet(p0, p1, n_a, n_b, size, margin, 0);
}

indexed_triangle_set make_edge_fillet(const Vec3d &p0, const Vec3d &p1, const Vec3d &n_a,
                                      const Vec3d &n_b, double size, double margin, int segments)
{
    indexed_triangle_set its;
    if (size <= 0. || margin < 0.)
        return its;
    const Vec3d  e = p1 - p0;
    const double len = e.norm();
    if (len < 1e-6)
        return its;
    const Vec3d w = e / len;
    Vec3d u, v;
    if (!face_inward_dir(w, n_a.normalized(), n_b.normalized(), u))
        return its;
    if (!face_inward_dir(w, n_b.normalized(), n_a.normalized(), v))
        return its;
    const std::vector<Vec2d> profile =
        segments > 0 ? fillet_profile(size, segments) : chamfer_profile(size);
    return extrude_profile(profile, p0, u, v, w, -margin, len + margin);
}

} // namespace Slic3r
