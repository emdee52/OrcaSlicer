#include "GLGizmoEdgeDress.hpp"

#include "libslic3r/EdgeProfiles.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include "glad/gl.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <set>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char *EDGE_CHAMFER_NAME = "EdgeChamfer";
constexpr const char *EDGE_FILLET_NAME  = "EdgeFillet";
constexpr const char *EDGE_LOOP_NAME    = "EdgeLoop";

const ColorRGBA ALL_COLOR{ 0.25f, 0.70f, 1.00f, 0.40f };
const ColorRGBA HOVER_COLOR{ 0.10f, 1.00f, 0.20f, 0.90f };
// Applied features, the same red the hole gizmo uses for applied holes.
const ColorRGBA APPLIED_COLOR{ 1.00f, 0.15f, 0.15f, 0.80f };

// Chamfer leg / fillet radius, in mm, that the panel offers.
constexpr double EDGE_SIZE_MIN = 0.5;
constexpr double EDGE_SIZE_MAX = 3.0;

// How far behind the surface under the cursor an edge may sit and still count as visible, in world
// mm. Edges on the far side of the model are tens of mm behind, edges on the visible side are at
// most a wall thickness behind, so this separates the two without needing a depth buffer read.
constexpr double EDGE_DEPTH_TOL = 0.5;

// Same idea for the loop pick: a rim may be found on the patch next to the one under the cursor, up
// to a band's height away, so it is allowed to sit deeper than a straight edge does.
constexpr double EDGE_LOOP_DEPTH_TOL = 2.0;

// Rings of faces searched around the cursor for a loop when the patch under it has none.
constexpr int EDGE_LOOP_RINGS = 3;

// A boundary that bends by less than this at both ends is a tessellated curve, not a straight edge.
constexpr double EDGE_CURVE_DEG = 20.;

// Cosines of the smallest crease that counts as an edge at all. A pair of faces closer to coplanar
// than this is a tessellation diagonal of a flat face or a seam of a smooth surface: dressing it
// would be dressing noise.
constexpr double EDGE_MIN_CREASE_DOT = 0.9659; // cos(15 degrees)

// Feature volumes are named <prefix>#<id>, or <prefix>#<id>@<tag> when the volume remembers which
// edge or loop it came from; the id is matched by prefix, like the hole features.
std::string feature_name(const char *prefix, int idx)
{
    return std::string(prefix) + "#" + std::to_string(idx);
}

std::string feature_name(const char *prefix, int idx, const std::string &tag)
{
    return feature_name(prefix, idx) + (tag.empty() ? std::string() : "@" + tag);
}

// A short key for the source geometry of a feature, so an applied volume can be tied back to the
// edge or loop it dresses, also after the project has been saved and reopened. The points are in
// object space and quantized, which keeps the key stable across a reload of the same mesh.
std::string feature_tag(const std::vector<Vec3d> &points)
{
    uint32_t h = 2166136261u;
    for (const Vec3d &p : points)
        for (int k = 0; k < 3; ++k) {
            const int64_t q = int64_t(std::llround(p(k) * 10000.));
            h ^= uint32_t(uint64_t(q) & 0xffffffffu);
            h *= 16777619u;
            h ^= uint32_t((uint64_t(q) >> 32) & 0xffffffffu);
            h *= 16777619u;
        }
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%08x", unsigned(h));
    return buf;
}

int parsed_feature_index(const std::string &name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.compare(0, n, prefix) != 0 || name[n] != '#')
        return -1;
    const size_t end = name.find('@', n + 1);
    try {
        return std::stoi(name.substr(n + 1, end == std::string::npos ? std::string::npos : end - n - 1));
    } catch (...) {
        return -1;
    }
}

// The source key a feature volume carries, empty when it has none.
std::string parsed_feature_tag(const std::string &name)
{
    const size_t at = name.find('@');
    return at == std::string::npos ? std::string() : name.substr(at + 1);
}

int object_idx_of(const Selection &selection, const ModelObject *mo)
{
    const int idx = selection.get_object_idx();
    if (idx >= 0 || mo == nullptr)
        return idx;
    const Model &model = wxGetApp().plater()->model();
    for (size_t i = 0; i < model.objects.size(); ++i)
        if (model.objects[i] == mo)
            return int(i);
    return -1;
}

bool face_contains_vertex(const indexed_triangle_set &its, int face, int vertex)
{
    const auto &tri = its.indices[face];
    return tri(0) == vertex || tri(1) == vertex || tri(2) == vertex;
}

const ModelVolume *first_model_part(const ModelObject *mo)
{
    if (mo == nullptr)
        return nullptr;
    for (const ModelVolume *v : mo->volumes)
        if (v != nullptr && v->is_model_part())
            return v;
    return nullptr;
}

