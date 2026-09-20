#include "HoleDetector.hpp"

#include <Eigen/Dense>

#include <algorithm>
#include <cmath>
#include <limits>

namespace Slic3r {

namespace {

// Orthonormal frame (e1, e2, axis) with e1 x e2 == axis, so that a point p decomposes as
// p = (p.e1) e1 + (p.e2) e2 + (p.axis) axis.
struct AxisFrame
{
    Vec3d e1, e2, axis;
};

AxisFrame make_frame(const Vec3d &axis_in)
{
    AxisFrame f;
    f.axis = axis_in.normalized();

    // Pick the world axis the cylinder axis is least aligned with for a stable cross product.
    int    i = 0;
    double m = std::abs(f.axis(0));
    if (std::abs(f.axis(1)) < m) { m = std::abs(f.axis(1)); i = 1; }
    if (std::abs(f.axis(2)) < m) { i = 2; }
    Vec3d ref = Vec3d::Zero();
    ref(i) = 1.;

    f.e1 = f.axis.cross(ref).normalized();
    f.e2 = f.axis.cross(f.e1).normalized();
    return f;
}

// Algebraic (Kasa) circle fit of 2D points, in input units. Returns false when the points
// do not span a disk (too few, coincident or collinear).
bool fit_circle(const std::vector<Vec2d> &pts, double &cu, double &cv, double &r, double &residual)
{
    const int n = int(pts.size());
    if (n < 5)
        return false;

    Eigen::Vector2d mean = Eigen::Vector2d::Zero();
    for (const Vec2d &p : pts)
        mean += Eigen::Vector2d(p(0), p(1));
    mean /= double(n);

    double rms = 0.;
    for (const Vec2d &p : pts)
        rms += (Eigen::Vector2d(p(0), p(1)) - mean).squaredNorm();
    rms = std::sqrt(rms / double(n));
    if (rms < 1e-9)
        return false;

    // Normalize to unit RMS radius for conditioning: solve x^2 + y^2 + a x + b y + c = 0.
    Eigen::Matrix3d A = Eigen::Matrix3d::Zero();
    Eigen::Vector3d b = Eigen::Vector3d::Zero();
    for (const Vec2d &p : pts) {
        const double x = (p(0) - mean(0)) / rms;
        const double y = (p(1) - mean(1)) / rms;
        const Eigen::Vector3d row(x, y, 1.);
        A.noalias() += row * row.transpose();
        b += row * (-(x * x + y * y));
    }

    const Eigen::FullPivLU<Eigen::Matrix3d> lu(A);
    if (lu.rank() < 3)
        return false; // collinear points: a flat patch, not a cylinder

    const Eigen::Vector3d s = lu.solve(b);
    const double          cx = -0.5 * s(0);
    const double          cy = -0.5 * s(1);
    const double          rr = cx * cx + cy * cy - s(2);
    if (rr <= 1e-12)
        return false;

    cu = mean(0) + cx * rms;
    cv = mean(1) + cy * rms;
    r  = std::sqrt(rr) * rms;

    residual = 0.;
    for (const Vec2d &p : pts)
        residual = std::max(residual, std::abs(std::hypot(p(0) - cu, p(1) - cv) - r));
    return true;
}

// Deterministic axis sign: point the axis towards +Z, or towards +X/+Y when it is horizontal.
Vec3d canonical_axis(Vec3d a)
{
    if (a(2) < -1e-9 || (std::abs(a(2)) <= 1e-9 && (a(0) < -1e-9 || (std::abs(a(0)) <= 1e-9 && a(1) < 0.))))
        a = -a;
    return a;
}

} // namespace

std::vector<DetectedHole> detect_holes(const indexed_triangle_set &its, const HoleDetectorParams &params)
{
    std::vector<DetectedHole> holes;

    const int nf = int(its.indices.size());
    if (nf < params.min_facets || its.vertices.empty())
        return holes;

    const std::vector<Vec3f>   normals = its_face_normals(its);
    // Patch growth uses vertex adjacency, not edge adjacency: a boolean/repair can leave a
    // cylinder wall split across T-junctions, and edge neighbours then break it into arcs.
    const VertexFaceIndex      vertex_faces(its);
    const double               cos_smooth = std::cos(params.smooth_angle_deg * PI / 180.);

    std::vector<char> visited(nf, 0);
    std::vector<char> in_patch(nf, 0);
    std::vector<int>  stack;
    std::vector<int>  patch;
    std::vector<int>  vids;
    // Generation stamp instead of a cleared flag array: resetting per patch would make the
    // scan quadratic in the number of patches.
    std::vector<int>  vertex_stamp(its.vertices.size(), -1);
    int               patch_stamp = 0;

    for (int seed = 0; seed < nf; ++seed) {
        if (visited[seed])
            continue;

        // Grow a patch across smooth (small-dihedral) edges. A cylinder wall is smooth; its
        // flat caps and the surrounding surface meet it at a sharp angle and stay out.
        visited[seed] = 1;
        stack.assign(1, seed);
        patch.assign(1, seed);
        while (!stack.empty()) {
            const int    f  = stack.back();
            stack.pop_back();
            const Vec3f &n0 = normals[f];
            for (int k = 0; k < 3; ++k) {
                for (size_t g : vertex_faces[its.indices[f][k]]) {
                    if (int(g) == f || visited[g])
                        continue;
                    if (double(n0.dot(normals[g])) < cos_smooth)
                        continue;
                    visited[g] = 1;
                    stack.push_back(int(g));
                    patch.push_back(int(g));
                }
            }
        }
        if (int(patch.size()) < params.min_facets)
            continue;

        // The cylinder axis is the direction in which the wall normals vary least: the
        // normals of a cylinder lie on a great circle in the plane perpendicular to the axis.
        Eigen::Vector3d mean_n = Eigen::Vector3d::Zero();
        for (int f : patch) {
            const Vec3f &n = normals[f];
            mean_n += Eigen::Vector3d(n(0), n(1), n(2));
        }
        mean_n /= double(patch.size());

        Eigen::Matrix3d cov = Eigen::Matrix3d::Zero();
        for (int f : patch) {
            const Vec3f    &n = normals[f];
            Eigen::Vector3d d(n(0) - mean_n(0), n(1) - mean_n(1), n(2) - mean_n(2));
            cov.noalias() += d * d.transpose();
        }
        const Eigen::SelfAdjointEigenSolver<Eigen::Matrix3d> eig(cov);
        // eigenvalues() is ascending. A cylinder needs curvature (largest two eigenvalues
        // non-zero and comparable) and planarity of the normal distribution (smallest much
        // smaller). This rejects flat patches (no curvature) and spheres (isotropic normals).
        const Eigen::Vector3d evals = eig.eigenvalues();
        if (evals(2) < 1e-12 || evals(1) < 0.25 * evals(2) || evals(0) > 0.25 * evals(1))
            continue;
        const Vec3d   axis(eig.eigenvectors()(0, 0), eig.eigenvectors()(1, 0), eig.eigenvectors()(2, 0));
        const AxisFrame frame = make_frame(axis);

        // Project the wall vertices onto the plane perpendicular to the axis and fit a circle.
        const int stamp = patch_stamp++;
        vids.clear();
        std::vector<Vec2d> pts;
        double             t_min = std::numeric_limits<double>::max();
        double             t_max = -std::numeric_limits<double>::max();
        for (int f : patch) {
            for (int k = 0; k < 3; ++k) {
                const int v = its.indices[f][k];
                if (vertex_stamp[v] == stamp)
                    continue;
                vertex_stamp[v] = stamp;
                vids.push_back(v);
                const Vec3f &p = its.vertices[v];
                pts.emplace_back(p(0) * frame.e1(0) + p(1) * frame.e1(1) + p(2) * frame.e1(2),
                                 p(0) * frame.e2(0) + p(1) * frame.e2(1) + p(2) * frame.e2(2));
                const double t = p(0) * frame.axis(0) + p(1) * frame.axis(1) + p(2) * frame.axis(2);
                t_min = std::min(t_min, t);
                t_max = std::max(t_max, t);
            }
        }

        double cu = 0., cv = 0., r = 0., residual = 0.;
        if (!fit_circle(pts, cu, cv, r, residual))
            continue;
        if (r < params.min_radius || r > params.max_radius)
            continue;
        if (residual > params.radial_tolerance * r)
            continue;
        const double depth = t_max - t_min;
        if (depth < 1e-6)
            continue;

        // The wall must wrap around the axis: find the largest empty angular sector.
        std::vector<double> angles;
        angles.reserve(pts.size());
        for (const Vec2d &p : pts)
            angles.push_back(std::atan2(p(1) - cv, p(0) - cu));
        std::sort(angles.begin(), angles.end());
        double max_gap = 0.;
        for (size_t i = 0; i < angles.size(); ++i) {
            const double a0 = angles[i];
            const double a1 = (i + 1 < angles.size()) ? angles[i + 1] : angles[0] + 2. * PI;
            max_gap = std::max(max_gap, a1 - a0);
        }
        if (max_gap > params.max_angular_gap_deg * PI / 180.)
            continue;

        const double t_mid = 0.5 * (t_min + t_max);
        DetectedHole h;
        h.axis   = canonical_axis(frame.axis);
        h.center = cu * frame.e1 + cv * frame.e2 + t_mid * frame.axis;
        h.radius = r;
        h.depth  = depth;
        h.facets = patch;

        // A cap closes an end: a face adjacent to the wall, perpendicular to the axis, lying
        // within the hole radius, at one end, covering the cross-section. A floor of a blind
        // hole does; the plate surface around a through-hole has vertices far from the axis
        // and does not.
        std::fill(in_patch.begin(), in_patch.end(), 0);
        for (int f : patch)
            in_patch[f] = 1;

        const double cap_tol_t   = params.cap_end_tolerance * r;
        const double cap_max_rad = 1.3 * r + cap_tol_t;
        const double hole_area   = PI * r * r;

        // Which end of the wall a face closes, or -1. A cap face is parallel to the axis, lies at
        // one end, and stays within the hole radius.
        auto cap_end_of = [&](int g) -> int {
            const Vec3f &n = normals[g];
            if (std::abs(n(0) * frame.axis(0) + n(1) * frame.axis(1) + n(2) * frame.axis(2)) < 0.9)
                return -1;
            double gt_min = std::numeric_limits<double>::max(), gt_max = -std::numeric_limits<double>::max(), max_rad = 0.;
            for (int k = 0; k < 3; ++k) {
                const Vec3f &p = its.vertices[its.indices[g][k]];
                const double t = p(0) * frame.axis(0) + p(1) * frame.axis(1) + p(2) * frame.axis(2);
                gt_min = std::min(gt_min, t);
                gt_max = std::max(gt_max, t);
                const double u = p(0) * frame.e1(0) + p(1) * frame.e1(1) + p(2) * frame.e1(2) - cu;
                const double v = p(0) * frame.e2(0) + p(1) * frame.e2(1) + p(2) * frame.e2(2) - cv;
                max_rad = std::max(max_rad, std::hypot(u, v));
            }
            if (max_rad > cap_max_rad)
                return -1;
            if (std::abs(gt_min - t_min) < cap_tol_t && std::abs(gt_max - t_min) < cap_tol_t)
                return 0;
            if (std::abs(gt_min - t_max) < cap_tol_t && std::abs(gt_max - t_max) < cap_tol_t)
                return 1;
            return -1;
        };

        // Flood the whole cap surface from the wall rim: a floor can be triangulated so only part
        // of it touches the wall, and counting just the adjacent faces would miss the cap.
        std::vector<char> cap_seen(nf, 0);
        double            cap_area[2] = {0., 0.};
        std::vector<int>  cap_stack;
        for (int f : patch) {
            for (int k = 0; k < 3; ++k) {
                for (size_t gg : vertex_faces[its.indices[f][k]]) {
                    const int g = int(gg);
                    if (in_patch[g] || cap_seen[g])
                        continue;
                    const int end = cap_end_of(g);
                    if (end < 0)
                        continue;
                    cap_seen[g] = 1;
                    cap_stack.assign(1, g);
                    while (!cap_stack.empty()) {
                        const int h = cap_stack.back();
                        cap_stack.pop_back();
                        cap_area[end] += its.facet_area(h);
                        for (int kk = 0; kk < 3; ++kk)
                            for (size_t qq : vertex_faces[its.indices[h][kk]]) {
                                const int q = int(qq);
                                if (in_patch[q] || cap_seen[q] || cap_end_of(q) != end)
                                    continue;
                                cap_seen[q] = 1;
                                cap_stack.push_back(q);
                            }
                    }
                }
            }
        }
        const bool cap_min = cap_area[0] >= params.cap_area_fraction * hole_area;
        const bool cap_max = cap_area[1] >= params.cap_area_fraction * hole_area;
        h.through = !cap_min && !cap_max;

        const double residual_score = std::clamp(1. - residual / (params.radial_tolerance * r), 0., 1.);
        const double coverage_score = std::clamp((2. * PI - max_gap) / (2. * PI), 0., 1.);
        h.confidence = residual_score * coverage_score;

        holes.push_back(std::move(h));
    }

    std::sort(holes.begin(), holes.end(),
              [](const DetectedHole &a, const DetectedHole &b) { return a.radius > b.radius; });
    return holes;
}

} // namespace Slic3r
