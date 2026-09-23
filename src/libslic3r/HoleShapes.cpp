#include "HoleShapes.hpp"

#include "AABBMesh.hpp"

#include <Eigen/Eigenvalues>
#include <Eigen/Geometry>

#include <algorithm>
#include <cmath>
#include <functional>
#include <queue>
#include <utility>
#include <vector>

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
    const Vec3d right = up.cross(a).normalized(); // right x up == a, a proper rotation

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

// ---------------------------------------------------------------------------------------------
// Bore / pocket primitives
// ---------------------------------------------------------------------------------------------

namespace {

// Place a local +Z extruded mesh so its local origin maps to `entry` and local +Z to `axis`.
void place_on_axis(indexed_triangle_set &its, const Vec3d &axis, const Vec3d &entry)
{
    const Vec3d a = axis.normalized();
    Vec3d       ref = (std::abs(a.dot(Vec3d::UnitZ())) < 0.9) ? Vec3d::UnitZ() : Vec3d::UnitX();
    const Vec3d u = (ref - a * a.dot(ref)).normalized();
    const Vec3d r = u.cross(a).normalized(); // r x u == a: proper rotation

    Eigen::Matrix3d R;
    R.col(0) = Eigen::Vector3d(r(0), r(1), r(2));
    R.col(1) = Eigen::Vector3d(u(0), u(1), u(2));
    R.col(2) = Eigen::Vector3d(a(0), a(1), a(2));

    Transform3d tr = Transform3d::Identity();
    tr.linear()    = R;
    tr.translation() = entry;
    for (stl_vertex &v : its.vertices)
        v = (tr * Eigen::Vector3d(v(0), v(1), v(2))).cast<float>();
}

// Closed solid frustum, bottom radius r0 at z=0, top radius r1 at z=h, outward normals.
indexed_triangle_set make_frustum_solid(double r0, double r1, double h, int n)
{
    indexed_triangle_set its;
    if (r0 <= 0. || r1 <= 0. || h <= 0. || n < 8)
        return its;
    its.vertices.reserve(2 + 2 * n);
    its.indices.reserve(4 * n);
    its.vertices.emplace_back(Vec3f(0.f, 0.f, 0.f));
    its.vertices.emplace_back(Vec3f(0.f, 0.f, float(h)));
    for (int i = 0; i < n; ++i) {
        const double a = 2. * PI * i / n;
        const float  c = float(std::cos(a)), s = float(std::sin(a));
        its.vertices.emplace_back(float(r0) * c, float(r0) * s, 0.f);
        its.vertices.emplace_back(float(r1) * c, float(r1) * s, float(h));
    }
    for (int i = 0; i < n; ++i) {
        const int j  = (i + 1) % n;
        const int b0 = 2 + 2 * i, b1 = 2 + 2 * j;
        const int t0 = 3 + 2 * i, t1 = 3 + 2 * j;
        its.indices.emplace_back(0, b1, b0); // bottom cap, faces -Z
        its.indices.emplace_back(1, t0, t1); // top cap, faces +Z
        its.indices.emplace_back(b0, b1, t1); // side
        its.indices.emplace_back(b0, t1, t0);
    }
    return its;
}

// Closed annulus solid (tube), outward normals, bore toward the axis.
indexed_triangle_set make_tube_solid(double r_out, double r_in, double h, int n)
{
    indexed_triangle_set its;
    if (r_in <= 0. || r_out <= r_in || h <= 0. || n < 8)
        return its;
    its.vertices.reserve(4 * n);
    its.indices.reserve(8 * n);
    for (int i = 0; i < n; ++i) {
        const double a = 2. * PI * i / n;
        const float  c = float(std::cos(a)), s = float(std::sin(a));
        const float  ro = float(r_out), ri = float(r_in);
        its.vertices.emplace_back(ro * c, ro * s, 0.f);   // 4i+0 outer bottom
        its.vertices.emplace_back(ro * c, ro * s, float(h));
        its.vertices.emplace_back(ri * c, ri * s, 0.f);
        its.vertices.emplace_back(ri * c, ri * s, float(h));
    }
    for (int i = 0; i < n; ++i) {
        const int j = (i + 1) % n;
        const int ob_i = 4 * i + 0, ot_i = 4 * i + 1, ib_i = 4 * i + 2, it_i = 4 * i + 3;
        const int ob_j = 4 * j + 0, ot_j = 4 * j + 1, ib_j = 4 * j + 2, it_j = 4 * j + 3;
        its.indices.emplace_back(ob_i, ob_j, ot_j); // outer wall, outward
        its.indices.emplace_back(ob_i, ot_j, ot_i);
        its.indices.emplace_back(ib_j, ib_i, it_i); // inner wall, toward the axis
        its.indices.emplace_back(ib_j, it_i, it_j);
        its.indices.emplace_back(ob_i, ib_i, ib_j); // bottom annulus, faces -Z
        its.indices.emplace_back(ob_i, ib_j, ob_j);
        its.indices.emplace_back(ot_i, ot_j, it_j); // top annulus, faces +Z
        its.indices.emplace_back(ot_i, it_j, it_i);
    }
    return its;
}

// Revolve a closed (r,z) profile around the local +Z axis. The profile must be CCW in the (r,z)
// plane and stay clear of the axis (r > 0), which makes the result a closed ring. The profile is
// not closed explicitly: the last vertex connects back to the first.
indexed_triangle_set revolve_profile(const std::vector<Vec2d> &profile, int n)
{
    indexed_triangle_set its;
    const int            m = int(profile.size());
    if (m < 3 || n < 8)
        return its;
    its.vertices.reserve(size_t(m) * n);
    its.indices.reserve(size_t(2 * m) * n);
    for (const Vec2d &p : profile) {
        for (int j = 0; j < n; ++j) {
            const double th = 2. * PI * j / n;
            const float  c = float(std::cos(th)), s = float(std::sin(th));
            its.vertices.emplace_back(float(p(0)) * c, float(p(0)) * s, float(p(1)));
        }
    }
    for (int i = 0; i < m; ++i) {
        const int k = (i + 1) % m;
        for (int j = 0; j < n; ++j) {
            const int jn = (j + 1) % n;
            const int v0 = i * n + j;   // profile i, angle j
            const int v1 = i * n + jn;  // profile i, angle j+1
            const int v2 = k * n + j;   // profile i+1, angle j
            const int v3 = k * n + jn;  // profile i+1, angle j+1
            its.indices.emplace_back(v0, v1, v3);
            its.indices.emplace_back(v0, v3, v2);
        }
    }
    return its;
}

// Chamfer rim profile: the triangle cut at the hole corner (r,0), level with the face at
// (r + size, 0) and down the wall at (r, size). CCW in (r,z).
std::vector<Vec2d> chamfer_rim_profile(double r, double size)
{
    return {Vec2d(r, 0.), Vec2d(r + size, 0.), Vec2d(r, size)};
}

// Fillet rim profile: the lune left by removing the quarter disc of radius `size` centred at
// (r + size, size) from the corner square. CCW in (r,z).
std::vector<Vec2d> fillet_rim_profile(double r, double size, int segments)
{
    const int          arc_segs = std::max(8, segments / 4);
    const double       cx = r + size, cz = size; // arc centre
    std::vector<Vec2d> profile;
    profile.reserve(arc_segs + 2);
    profile.emplace_back(r, 0.);        // hole corner on the face
    profile.emplace_back(r + size, 0.); // tangent point on the face
    for (int i = 1; i <= arc_segs; ++i) {
        const double phi = -0.5 * PI - 0.5 * PI * double(i) / double(arc_segs); // -90 -> -180 degrees
        profile.emplace_back(cx + size * std::cos(phi), cz + size * std::sin(phi));
    }
    // The last arc point is (r, size); the closing wall edge is implicit.
    return profile;
}

} // namespace