// Distance from p to the segment ab in screen space; t (when asked for) is where on the segment
// the closest point sits, which the caller needs to test whether that part of the edge is visible.
double point_segment_distance(const Vec2d &p, const Vec2d &a, const Vec2d &b, double *t_out = nullptr)
{
    const Vec2d  ab = b - a;
    const double l2 = ab.squaredNorm();
    const double t  = l2 > 0. ? std::clamp((p - a).dot(ab) / l2, 0., 1.) : 0.;
    if (t_out != nullptr)
        *t_out = t;
    return (a + t * ab - p).norm();
}

// Grow a mesh-edge segment (a, b) with the adjacent faces face/other into the full geometric edge:
// a long edge is normally tessellated into many collinear mesh segments, and each of them must be
// part of the same chamfer. The walk stops where the adjacent face normals change or the mesh turns
// a corner. `turn_a` / `turn_b` report, in degrees, how the boundary continues past either end when
// it keeps the same pair of faces and only bends: a small value is the tell-tale of a tessellated
// curve, where the edge is one segment of a round rim rather than a straight edge of its own.
// Returns false for a coplanar crease (the tessellation diagonal of a flat face), which is not an
// edge that can be dressed.
bool extend_feature_edge(const indexed_triangle_set &its, const std::vector<std::vector<int>> &vertex_faces,
                         const std::vector<Vec3f> &normals, int a, int b, int face, int other, int &out_a,
                         int &out_b, Vec3d &out_na, Vec3d &out_nb, double &turn_a, double &turn_b)
{
    const Vec3d na = normals[face].cast<double>();
    const Vec3d nb = normals[other].cast<double>();
    if (na.dot(nb) > EDGE_MIN_CREASE_DOT)
        return false; // coplanar: a flat face's diagonal, or a seam of a smooth surface

    auto faces_of_edge = [&](int x, int y) {
        std::vector<int> fs;
        for (int g : vertex_faces[size_t(x)])
            if (face_contains_vertex(its, g, y))
                fs.push_back(g);
        return fs;
    };
    // A segment continues the edge when its two adjacent faces carry the same normal pair.
    auto matching = [&](int x, int y) {
        const std::vector<int> fs = faces_of_edge(x, y);
        if (fs.size() != 2)
            return false;
        const Vec3d f0 = normals[fs[0]].cast<double>();
        const Vec3d f1 = normals[fs[1]].cast<double>();
        return (f0.dot(na) > 1. - 1e-5 && f1.dot(nb) > 1. - 1e-5) ||
               (f0.dot(nb) > 1. - 1e-5 && f1.dot(na) > 1. - 1e-5);
    };
    auto walk = [&](int from, int towards) {
        const Vec3d dir   = (its.vertices[size_t(from)].cast<double>() - its.vertices[size_t(towards)].cast<double>()).normalized();
        int         cur   = from;
        int         prev  = towards;
        size_t      steps = 0;
        for (;;) {
            int next = -1;
            for (int g : vertex_faces[size_t(cur)]) {
                for (int k = 0; k < 3 && next < 0; ++k) {
                    const int w = its.indices[g](k);
                    if (w == cur || w == prev)
                        continue;
                    const Vec3d dd = (its.vertices[size_t(w)].cast<double>() - its.vertices[size_t(cur)].cast<double>()).normalized();
                    if (dd.dot(dir) < 1. - 1e-6)
                        continue;
                    if (matching(cur, w))
                        next = w;
                }
                if (next >= 0)
                    break;
            }
            if (next < 0 || ++steps > its.vertices.size())
                break;
            prev = cur;
            cur  = next;
        }
        return cur;
    };
    // The boundary keeps going past `end` while carrying the same two faces, but only bends. `in` is
    // the direction the run travels as it reaches `end`. Face matching is loose here on purpose: on
    // a smooth surface the next segment's faces are a few degrees off, and that is exactly the case
    // worth reporting.
    auto bend = [&](int end, const Vec3d &in) {
        constexpr double LOOSE = 0.9397; // cos(20 degrees)
        double           best  = 0.;
        for (int g : vertex_faces[size_t(end)]) {
            for (int k = 0; k < 3; ++k) {
                const int w = its.indices[g](k);
                if (w == end)
                    continue;
                const Vec3d out = (its.vertices[size_t(w)].cast<double>() - its.vertices[size_t(end)].cast<double>()).normalized();
                if (out.dot(in) <= 0.)
                    continue; // that edge goes back down the run, not onwards
                const std::vector<int> fs = faces_of_edge(end, w);
                if (fs.size() != 2)
                    continue;
                const Vec3d f0 = normals[fs[0]].cast<double>();
                const Vec3d f1 = normals[fs[1]].cast<double>();
                const bool  same = (f0.dot(na) > LOOSE && f1.dot(nb) > LOOSE) || (f0.dot(nb) > LOOSE && f1.dot(na) > LOOSE);
                if (!same)
                    continue;
                const double turn = std::acos(std::clamp(in.dot(out), -1., 1.)) * 180. / PI;
                if (best == 0. || turn < best)
                    best = turn;
            }
        }
        return best;
    };

    const int a2 = walk(a, b);
    const int b2 = walk(b, a);
    if (a2 == b2)
        return false;
    out_a  = a2;
    out_b  = b2;
    out_na = na;
    out_nb = nb;
    // Both ends of the run are known, so the direction of travel is unambiguous even when the run
    // is a single mesh segment (a2 == a and b2 == b).
    const Vec3d along = (its.vertices[size_t(a2)].cast<double>() - its.vertices[size_t(b2)].cast<double>()).normalized();
    turn_a = bend(a2, along);
    turn_b = bend(b2, -along);
    return true;
}

} // namespace

