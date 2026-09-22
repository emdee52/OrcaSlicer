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
#include <cstdlib>
#include <set>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char *EDGE_CHAMFER_NAME = "EdgeChamfer";
constexpr const char *EDGE_FILLET_NAME  = "EdgeFillet";

const ColorRGBA ALL_COLOR{ 0.25f, 0.70f, 1.00f, 0.40f };
const ColorRGBA HOVER_COLOR{ 0.10f, 1.00f, 0.20f, 0.90f };

// Feature volumes are named <prefix>#<id>; the id is matched by prefix, like the hole features.
std::string feature_name(const char *prefix, int idx)
{
    return std::string(prefix) + "#" + std::to_string(idx);
}

int parsed_feature_index(const std::string &name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (name.size() <= n || name.compare(0, n, prefix) != 0 || name[n] != '#')
        return -1;
    try {
        return std::stoi(name.substr(n + 1));
    } catch (...) {
        return -1;
    }
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

double point_segment_distance(const Vec2d &p, const Vec2d &a, const Vec2d &b)
{
    const Vec2d  ab = b - a;
    const double l2 = ab.squaredNorm();
    const double t  = l2 > 0. ? std::clamp((p - a).dot(ab) / l2, 0., 1.) : 0.;
    return (a + t * ab - p).norm();
}

// Grow a mesh-edge segment (a, b) with the adjacent faces face/other into the full geometric edge:
// a long edge is normally tessellated into many collinear mesh segments, and each of them must be
// part of the same chamfer. The walk stops where the adjacent face normals change or the mesh turns
// a corner. Returns false for a coplanar crease (the tessellation diagonal of a flat face), which is
// not an edge that can be dressed.
bool extend_feature_edge(const indexed_triangle_set &its, const std::vector<std::vector<int>> &vertex_faces,
                         const std::vector<Vec3f> &normals, int a, int b, int face, int other, int &out_a,
                         int &out_b, Vec3d &out_na, Vec3d &out_nb)
{
    const Vec3d na = normals[face].cast<double>();
    const Vec3d nb = normals[other].cast<double>();
    if (na.dot(nb) > 1. - 1e-5)
        return false; // flat crease

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

    const int a2 = walk(a, b);
    const int b2 = walk(b, a);
    if (a2 == b2)
        return false;
    out_a  = a2;
    out_b  = b2;
    out_na = na;
    out_nb = nb;
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
        if (v != nullptr && (parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 || parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0))
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

void GLGizmoEdgeDress::clear_hover()
{
    m_hover         = HoverEdge();
    m_preview_dirty = true;
    m_preview.reset();
}

void GLGizmoEdgeDress::update_hover(const Vec2d &screen_pos)
{
    const HoverEdge previous = m_hover;
    m_hover                  = HoverEdge();

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
            const Camera    &camera   = wxGetApp().plater()->get_camera();
            const Transform3d to_world = instance_matrix();
            const double     tol      = 12.0;

            double best = tol;
            for (const HoverEdge &e : cached_edges()) {
                const Vec2d a = world_to_screen(camera, to_world * e.p0);
                const Vec2d b = world_to_screen(camera, to_world * e.p1);
                if (!a.allFinite() || !b.allFinite())
                    continue;
                const double d = point_segment_distance(screen_pos, a, b);
                if (d < best) {
                    best    = d;
                    m_hover = e;
                }
            }
        }
    }

    if (m_hover.valid != previous.valid ||
        (m_hover.valid && ((m_hover.p0 - previous.p0).norm() > 1e-9 || (m_hover.p1 - previous.p1).norm() > 1e-9 ||
                           (m_hover.n_a - previous.n_a).norm() > 1e-6 || (m_hover.n_b - previous.n_b).norm() > 1e-6)))
        m_preview_dirty = true;
}