indexed_triangle_set its_make_bore(double diameter, double depth, const Vec3d &axis, const Vec3d &entry, int segments)
{
    if (diameter <= 0. || depth <= 0.)
        return {};
    const int            n   = std::max(segments, 8);
    indexed_triangle_set its = its_make_cylinder(diameter * 0.5, depth, 2. * PI / n);
    place_on_axis(its, axis, entry);
    return its;
}

indexed_triangle_set its_make_counterbore(double clearance_d, double cbore_d, double cbore_depth,
                                          double depth, const Vec3d &axis, const Vec3d &entry, int segments)
{
    if (clearance_d <= 0. || depth <= 0.)
        return {};
    const int            n     = std::max(segments, 8);
    const double         fa    = 2. * PI / n;
    indexed_triangle_set bore  = its_make_cylinder(clearance_d * 0.5, depth, fa);
    if (cbore_d > clearance_d && cbore_depth > 0.) {
        indexed_triangle_set cbore = its_make_cylinder(cbore_d * 0.5, cbore_depth, fa);
        its_merge(bore, cbore);
    }
    place_on_axis(bore, axis, entry);
    return bore;
}

indexed_triangle_set its_make_countersink(double clearance_d, double csink_d, double csink_angle,
                                          double depth, const Vec3d &axis, const Vec3d &entry, int segments)
{
    if (clearance_d <= 0. || depth <= 0.)
        return {};
    const int    n  = std::max(segments, 8);
    const double fa = 2. * PI / n;

    indexed_triangle_set its = its_make_cylinder(clearance_d * 0.5, depth, fa);
    if (csink_d > clearance_d) {
        const double half = std::max(1., std::min(csink_angle, 179.)) * 0.5 * PI / 180.;
        const double cs   = (csink_d - clearance_d) * 0.5 / std::tan(half); // cone height
        if (cs > 0.) {
            indexed_triangle_set cone = make_frustum_solid(csink_d * 0.5, clearance_d * 0.5, cs, n);
            its_merge(its, cone);
        }
    }
    place_on_axis(its, axis, entry);
    return its;
}