GLGizmoEdgeDress::GLGizmoEdgeDress(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

const ModelObject *GLGizmoEdgeDress::model_object() const
{
    return m_c != nullptr && m_c->selection_info() != nullptr ? m_c->selection_info()->model_object() : nullptr;
}

Transform3d GLGizmoEdgeDress::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

double GLGizmoEdgeDress::object_scale() const
{
    const Transform3d im = instance_matrix();
    const double      s  = (im.linear().col(0).norm() + im.linear().col(1).norm() + im.linear().col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

int GLGizmoEdgeDress::applied_count() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    int n = 0;
    for (const ModelVolume *v : mo->volumes)
        if (v != nullptr && (parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 || parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0 ||
                             parsed_feature_index(v->name, EDGE_LOOP_NAME) >= 0))
            ++n;
    return n;
}

int GLGizmoEdgeDress::next_feature_id() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    int next = 0;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr)
            continue;
        next = std::max(next, parsed_feature_index(v->name, EDGE_CHAMFER_NAME) + 1);
        next = std::max(next, parsed_feature_index(v->name, EDGE_FILLET_NAME) + 1);
        next = std::max(next, parsed_feature_index(v->name, EDGE_LOOP_NAME) + 1);
    }
    return next;
}

const std::vector<GLGizmoEdgeDress::HoverEdge> &GLGizmoEdgeDress::cached_edges() const
{
    if (m_edges_dirty) {
        m_edges       = collect_edges();
        m_edges_dirty = false;
    }
    return m_edges;
}

std::string GLGizmoEdgeDress::tag_for_edge(const HoverEdge &edge)
{
    return edge.valid ? feature_tag({ edge.p0, edge.p1 }) : std::string();
}

std::string GLGizmoEdgeDress::tag_for_loop(const std::vector<LoopFrame> &loop) const
{
    if (loop.size() < 3)
        return std::string();
    std::vector<Vec3d> points;
    points.reserve(loop.size() * 2);
    for (const LoopFrame &f : loop) {
        points.push_back(f.p);
        points.push_back(f.q);
    }
    return feature_tag(points);
}

// The feature volume that already dresses this source, or -1. One feature per source: the tag is
// written into the volume name when it is applied.
int GLGizmoEdgeDress::find_applied_volume(const std::string &tag) const
{
    if (tag.empty())
        return -1;
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return -1;
    for (size_t i = 0; i < mo->volumes.size(); ++i) {
        const ModelVolume *v = mo->volumes[i];
        if (v == nullptr)
            continue;
        const bool ours = parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 ||
                          parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0 ||
                          parsed_feature_index(v->name, EDGE_LOOP_NAME) >= 0;
        if (ours && parsed_feature_tag(v->name) == tag)
            return int(i);
    }
    return -1;
}

bool GLGizmoEdgeDress::remove_applied_volume(const std::string &tag)
{
    const int at = find_applied_volume(tag);
    if (at < 0)
        return false;
    ModelObject *mo = const_cast<ModelObject *>(model_object());
    if (mo == nullptr)
        return false;
    const int oi = object_idx_of(m_parent.get_selection(), mo);
    if (oi < 0)
        return false;
    std::vector<ItemForDelete> items;
    items.emplace_back(ItemType::itVolume, oi, at);
    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Remove edge feature"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
    m_edges_dirty = true;
    m_preview_dirty = true;
    m_hover_applied_volume = -1;
    return true;
}

void GLGizmoEdgeDress::clear_hover()
{
    m_hover         = HoverEdge();
    m_hover_loop.clear();
    m_preview_dirty = true;
    m_hover_applied_volume = -1;
    m_preview.reset();
}