indexed_triangle_set GLGizmoEdgeDress::hover_mesh() const
{
    return mesh_for_edge(m_hover);
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
    const ModelVolume *mv = nullptr;
    for (const ModelVolume *v : mo->volumes)
        if (v != nullptr && v->is_model_part()) {
            mv = v;
            break;
        }
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
            if (!extend_feature_edge(its, vertex_faces, normals, a, b, int(f), other, ea, eb, na, nb))
                continue; // coplanar crease
            if (!seen_full.insert({ std::min(ea, eb), std::max(ea, eb) }).second)
                continue;

            HoverEdge e;
            e.valid = true;
            e.p0    = trafo * its.vertices[size_t(ea)].cast<double>();
            e.p1    = trafo * its.vertices[size_t(eb)].cast<double>();
            e.n_a   = (normal_trafo * na).normalized();
            e.n_b   = (normal_trafo * nb).normalized();
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
    const indexed_triangle_set its = mesh_for_edge(edges[index]);
    if (its.indices.empty())
        return false; // concave edge: the sweep has no valid cross section
    add_named_negative(feature_name(m_mode == EdgeDressMode::Fillet ? EDGE_FILLET_NAME : EDGE_CHAMFER_NAME, next_feature_id()), its);
    return true;
}

void GLGizmoEdgeDress::rebuild_preview()
{
    m_preview_dirty = false;
    indexed_triangle_set its = hover_mesh();
    if (its.indices.empty()) {
        m_preview.reset();
        return;
    }
    m_preview.model.init_from(its);
    m_preview.model.set_color(HOVER_COLOR);
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
    m_size          = std::max(0.01, size);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

bool GLGizmoEdgeDress::gizmo_hover_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    rebuild_preview();
    m_parent.set_as_dirty();
    return m_hover.valid;
}

bool GLGizmoEdgeDress::gizmo_apply_hovered()
{
    if (!m_hover.valid)
        return false;
    const indexed_triangle_set its = hover_mesh();
    if (its.indices.empty())
        return false;
    add_named_negative(feature_name(m_mode == EdgeDressMode::Fillet ? EDGE_FILLET_NAME : EDGE_CHAMFER_NAME, next_feature_id()), its);
    return true;
}

bool GLGizmoEdgeDress::gizmo_apply_at(const Vec2d &screen_pos)
{
    if (!gizmo_hover_at(screen_pos))
        return false;
    return gizmo_apply_hovered();
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
        if (parsed_feature_index(v->name, EDGE_CHAMFER_NAME) >= 0 || parsed_feature_index(v->name, EDGE_FILLET_NAME) >= 0)
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
    m_desc["apply"]         = _L("Apply to hovered edge");
    m_desc["clear"]         = _L("Clear all");
    m_desc["hover_hint"]    = _L("Hover an edge, then click it or use Apply.");
    m_desc["no_edge"]       = _L("No edge under the cursor.");
    m_desc["edge_hovered"]  = _L("Edge under the cursor.");
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
        m_preview_dirty = true;
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
        m_edges_dirty  = true;
        m_old_object   = nullptr;
        clear_hover();
        return;
    }

    if (mo != m_old_object || int(mo->volumes.size()) != m_old_volume_count || instance_matrix().matrix() != m_old_matrix.matrix()) {
        m_old_object       = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_old_matrix       = instance_matrix();
        m_edges_dirty      = true; // the edge list is tied to the object's mesh
        m_preview_dirty    = true;
        m_parent.set_as_dirty();
    }
}

void GLGizmoEdgeDress::on_render()
{
    if (get_state() != On)
        return;

    // Track the cursor so the edge under it is highlighted before the click.
    update_hover(m_parent.get_local_mouse_position());
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

    const Camera   &camera            = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * instance_matrix();
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    m_preview.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

bool GLGizmoEdgeDress::on_mouse(const wxMouseEvent &mouse_event)
{
    if (get_state() != On || !mouse_event.LeftDown())
        return false;
    // Apply only when the cursor is actually on an edge, so a click on empty space still falls
    // through to the canvas.
    gizmo_hover_at(m_parent.get_local_mouse_position());
    if (!m_hover.valid)
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

    const float sliders_width = m_imgui->scaled(7.0f);
    const float left_width    = m_imgui->calc_text_size(m_desc.at("size")).x + m_imgui->scaled(1.0f);

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

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("size"));
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    float size = float(m_size);
    if (ImGui::InputFloat("##edge_size", &size, 0.05f, 0.5f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
        set_size(size);
    ImGui::PopItemWidth();

    if (m_imgui->button(m_desc.at("apply")))
        gizmo_apply_hovered();
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("clear")))
        gizmo_clear_all();

    m_imgui->text(m_hover.valid ? m_desc.at("edge_hovered") : m_desc.at("no_edge"));
    m_imgui->text(m_desc.at("hover_hint"));
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