indexed_triangle_set its_make_rim_chamfer(double hole_radius, double size, const Vec3d &axis,
                                          const Vec3d &entry, int segments)
{
    if (hole_radius <= 0. || size <= 0.)
        return {};
    indexed_triangle_set its = revolve_profile(chamfer_rim_profile(hole_radius, size), std::max(segments, 8));
    if (its.indices.empty())
        return {};
    place_on_axis(its, axis, entry);
    return its;
}

indexed_triangle_set its_make_rim_fillet(double hole_radius, double size, const Vec3d &axis,
                                         const Vec3d &entry, int segments)
{
    if (hole_radius <= 0. || size <= 0.)
        return {};
    indexed_triangle_set its = revolve_profile(fillet_rim_profile(hole_radius, size, std::max(segments, 8)),
                                               std::max(segments, 8));
    if (its.indices.empty())
        return {};
    place_on_axis(its, axis, entry);
    return its;
}

indexed_triangle_set its_make_tube(double outer_d, double inner_d, double depth,
                                   const Vec3d &axis, const Vec3d &entry, int segments)
{
    indexed_triangle_set its = make_tube_solid(outer_d * 0.5, inner_d * 0.5, depth, std::max(segments, 8));
    if (its.indices.empty())
        return {};
    place_on_axis(its, axis, entry);
    return its;
}

indexed_triangle_set its_make_nut_pocket(double across_flats, double pocket_depth,
                                         double clearance_d, double bore_depth,
                                         const Vec3d &axis, const Vec3d &entry, int segments)
{
    if (across_flats <= 0. || pocket_depth <= 0.)
        return {};
    // A regular hexagon with the given across-flats has circumradius across_flats / sqrt(3).
    // its_make_cylinder with 6 segments is exactly that hex prism.
    indexed_triangle_set hex = its_make_cylinder(across_flats / std::sqrt(3.0), pocket_depth, 2. * PI / 6.);
    if (clearance_d > 0. && bore_depth > 0.) {
        indexed_triangle_set bore = its_make_cylinder(clearance_d * 0.5, bore_depth, 2. * PI / std::max(segments, 8));
        its_merge(hex, bore);
    }
    place_on_axis(hex, axis, entry);
    return hex;
}

double hole_through_depth(const indexed_triangle_set &its, const Vec3d &entry, const Vec3d &dir, double margin)
{
    if (its.indices.empty() || !dir.allFinite() || dir.squaredNorm() < 1e-18)
        return 0.;
    const Vec3d                             d = dir.normalized();
    const AABBMesh                          mesh(its);
    const std::vector<AABBMesh::hit_result> hits = mesh.query_ray_hits(entry, d);
    double                                  far  = 0.;
    for (const AABBMesh::hit_result &h : hits)
        if (h.is_hit() && h.distance() > far)
            far = h.distance();
    return far > 0. ? far + std::max(0., margin) : 0.;
}