void GLGizmoEdgeDress::update_hover(const Vec2d &screen_pos)
{
    const HoverEdge              previous      = m_hover;
    const std::vector<LoopFrame> previous_loop = m_hover_loop;
    m_hover = HoverEdge();
    m_hover_loop.clear();

    // The edge nearest to the cursor in screen space, not in 3D distance to the hit point: the
    // latter picks whichever edge happens to be close in space, so a far edge can win over the one
    // under the cursor. The cursor must be on the object, so empty space does not keep a highlight.
    const ModelObject *mo = model_object();
    if (mo != nullptr) {
        const ClippingPlane *clipping = m_c != nullptr && m_c->object_clipper() != nullptr ? m_c->object_clipper()->get_clipping_plane() : nullptr;
        const GLVolume      *volume   = nullptr;
        const ModelVolume   *mv       = nullptr;
        size_t               facet    = 0;
        Vec3d                hit_world;
        if (raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit_world)) {
            const Camera     &camera   = wxGetApp().plater()->get_camera();
            const Transform3d to_world = instance_matrix();
            // A generous screen-space grab radius, so the edge does not have to be hit exactly.
            const double      tol      = std::max(24.0, 0.04 * double(camera.get_viewport()[3]));
            // Screen space alone would let an edge on the far side of the model win, so a candidate
            // is only accepted when the part of it nearest to the cursor is not behind the surface
            // the cursor is actually on.
            const Transform3d view      = camera.get_view_matrix();
            const double      hit_depth = (view * hit_world).z();
            const double depth_tol = m_loop_mode ? EDGE_LOOP_DEPTH_TOL : EDGE_DEPTH_TOL;
            const auto   visible   = [&view, hit_depth, depth_tol, &to_world](const Vec3d &object_pt) {
                return (view * (to_world * object_pt)).z() >= hit_depth - depth_tol;
            };

            if (m_loop_mode) {
                // The whole boundary loop of the patch under the cursor, so a rim is dressed as one
                // feature instead of its tessellation segments. A rim often borders a smooth band (a
                // bevel, or the rounded side of a low boss) and the cursor lands on that band, where
                // the patch has no loop of its own; loops_for_facet rings outwards in that case.
                // No screen-space radius cap: the loop that comes back may belong to a neighbouring
                // patch and sit some distance from the cursor, and picking the nearest visible rim is
                // the point of whole-loop mode. The depth test keeps rims on the far side out.
                double best = std::numeric_limits<double>::max();
                for (const std::vector<LoopFrame> &loop : loops_for_facet(mv, int(facet))) {
                    if (loop.size() < 3)
                        continue;
                    for (const LoopFrame &f : loop) {
                        const Vec2d a = world_to_screen(camera, to_world * f.p);
                        const Vec2d b = world_to_screen(camera, to_world * f.q);
                        if (!a.allFinite() || !b.allFinite())
                            continue;
                        double       t = 0.;
                        const double d = point_segment_distance(screen_pos, a, b, &t);
                        if (d < best && visible(f.p + t * (f.q - f.p))) {
                            best         = d;
                            m_hover_loop = loop;
                        }
                    }
                }
            } else {
                double best = tol;
                for (const HoverEdge &e : cached_edges()) {
                    const Vec2d a = world_to_screen(camera, to_world * e.p0);
                    const Vec2d b = world_to_screen(camera, to_world * e.p1);
                    if (!a.allFinite() || !b.allFinite())
                        continue;
                    double       t = 0.;
                    const double d = point_segment_distance(screen_pos, a, b, &t);
                    if (d < best && visible(e.p0 + t * (e.p1 - e.p0))) {
                        best    = d;
                        m_hover = e;
                    }
                }
            }
        }
    }

    if (m_loop_mode) {
        const bool loop_changed = m_hover_loop.size() != previous_loop.size() ||
                                  (!m_hover_loop.empty() && ((m_hover_loop.front().p - previous_loop.front().p).norm() > 1e-9 ||
                                                             (m_hover_loop.back().p - previous_loop.back().p).norm() > 1e-9));
        if (loop_changed)
            m_preview_dirty = true;
    } else if (m_hover.valid != previous.valid ||
               (m_hover.valid && ((m_hover.p0 - previous.p0).norm() > 1e-9 || (m_hover.p1 - previous.p1).norm() > 1e-9 ||
                                  (m_hover.n_a - previous.n_a).norm() > 1e-6 || (m_hover.n_b - previous.n_b).norm() > 1e-6)))
        m_preview_dirty = true;

    // Whether the hovered source already carries a feature: shown red, and not applied a second time.
    const int applied = m_loop_mode ? find_applied_volume(tag_for_loop(m_hover_loop))
                                    : find_applied_volume(tag_for_edge(m_hover));
    if (applied != m_hover_applied_volume) {
        m_hover_applied_volume = applied;
        m_preview_dirty        = true;
    }
}

indexed_triangle_set GLGizmoEdgeDress::hover_mesh() const
{
    return mesh_for_edge(m_hover);
}

indexed_triangle_set GLGizmoEdgeDress::active_mesh() const
{
    return m_loop_mode ? mesh_for_loop(m_hover_loop) : hover_mesh();
}

indexed_triangle_set GLGizmoEdgeDress::mesh_for_loop(const std::vector<LoopFrame> &loop) const
{
    if (loop.size() < 3)
        return indexed_triangle_set();

    const double size = std::max(0.01, m_size / object_scale());
    if (m_mode == EdgeDressMode::Fillet)
        return make_loop_fillet(loop, size);
    return make_loop_chamfer(loop, size);
}

const std::vector<std::vector<LoopFrame>> &GLGizmoEdgeDress::loops_for_facet(const ModelVolume *mv, int facet) const
{
    if (mv != m_loops_volume || facet != m_loops_facet) {
        m_loops.clear();
        m_loops_volume = mv;
        m_loops_facet  = facet;
        if (mv != nullptr && facet >= 0 && facet < int(mv->mesh().its.indices.size()))
            m_loops = its_face_patch_loops_around(mv->mesh().its, mv->get_matrix(), facet, EDGE_LOOP_RINGS);
    }
    return m_loops;
}

