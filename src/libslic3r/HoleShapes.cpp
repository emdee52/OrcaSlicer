#include "HoleShapes.hpp"

#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

double polygon_signed_area(const std::vector<Vec2d> &pts)
{
    double a = 0.;
    for (size_t i = 0; i < pts.size(); ++i) {
        const Vec2d &p = pts[i];
        const Vec2d &q = pts[(i + 1) % pts.size()];
        a += p(0) * q(1) - q(0) * p(1);
    }
    return 0.5 * a;
}

} // namespace

indexed_triangle_set its_make_teardrop(double radius, double depth, double angle_deg, int segments)
{
    indexed_triangle_set mesh;
    if (radius <= 0. || depth <= 0. || segments < 8)
        return mesh;

    const double alpha = std::clamp(angle_deg, 1., 89.) * PI / 180.;

    // Two lines tangent to the circle meet on the axis at radius / sin(alpha) above the
    // centre, each at `alpha` from vertical. The profile is the circle arc below the two
    // tangent points plus the apex; the arc is generated clockwise and reversed to CCW.
    const double phi0 = 0.5 * PI - alpha;      // right tangent point
    const double phi1 = 1.5 * PI + alpha;      // left tangent point, via the bottom
    std::vector<Vec2d> profile;
    profile.reserve(segments + 2);
    for (int i = 0; i <= segments; ++i) {
        const double phi = phi0 + (phi1 - phi0) * double(i) / double(segments);
        profile.emplace_back(radius * std::sin(phi), radius * std::cos(phi));
    }
    profile.emplace_back(0., radius / std::sin(alpha)); // apex

    if (polygon_signed_area(profile) < 0.)
        std::reverse(profile.begin(), profile.end());

    const int m  = int(profile.size());
    const int vb = 0; // bottom centre
    const int vt = 1; // top centre
    mesh.vertices.reserve(2 + 2 * m);
    mesh.indices.reserve(4 * m);
    mesh.vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    mesh.vertices.emplace_back(Vec3f(0.f, 0.f, float(depth)));
    for (const Vec2d &p : profile) {
        mesh.vertices.emplace_back(Vec3f(float(p(0)), float(p(1)), 0.f));
        mesh.vertices.emplace_back(Vec3f(float(p(0)), float(p(1)), float(depth)));
    }

    for (int i = 0; i < m; ++i) {
        const int j  = (i + 1) % m;
        const int b0 = 2 + 2 * i, b1 = 2 + 2 * j;
        const int t0 = 3 + 2 * i, t1 = 3 + 2 * j;
        mesh.indices.emplace_back(vb, b1, b0); // bottom cap, faces -Z
        mesh.indices.emplace_back(vt, t0, t1); // top cap, faces +Z
        mesh.indices.emplace_back(b0, b1, t1); // side
        mesh.indices.emplace_back(b0, t1, t0);
    }
    return mesh;
}

indexed_triangle_set its_make_teardrop_for_hole(const DetectedHole &hole, double depth,
                                                double angle_deg, int segments, const Vec3d &up_dir)
{
    indexed_triangle_set local = its_make_teardrop(hole.radius, depth, angle_deg, segments);
    if (local.indices.empty())
        return local;

    const Vec3d a  = hole.axis.normalized();
    Vec3d       up = up_dir - a * a.dot(up_dir);
    if (up.norm() < 1e-6)
        return {}; // hole parallel to up: a teardrop has no meaning
    up.normalize();
    const Vec3d right = a.cross(up).normalized();

    Eigen::Matrix3d R;
    R.col(0) = Eigen::Vector3d(right(0), right(1), right(2));
    R.col(1) = Eigen::Vector3d(up(0), up(1), up(2));
    R.col(2) = Eigen::Vector3d(a(0), a(1), a(2));

    Transform3d tr       = Transform3d::Identity();
    tr.linear()          = R;
    tr.translation()     = hole.center - R * Eigen::Vector3d(0., 0., 0.5 * depth);

    for (stl_vertex &v : local.vertices)
        v = (tr * Eigen::Vector3d(v(0), v(1), v(2))).cast<float>();
    return local;
}

} // namespace Slic3r