namespace {

// Filling a cavity flush needs the surface the cavity is cut into, not a plane the user paints: a
// painted plane is flat and the wall it is painted on is not. So every vertex is raised to the
// highest neighbouring surface plane within the brush reach. On a flat or convex wall (the wall of a
// part is convex where it curves away) no neighbour plane is above the vertex, so nothing is added;
// a cavity is cut behind the wall, so its floor is raised onto the wall around it and comes out
// exactly flush - a plane through the wall, not a fit, so there is no residual to leave behind.
constexpr double CAVITY_EPS        = 0.15; // raised by less than this: not a cavity, mm
constexpr double CAVITY_PARALLEL   = 0.9;  // ignore neighbour planes off parallel by more than cos(26)
constexpr size_t CAVITY_MAX_WORK   = 20000000; // vertex x facet pairs, so a huge brush cannot stall
constexpr int    CAVITY_MAX_REGION = 200000;

// Facet adjacency distances. The scratch buffers live across calls so they are allocated once.
class FacetDistances
{
public:
    FacetDistances(const std::vector<Vec3i32> &neighbors, const std::vector<Vec3d> &centroids)
        : m_neighbors(neighbors), m_centroids(centroids), m_dist(neighbors.size(), 0.),
          m_stamp(neighbors.size(), -1)
    {}

    // Every facet within `limit` of `src`, as (facet, distance), walking facet centroids.
    void run(const std::vector<int> &src, double limit, std::vector<std::pair<int, double>> &out)
    {
        ++m_run;
        out.clear();
        for (const int s : src) {
            if (s < 0 || s >= int(m_neighbors.size()))
                continue;
            m_stamp[s] = m_run;
            m_dist[s]  = 0.;
            m_queue.emplace(0., s);
        }
        while (!m_queue.empty()) {
            const std::pair<double, int> top = m_queue.top();
            m_queue.pop();
            const int f = top.second;
            if (m_stamp[f] != m_run || top.first > m_dist[f])
                continue;
            out.emplace_back(f, top.first);
            for (int k = 0; k < 3; ++k) {
                const int g = m_neighbors[f](k);
                if (g < 0)
                    continue;
                const double nd = top.first + (m_centroids[g] - m_centroids[f]).norm();
                if (nd > limit || (m_stamp[g] == m_run && nd >= m_dist[g]))
                    continue;
                m_stamp[g] = m_run;
                m_dist[g]  = nd;
                m_queue.emplace(nd, g);
            }
        }
    }

private:
    const std::vector<Vec3i32> &m_neighbors;
    const std::vector<Vec3d>   &m_centroids;
    std::vector<double>         m_dist;
    std::vector<int>            m_stamp;
    int                         m_run = 0;
    std::priority_queue<std::pair<double, int>, std::vector<std::pair<double, int>>,
                        std::greater<std::pair<double, int>>>
        m_queue;
};

} // namespace