indexed_triangle_set GLGizmoEdgeDress::mesh_for_edge(const HoverEdge &edge) const
{
    if (!edge.valid)
        return indexed_triangle_set();

    const double size   = std::max(0.01, m_size / object_scale());
    const double margin = 0.5 * size;
    if (m_mode == EdgeDressMode::Fillet)
        return make_edge_fillet(edge.p0, edge.p1, edge.n_a, edge.n_b, size, margin);
    return make_edge_chamfer(edge.p0, edge.p1, edge.n_a, edge.n_b, size, margin);
}

// Enumerate the manifold edges of the first model part in object space, deduplicated and in mesh
// order. Concave edges are included; they simply produce no geometry when swept.
std::vector<GLGizmoEdgeDress::HoverEdge> GLGizmoEdgeDress::collect_edges() const
{
    std::vector<HoverEdge> edges;

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return edges;
    const ModelVolume *mv = first_model_part(mo);
    if (mv == nullptr)
        return edges;

    const indexed_triangle_set &its = mv->mesh().its;
    if (its.indices.empty())
        return edges;

    const Transform3d              trafo   = mv->get_matrix();
    const Eigen::Matrix3d          normal_trafo = trafo.linear().inverse().transpose();
    const std::vector<Vec3f>       normals = its_face_normals(its);

    std::vector<std::vector<int>> vertex_faces(its.vertices.size());
    for (size_t f = 0; f < its.indices.size(); ++f)
        for (int k = 0; k < 3; ++k)
            vertex_faces[its.indices[f](k)].push_back(int(f));

    std::set<std::pair<int, int>> seen;      // mesh segments already walked
    std::set<std::pair<int, int>> seen_full; // merged geometric edges already emitted
    for (size_t f = 0; f < its.indices.size(); ++f) {
        for (int k = 0; k < 3; ++k) {
            const int a  = its.indices[f](k);
            const int b  = its.indices[f]((k + 1) % 3);
            const int lo = std::min(a, b);
            const int hi = std::max(a, b);
            if (lo == hi || !seen.insert({ lo, hi }).second)
                continue;

            int other = -1;
            for (int g : vertex_faces[size_t(lo)]) {
                if (g == int(f) || size_t(g) >= its.indices.size())
                    continue;
                if (face_contains_vertex(its, g, hi)) {
                    other = g;
                    break;
                }
            }
            if (other < 0)
                continue; // open edge

            int   ea, eb;
            Vec3d na, nb;
            double ta = 0., tb = 0.;
            if (!extend_feature_edge(its, vertex_faces, normals, a, b, int(f), other, ea, eb, na, nb, ta, tb))
                continue; // coplanar crease
            // A run that only bends at both ends is a segment of a round rim, not an edge of its
            // own: it is what whole-rim mode is for, and offering the segments here would fight for
            // the cursor with each other and with every straight edge nearby.
            if (ta > 0. && ta < EDGE_CURVE_DEG && tb > 0. && tb < EDGE_CURVE_DEG)
                continue;
            if (!seen_full.insert({ std::min(ea, eb), std::max(ea, eb) }).second)
                continue;

            HoverEdge e;
            e.valid = true;
            e.p0    = trafo * its.vertices[size_t(ea)].cast<double>();
            e.p1    = trafo * its.vertices[size_t(eb)].cast<double>();
            e.n_a   = (normal_trafo * na).normalized();
            e.n_b   = (normal_trafo * nb).normalized();
            e.tag   = feature_tag({ its.vertices[size_t(ea)].cast<double>(),
                                    its.vertices[size_t(eb)].cast<double>() });
            if ((e.p1 - e.p0).norm() < 1e-9)
                continue;
            edges.push_back(e);
        }
    }
    return edges;
}

int GLGizmoEdgeDress::edge_count() const
{
    return int(cached_edges().size());
}

std::vector<std::array<double, 6>> GLGizmoEdgeDress::gizmo_list_edges(int max_count) const
{
    std::vector<std::array<double, 6>> out;
    const std::vector<HoverEdge>      &edges = cached_edges();
    const size_t                       n     = max_count > 0 ? std::min(size_t(max_count), edges.size()) : edges.size();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        const HoverEdge &e = edges[i];
        out.push_back({ e.p0.x(), e.p0.y(), e.p0.z(), e.p1.x(), e.p1.y(), e.p1.z() });
    }
    return out;
}

bool GLGizmoEdgeDress::gizmo_apply_edge(int index)
{
    const std::vector<HoverEdge> &edges = cached_edges();
    if (index < 0 || index >= int(edges.size()))
        return false;
    const HoverEdge &edge = edges[index];
    // One feature per edge: an edge that is already dressed is not dressed again, it is removed by
    // right-clicking it (or through remove_edge).
    if (find_applied_volume(tag_for_edge(edge)) >= 0)
        return false;
    const indexed_triangle_set its = mesh_for_edge(edge);
    if (its.indices.empty())
        return false; // concave edge: the sweep has no valid cross section
    add_named_negative(feature_name(m_mode == EdgeDressMode::Fillet ? EDGE_FILLET_NAME : EDGE_CHAMFER_NAME,
                                    next_feature_id(), tag_for_edge(edge)),
                       its);
    return true;
}

