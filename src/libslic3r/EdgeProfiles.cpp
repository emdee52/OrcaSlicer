#include "EdgeProfiles.hpp"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

// Smallest crease, as the sine of the dihedral angle between the patch and the face across a
// boundary run, that still counts as a real rim. Below this the two faces are near coplanar, which
// means the run is an interior edge of a smooth surface rather than the outline of a face.
constexpr double MIN_CREASE_SIN = 0.2588; // 15 degrees

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

namespace {

bool face_has_vertex(const indexed_triangle_set &its, int face, int vertex)
{
    const Vec3i32 &idx = its.indices[face];
    return idx(0) == vertex || idx(1) == vertex || idx(2) == vertex;
}

} // namespace

std::vector<std::vector<LoopFrame>> its_face_patch_loops(const indexed_triangle_set &its,
                                                         const Transform3d &trafo, int seed_face,
                                                         float normal_tol)
{
    std::vector<std::vector<LoopFrame>> loops;
    const int face_count = int(its.indices.size());
    if (seed_face < 0 || seed_face >= face_count || its.vertices.empty())
        return loops;

    const std::vector<Vec3f>  normals = its_face_normals(its);
    const std::vector<Vec3i32> neighbors = its_face_neighbors(its);

    // Grow the coplanar patch that holds the seed face.
    std::vector<int>  patch;
    std::vector<char> in_patch(size_t(face_count), 0);
    patch.push_back(seed_face);
    in_patch[size_t(seed_face)] = 1;
    for (size_t head = 0; head < patch.size(); ++head) {
        const int f = patch[head];
        for (int k = 0; k < 3; ++k) {
            const int g = neighbors[size_t(f)](k);
            if (g < 0 || in_patch[size_t(g)])
                continue;
            if ((normals[size_t(g)] - normals[size_t(f)]).cwiseAbs().maxCoeff() <= normal_tol) {
                in_patch[size_t(g)] = 1;
                patch.push_back(g);
            }
        }
    }

    // An edge of the patch is on its boundary when the face on the other side is outside it.
    struct BoundaryEdge
    {
        int a, b, patch_face, outside_face;
    };
    std::vector<BoundaryEdge> boundary;
    for (const int f : patch) {
        for (int k = 0; k < 3; ++k) {
            const int a = its.indices[size_t(f)](k);
            const int b = its.indices[size_t(f)]((k + 1) % 3);
            if (a == b)
                continue;
            int g = -1;
            for (int j = 0; j < 3; ++j) {
                const int n = neighbors[size_t(f)](j);
                if (n >= 0 && !in_patch[size_t(n)] && face_has_vertex(its, n, a) &&
                    face_has_vertex(its, n, b)) {
                    g = n;
                    break;
                }
            }
            if (g >= 0)
                boundary.push_back(BoundaryEdge{ a, b, f, g });
        }
    }

    std::vector<std::vector<int>> vertex_edges(its.vertices.size());
    for (size_t e = 0; e < boundary.size(); ++e) {
        vertex_edges[size_t(boundary[e].a)].push_back(int(e));
        vertex_edges[size_t(boundary[e].b)].push_back(int(e));
    }

    const Eigen::Matrix3d normal_trafo = trafo.linear().inverse().transpose();
    const auto to_world = [&trafo](const Vec3f &p) { return trafo * p.cast<double>(); };
    const auto to_world_normal = [&normal_trafo](const Vec3f &n) {
        return (normal_trafo * n.cast<double>()).normalized();
    };

    std::vector<char> used(boundary.size(), 0);
    for (size_t s = 0; s < boundary.size(); ++s) {
        if (used[s])
            continue;
        const int start_v = boundary[s].a;
        if (vertex_edges[size_t(start_v)].size() != 2) { // corner of several loops: skip
            used[s] = 1;
            continue;
        }

        std::vector<int> chain;
        int              entry = start_v;
        int              cur   = int(s);
        bool             closed = false;
        for (int guard = int(boundary.size()) + 1; guard > 0; --guard) {
            used[size_t(cur)] = 1;
            chain.push_back(cur);
            const int exit_v =
                boundary[size_t(cur)].a == entry ? boundary[size_t(cur)].b : boundary[size_t(cur)].a;
            if (exit_v == start_v) {
                closed = true;
                break;
            }
            if (vertex_edges[size_t(exit_v)].size() != 2)
                break;
            int next = -1;
            for (const int e : vertex_edges[size_t(exit_v)])
                if (!used[size_t(e)]) {
                    next = e;
                    break;
                }
            if (next < 0)
                break;
            entry = exit_v;
            cur   = next;
        }
        if (!closed || chain.size() < 3)
            continue;

        // Ordered ring of vertices; step k is the edge verts[k] -> verts[k+1].
        const size_t n = chain.size();
        std::vector<int> verts(n);
        std::vector<int> step_patch(n), step_outside(n);
        int              v = start_v;
        for (size_t k = 0; k < n; ++k) {
            verts[k]        = v;
            step_patch[k]   = boundary[size_t(chain[k])].patch_face;
            step_outside[k] = boundary[size_t(chain[k])].outside_face;
            v = boundary[size_t(chain[k])].a == v ? boundary[size_t(chain[k])].b
                                                  : boundary[size_t(chain[k])].a;
        }

        std::vector<LoopFrame> frames;
        frames.reserve(n);
        bool ok = true;
        for (size_t i = 0; i < n; ++i) {
            const Vec3d p = to_world(its.vertices[size_t(verts[i])]);
            const Vec3d q = to_world(its.vertices[size_t(verts[(i + 1) % n])]);
            const Vec3d e = q - p;
            if (e.norm() < 1e-9) {
                ok = false;
                break;
            }
            // The frame of this segment, from its own two faces: the same terms a straight edge
            // gets, so the cross section stays perpendicular to the boundary all the way round.
            const Vec3d dir = e.normalized();
            const Vec3d n_a = to_world_normal(normals[size_t(step_patch[i])]);
            const Vec3d n_b = to_world_normal(normals[size_t(step_outside[i])]);
            // A run whose neighbouring face is nearly coplanar with the patch is not a rim at all:
            // it is an interior edge of a tessellated curved surface, such as one segment of a
            // cylinder wall. Sweeping such a boundary gives a nonsense solid, so refuse the loop.
            if (std::fabs(dir.cross(n_a).normalized().dot(n_b)) < MIN_CREASE_SIN) {
                ok = false;
                break;
            }
            Vec3d u, vv;
            if (!face_inward_dir(dir, n_a, n_b, u) || !face_inward_dir(dir, n_b, n_a, vv)) {
                ok = false;
                break;
            }
            frames.push_back(LoopFrame{ p, q, u, vv });
        }

        // A boundary is normally tessellated, and only the corners need a miter: merge frames that
        // continue in the same direction with the same faces into one straight run, so a flat side
        // becomes a single frame and the seams between its segments do not add degenerate geometry.
        const auto same_run = [](const LoopFrame &a, const LoopFrame &b) {
            const Vec3d da = (a.q - a.p).normalized();
            const Vec3d db = (b.q - b.p).normalized();
            return da.cross(db).norm() < 1e-6 && (a.u - b.u).norm() < 1e-6 && (a.v - b.v).norm() < 1e-6;
        };
        if (ok) {
            std::vector<LoopFrame> merged;
            merged.reserve(frames.size());
            for (const LoopFrame &f : frames)
                if (!merged.empty() && same_run(merged.back(), f))
                    merged.back().q = f.q;
                else
                    merged.push_back(f);
            if (merged.size() > 1 && same_run(merged.back(), merged.front())) {
                merged.front().p = merged.back().p;
                merged.pop_back();
            }
            frames = std::move(merged);
        }
        if (ok && frames.size() >= 3)
            loops.push_back(std::move(frames));
    }
    return loops;
}