indexed_triangle_set cavity_fill_local(const indexed_triangle_set &its,
                                       const std::vector<Vec3d> &seed_points,
                                       const std::vector<int> &seed_facets, double radius)
{
    if (its.indices.empty() || radius <= 0. || seed_points.empty() ||
        seed_points.size() != seed_facets.size())
        return {};

    const size_t               nf        = its.indices.size();
    const size_t               nv        = its.vertices.size();
    const std::vector<Vec3i32> neighbors = its_face_neighbors(its);

    std::vector<Vec3d> centroid(nf);
    for (size_t f = 0; f < nf; ++f) {
        const Vec3i32 &t = its.indices[f];
        centroid[f] = (its.vertices[t(0)].cast<double>() + its.vertices[t(1)].cast<double>() +
                       its.vertices[t(2)].cast<double>()) /
                      3.;
    }

    std::vector<int> seeds;
    for (size_t i = 0; i < seed_facets.size(); ++i) {
        const int s = seed_facets[i];
        if (s < 0 || s >= int(nf) || !seed_points[i].allFinite())
            continue;
        if (std::find(seeds.begin(), seeds.end(), s) == seeds.end())
            seeds.push_back(s);
    }
    if (seeds.empty())
        return {};

    // The patch: everything reachable from a seed within the brush reach.
    FacetDistances                      dij(neighbors, centroid);
    std::vector<std::pair<int, double>> reached;
    dij.run(seeds, radius, reached);
    if (reached.size() < 4 || reached.size() > CAVITY_MAX_REGION)
        return {};
    std::vector<int> patch;
    patch.reserve(reached.size());
    for (const auto &r : reached)
        patch.push_back(r.first);

    // Outward normal of every facet in the patch, and of the patch as a whole.
    const double       wind = its_volume(its) < 0. ? -1. : 1.; // the file may wind inward
    std::vector<Vec3d> fnorm(nf, Vec3d::Zero());
    Vec3d              n0 = Vec3d::Zero();
    for (const int f : patch) {
        const Vec3i32 &t = its.indices[f];
        const Vec3d    a = its.vertices[t(0)].cast<double>();
        const Vec3d    b = its.vertices[t(1)].cast<double>();
        const Vec3d    c = its.vertices[t(2)].cast<double>();
        const Vec3d    fn = wind * (b - a).cross(c - a);
        if (fn.norm() < 1e-12) // degenerate facet: it has no plane to offer
            continue;
        fnorm[f] = fn.normalized();
        n0 += fn;
    }
    if (n0.norm() < 1e-12)
        return {};
    n0.normalize();

    std::vector<char> vused(nv, 0);
    size_t            vcount = 0;
    for (const int f : patch) {
        const Vec3i32 &t = its.indices[f];
        for (int k = 0; k < 3; ++k)
            if (!vused[t(k)]) {
                vused[t(k)] = 1;
                ++vcount;
            }
    }
    if (vcount == 0 || size_t(patch.size()) * vcount > CAVITY_MAX_WORK)
        return {};

    // How high each vertex has to come up to reach the highest surface plane around it, always along
    // the patch normal so a groove is closed as a solid and its side walls are sealed too. A plane
    // only counts if it faces about the same way as the patch (so a groove side wall cannot lift the
    // floor) and stands outside the vertex by no more than a fraction of the brush, or the closing
    // would fill the wall's own concave curvature as well as the marks. A plane through the vertex
    // stands outside it by nothing, so a flat or convex wall leaves every vertex where it is.
    const double        reach = 0.25 * radius;
    const double        h_max = 0.5 * radius;
    std::vector<double> lift(nv, 0.);
    for (size_t v = 0; v < nv; ++v) {
        if (!vused[v])
            continue;
        const Vec3d p = its.vertices[v].cast<double>();
        double      best = 0.;
        for (const int f : patch) {
            if (fnorm[f].norm() < 0.5)
                continue;
            const double cos0 = n0.dot(fnorm[f]);
            if (cos0 < CAVITY_PARALLEL)
                continue;
            const double outside = -(p - centroid[f]).dot(fnorm[f]) / cos0;
            if (outside <= 0. || outside > reach || outside > h_max)
                continue;
            best = std::max(best, outside);
        }
        lift[v] = best;
    }

    // One prism per raised facet: the facet, its vertices lifted onto the surface around it, and the
    // sides. The prisms share no vertices - they union away per layer when the volume is sliced - so
    // no edge bookkeeping can go wrong on a coarse or degenerate mesh.
    std::vector<Vec3f>   verts;
    std::vector<Vec3i32> tris;
    for (const int f : patch) {
        const Vec3i32 &nt = its.indices[f];
        const double   hc = std::max(std::max(lift[nt(0)], lift[nt(1)]), lift[nt(2)]);
        if (hc <= CAVITY_EPS || hc > h_max)
            continue;

        Vec3i32 t = nt;
        Vec3d   q[3];
        for (int k = 0; k < 3; ++k)
            q[k] = its.vertices[t(k)].cast<double>() + lift[t(k)] * n0;
        // Orient the prism by its cap: the facet's own normal says nothing useful about the winding
        // of the cap once the vertices have each moved along their own normal.
        if ((q[1] - q[0]).cross(q[2] - q[0]).dot(n0) < 0.) {
            std::swap(t(1), t(2));
            std::swap(q[1], q[2]);
        }

        const int base = int(verts.size());
        for (int k = 0; k < 3; ++k)
            verts.push_back(q[k].cast<float>()); // the cap, at the surrounding surface
        for (int k = 0; k < 3; ++k)
            verts.push_back(its.vertices[t(k)]); // the cavity surface

        tris.push_back(Vec3i32(base + 0, base + 1, base + 2)); // cap, facing out
        tris.push_back(Vec3i32(base + 3, base + 5, base + 4)); // cavity surface, reversed
        for (int e = 0; e < 3; ++e) {                          // the three sides
            const int a = base + 3 + e, b = base + 3 + (e + 1) % 3;
            const int qa = base + e, qb = base + (e + 1) % 3;
            tris.push_back(Vec3i32(a, b, qb));
            tris.push_back(Vec3i32(a, qb, qa));
        }
    }

    if (tris.empty())
        return {};
    indexed_triangle_set plug{std::move(tris), std::move(verts)};
    if (its_volume(plug) < 0.)
        its_flip_triangles(plug);
    return plug;
}

} // namespace Slic3r