std::vector<int> GLGizmoEdgeDress::applied_edge_indices() const
{
    std::vector<int> out;
    const std::vector<HoverEdge> &edges = cached_edges();
    for (size_t i = 0; i < edges.size(); ++i)
        if (find_applied_volume(tag_for_edge(edges[i])) >= 0)
            out.push_back(int(i));
    return out;
}

std::vector<std::array<int, 2>> GLGizmoEdgeDress::applied_loop_indices(int facet) const
{
    std::vector<std::array<int, 2>> out;
    const ModelVolume *mv = first_model_part(model_object());
    const int          f  = facet >= 0 ? facet : m_loops_facet;
    const std::vector<std::vector<LoopFrame>> &loops = loops_for_facet(mv, f);
    for (size_t i = 0; i < loops.size(); ++i)
        if (find_applied_volume(tag_for_loop(loops[i])) >= 0)
            out.push_back({ f, int(i) });
    return out;
}

void GLGizmoEdgeDress::rebuild_preview()
{
    m_preview_dirty = false;
    // GLModel::init_from only builds a model once: an already initialized one has to be reset, or
    // the preview would keep showing whatever it was first built from.
    m_preview.reset();
    m_preview_applied.reset();

    // The hovered edge or loop, unless it already carries a feature: an applied one is shown in the
    // applied overlay instead, and must not look like something waiting to be applied.
    indexed_triangle_set its = m_hover_applied_volume >= 0 ? indexed_triangle_set() : active_mesh();
    if (!its.indices.empty()) {
        m_preview.model.init_from(its);
        m_preview.model.set_color(HOVER_COLOR);
    }

    // Every applied feature, in the same red the hole gizmo marks applied holes with.
    indexed_triangle_set applied;
    if (const ModelObject *mo = model_object(); mo != nullptr) {
        for (const ModelVolume *v : mo->volumes) {
            if (v == nullptr || !v->is_negative_volume())
                continue;
            const bool ours = parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 ||
                              parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0 ||
                              parsed_feature_index(v->name, EDGE_LOOP_NAME) >= 0;
            if (!ours)
                continue;
            indexed_triangle_set part = v->mesh().its;
            const Transform3d    m    = v->get_matrix();
            if (!m.isApprox(Transform3d::Identity()))
                for (stl_vertex &p : part.vertices)
                    p = (m * Eigen::Vector3d(p(0), p(1), p(2))).cast<float>();
            its_merge(applied, part);
        }
    }
    if (!applied.indices.empty()) {
        m_preview_applied.model.init_from(applied);
        m_preview_applied.model.set_color(APPLIED_COLOR);
    }
}