std::vector<std::vector<LoopFrame>> its_face_patch_loops_around(const indexed_triangle_set &its,
                                                                const Transform3d &trafo,
                                                                int seed_face, int max_rings,
                                                                float normal_tol)
{
    std::vector<std::vector<LoopFrame>> loops = its_face_patch_loops(its, trafo, seed_face, normal_tol);
    if (!loops.empty() || max_rings <= 0 || seed_face < 0 || seed_face >= int(its.indices.size()))
        return loops;

    // A rim can border a smooth band, and a cursor on that band lands on a patch with no rim of its
    // own. Ring outwards from the seed face and return the loops of the first ring that has any:
    // the patch beyond the band is the flat face the rim belongs to. Ringing stops at the first
    // ring that yields a loop, so the cost stays local to the cursor.
    const std::vector<Vec3i32> neighbors = its_face_neighbors(its);
    std::vector<char>          seen(its.indices.size(), 0);
    seen[seed_face] = 1;
    std::vector<int> frontier{ seed_face };
    for (int ring = 0; ring < max_rings && !frontier.empty(); ++ring) {
        std::vector<int> next;
        for (const int f : frontier) {
            if (f < 0 || f >= int(neighbors.size()))
                continue;
            for (int k = 0; k < 3; ++k) {
                const int n = neighbors[f][k];
                if (n < 0 || seen[n])
                    continue;
                seen[n] = 1;
                next.push_back(n);
            }
        }
        for (const int n : next) {
            std::vector<std::vector<LoopFrame>> found = its_face_patch_loops(its, trafo, n, normal_tol);
            for (std::vector<LoopFrame> &loop : found)
                loops.push_back(std::move(loop));
        }
        if (!loops.empty())
            return loops;
        frontier.swap(next);
    }
    return loops;
}