void GLGizmoEdgeDress::add_named_negative(const std::string &name, const indexed_triangle_set &its)
{
    ModelObject *mo = const_cast<ModelObject *>(model_object());
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    plater->take_snapshot(_u8L("Apply edge feature"));
    indexed_triangle_set copy = its;
    ModelVolume         *v    = mo->add_volume(TriangleMesh(std::move(copy)), ModelVolumeType::NEGATIVE_VOLUME, false);
    v->name = name;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

// ---------------------------------------------------------------------------------------------
// MCP surface
// ---------------------------------------------------------------------------------------------

void GLGizmoEdgeDress::set_mode(EdgeDressMode mode)
{
    if (m_mode == mode)
        return;
    m_mode          = mode;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoEdgeDress::set_size(double size)
{
    m_size          = std::clamp(size, EDGE_SIZE_MIN, EDGE_SIZE_MAX);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

bool GLGizmoEdgeDress::gizmo_hover_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    rebuild_preview();
    m_parent.set_as_dirty();
    return hover_edge_valid();
}

bool GLGizmoEdgeDress::gizmo_apply_hovered()
{
    if (!hover_edge_valid())
        return false;
    // Already dressed: it is removed by right-click, not dressed a second time.
    if (m_hover_applied_volume >= 0)
        return false;
    const indexed_triangle_set its = active_mesh();
    if (its.indices.empty())
        return false;
    const char *prefix = m_loop_mode ? EDGE_LOOP_NAME : (m_mode == EdgeDressMode::Fillet ? EDGE_FILLET_NAME : EDGE_CHAMFER_NAME);
    add_named_negative(feature_name(prefix, next_feature_id(),
                                    m_loop_mode ? tag_for_loop(m_hover_loop) : tag_for_edge(m_hover)),
                       its);
    return true;
}

bool GLGizmoEdgeDress::gizmo_apply_at(const Vec2d &screen_pos)
{
    if (!gizmo_hover_at(screen_pos))
        return false;
    return gizmo_apply_hovered();
}

bool GLGizmoEdgeDress::gizmo_remove_hovered()
{
    if (!hover_edge_valid())
        return false;
    return remove_applied_volume(m_loop_mode ? tag_for_loop(m_hover_loop) : tag_for_edge(m_hover));
}

bool GLGizmoEdgeDress::gizmo_remove_edge(int index)
{
    const std::vector<HoverEdge> &edges = cached_edges();
    if (index < 0 || index >= int(edges.size()))
        return false;
    return remove_applied_volume(tag_for_edge(edges[index]));
}

bool GLGizmoEdgeDress::gizmo_remove_loop(int facet, int index)
{
    if (facet < 0)
        facet = m_loops_facet;
    const std::vector<std::vector<LoopFrame>> &loops = loops_for_facet(first_model_part(model_object()), facet);
    if (index < 0 || index >= int(loops.size()))
        return false;
    return remove_applied_volume(tag_for_loop(loops[index]));
}

void GLGizmoEdgeDress::gizmo_clear_all()
{
    ModelObject *mo = const_cast<ModelObject *>(model_object());
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if (v == nullptr)
            continue;
        if (parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 || parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0 ||
            parsed_feature_index(v->name, EDGE_LOOP_NAME) >= 0)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Clear edge features"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
}

void GLGizmoEdgeDress::gizmo_refresh()
{
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoEdgeDress::set_loop_mode(bool on)
{
    if (m_loop_mode == on)
        return;
    m_loop_mode = on;
    m_hover_loop.clear();
    m_hover_applied_volume = -1;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

int GLGizmoEdgeDress::loop_count(int facet) const
{
    if (facet < 0)
        facet = m_loops_facet;
    return int(loops_for_facet(first_model_part(model_object()), facet).size());
}

std::vector<std::vector<std::array<double, 3>>> GLGizmoEdgeDress::gizmo_list_loops(int facet, int max_count) const
{
    std::vector<std::vector<std::array<double, 3>>> out;
    if (facet < 0)
        facet = m_loops_facet;
    const std::vector<std::vector<LoopFrame>> &loops = loops_for_facet(first_model_part(model_object()), facet);
    const size_t n = max_count > 0 ? std::min(size_t(max_count), loops.size()) : loops.size();
    out.reserve(n);
    for (size_t i = 0; i < n; ++i) {
        std::vector<std::array<double, 3>> pts;
        pts.reserve(loops[i].size());
        for (const LoopFrame &f : loops[i])
            pts.push_back({ f.p.x(), f.p.y(), f.p.z() });
        out.push_back(std::move(pts));
    }
    return out;
}

bool GLGizmoEdgeDress::gizmo_apply_loop(int facet, int index)
{
    if (facet < 0)
        facet = m_loops_facet;
    const std::vector<std::vector<LoopFrame>> &loops = loops_for_facet(first_model_part(model_object()), facet);
    if (index < 0 || index >= int(loops.size()))
        return false;
    if (find_applied_volume(tag_for_loop(loops[index])) >= 0)
        return false; // already dressed: remove_loop is how it goes away
    const indexed_triangle_set its = mesh_for_loop(loops[index]);
    if (its.indices.empty())
        return false;
    add_named_negative(feature_name(EDGE_LOOP_NAME, next_feature_id(), tag_for_loop(loops[index])), its);
    return true;
}

// ---------------------------------------------------------------------------------------------
// GLGizmoBase callbacks
// ---------------------------------------------------------------------------------------------

bool GLGizmoEdgeDress::on_init()
{
    m_shortcut_key = WXK_CONTROL_D;

    m_desc["name"]          = _L("Edge chamfer / fillet");
    m_desc["chamfer"]       = _L("Chamfer");
    m_desc["fillet"]        = _L("Fillet");
    m_desc["size"]          = _L("Size");
    m_desc["loop"]          = _L("Whole loop / rim");
    m_desc["apply"]         = _L("Apply to hovered edge");
    m_desc["clear"]         = _L("Clear all");
    m_desc["hover_hint"]    = _L("Hover an edge, then click it or use Apply.");
    m_desc["no_edge"]       = _L("No edge under the cursor.");
    m_desc["edge_hovered"]  = _L("Edge under the cursor.");
    m_desc["already_done"]  = _L("Already chamfered or filleted.");
    m_desc["remove_hint"]   = _L("Right-click an applied feature to remove it.");
    m_desc["applied"]       = _L("Applied");
    return true;
}

std::string GLGizmoEdgeDress::on_get_name() const
{
    return _u8L("Edge chamfer / fillet");
}

bool GLGizmoEdgeDress::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoEdgeDress::on_set_state()
{
    if (get_state() == On) {
        m_preview_dirty  = true;
        m_hover_computed = false;
        m_parent.set_as_dirty();
    } else {
        clear_hover();
    }
}

CommonGizmosDataID GLGizmoEdgeDress::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoEdgeDress::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_edges_dirty    = true;
        m_old_object     = nullptr;
        m_hover_computed = false;
        clear_hover();
        return;
    }

    if (mo != m_old_object || int(mo->volumes.size()) != m_old_volume_count || instance_matrix().matrix() != m_old_matrix.matrix()) {
        m_old_object       = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_old_matrix       = instance_matrix();
        m_edges_dirty      = true; // the edge list is tied to the object's mesh
        m_preview_dirty    = true;
        m_hover_computed   = false;
        m_parent.set_as_dirty();
    }
}

void GLGizmoEdgeDress::on_render()
{
    if (get_state() != On)
        return;

    // Track the cursor so the edge under it is highlighted before the click. The work is skipped
    // while neither the cursor nor the camera moves, which keeps a still scene cheap.
    const Camera     &camera = wxGetApp().plater()->get_camera();
    const Vec2d       mouse  = m_parent.get_local_mouse_position();
    const Transform3d view   = camera.get_view_matrix();
    if (!m_hover_computed || mouse.x() != m_last_mouse.x() || mouse.y() != m_last_mouse.y() ||
        !view.matrix().isApprox(m_last_view.matrix(), 1e-12)) {
        update_hover(mouse);
        m_last_mouse    = mouse;
        m_last_view     = view;
        m_hover_computed = true;
    }
    if (m_preview_dirty)
        rebuild_preview();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));

    const Transform3d view_model_matrix = camera.get_view_matrix() * instance_matrix();
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    m_preview_applied.model.render(shader);
    m_preview.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

bool GLGizmoEdgeDress::on_mouse(const wxMouseEvent &mouse_event)
{
    if (get_state() != On)
        return false;
    if (mouse_event.RightDown()) {
        // Right-click removes the feature the cursor is on, like the hole gizmo removes a hole.
        gizmo_hover_at(m_parent.get_local_mouse_position());
        return gizmo_remove_hovered();
    }
    if (!mouse_event.LeftDown())
        return false;
    // Apply only when the cursor is actually on an edge, so a click on empty space still falls
    // through to the canvas. An edge that is already dressed is not dressed again.
    gizmo_hover_at(m_parent.get_local_mouse_position());
    if (!hover_edge_valid())
        return false;
    gizmo_apply_hovered();
    return true;
}

void GLGizmoEdgeDress::on_render_input_window(float x, float y, float bottom_limit)
{
    if (model_object() == nullptr)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(6.0f);
    const float left_width    = m_imgui->calc_text_size(m_desc.at("size")).x + m_imgui->scaled(2.0f);

    // Mode: chamfer or fillet.
    {
        const bool chamfer = m_mode == EdgeDressMode::Chamfer;
        if (chamfer) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
        }
        const bool clicked = m_imgui->button(m_desc.at("chamfer"));
        if (chamfer)
            ImGui::PopStyleColor(3);
        if (clicked)
            set_mode(EdgeDressMode::Chamfer);
        ImGui::SameLine();
    }
    {
        const bool fillet = m_mode == EdgeDressMode::Fillet;
        if (fillet) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
        }
        const bool clicked = m_imgui->button(m_desc.at("fillet"));
        if (fillet)
            ImGui::PopStyleColor(3);
        if (clicked)
            set_mode(EdgeDressMode::Fillet);
    }

    {
        bool loop = m_loop_mode;
        if (ImGui::Checkbox(m_desc.at("loop").utf8_str().data(), &loop))
            set_loop_mode(loop);
    }

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("size"));
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    float size = float(m_size);
    if (m_imgui->bbl_slider_float_style("##edge_size", &size, float(EDGE_SIZE_MIN), float(EDGE_SIZE_MAX), "%.2f", 0.05f, true))
        set_size(size);
    ImGui::PopItemWidth();
    // A field next to the slider, so an exact size can be typed instead of dragged.
    ImGui::SameLine(0.f, m_imgui->scaled(1.0f));
    ImGui::PushItemWidth(m_imgui->scaled(4.5f));
    float typed = float(m_size);
    if (ImGui::InputFloat("##edge_size_in", &typed, 0.05f, 0.5f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
        set_size(typed);
    ImGui::PopItemWidth();

    if (m_imgui->button(m_desc.at("apply")))
        gizmo_apply_hovered();
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("clear")))
        gizmo_clear_all();

    if (hover_edge_valid() && m_hover_applied_volume >= 0)
        m_imgui->text(m_desc.at("already_done"));
    else
        m_imgui->text(hover_edge_valid() ? m_desc.at("edge_hovered") : m_desc.at("no_edge"));
    m_imgui->text(m_desc.at("hover_hint"));
    if (applied_count() > 0)
        m_imgui->text(m_desc.at("remove_hint"));
    const wxString applied = wxString::Format("%s: %d", m_desc.at("applied").c_str(), applied_count());
    m_imgui->text(applied);

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoEdgeDress::on_register_raycasters_for_picking()
{
    // Edge picking uses raycast_object_face on the scene volumes, so no gizmo raycaster is added.
}

void GLGizmoEdgeDress::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
}

} // namespace GUI
} // namespace Slic3r