indexed_triangle_set sweep_loop(const std::vector<LoopFrame> &loop, const std::vector<Vec2d> &profile)
{
    indexed_triangle_set its;
    const int n = int(loop.size()), m = int(profile.size());
    if (n < 3 || m < 3)
        return its;

    // Per segment, two rings of the profile: one at the segment start and one at its end, both in
    // the frame of that segment, so the segment is a straight prism. Consecutive segments are then
    // joined by a miter ring at their shared vertex. Rings are not capped, so the ring of a shared
    // vertex closes the prism of one segment against the miter of the next and the solid is closed.
    its.vertices.reserve(size_t(n) * size_t(m) * 2);
    its.indices.reserve(size_t(n) * size_t(m) * 4);
    for (const LoopFrame &f : loop) {
        for (const Vec2d &q : profile)
            its.vertices.emplace_back((f.p + q(0) * f.u + q(1) * f.v).cast<float>());
        for (const Vec2d &q : profile)
            its.vertices.emplace_back((f.q + q(0) * f.u + q(1) * f.v).cast<float>());
    }

    const auto quad = [&its](int a, int b, int c, int d) {
        its.indices.emplace_back(a, b, c);
        its.indices.emplace_back(a, c, d);
    };
    for (int i = 0; i < n; ++i) {
        const int start = i * 2 * m;
        const int end   = start + m;
        for (int k = 0; k < m; ++k) {
            const int kn = (k + 1) % m;
            quad(start + k, start + kn, end + kn, end + k);
        }
        const int next_start = ((i + 1) % n) * 2 * m;
        for (int k = 0; k < m; ++k) {
            const int kn = (k + 1) % m;
            quad(end + k, end + kn, next_start + kn, next_start + k);
        }
    }

    if (its_volume(its) < 0.f)
        its_flip_triangles(its);
    return its;
}

indexed_triangle_set make_loop_chamfer(const std::vector<LoopFrame> &loop, double size)
{
    return sweep_loop(loop, chamfer_profile(size));
}

indexed_triangle_set make_loop_fillet(const std::vector<LoopFrame> &loop, double size,
                                      int segments)
{
    return sweep_loop(loop, fillet_profile(size, segments));
}

} // namespace Slic3r
