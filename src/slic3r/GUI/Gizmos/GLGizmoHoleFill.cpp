#include "GLGizmoHoleFill.hpp"

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
#include <cstring>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char *HOLE_FILL_NAME = "HoleFill";

const ColorRGBA HOVER_COLOR{ 0.10f, 1.00f, 0.20f, 0.90f };
// Applied plugs, the same red the hole gizmo uses for applied holes.
const ColorRGBA APPLIED_COLOR{ 1.00f, 0.15f, 0.15f, 0.80f };

// Fill radius, in world mm, that the panel offers. Small enough to stay on one flat mark, large
// enough to span a watermark on a curved wall.
constexpr double FILL_RADIUS_MIN = 0.5;
constexpr double FILL_RADIUS_MAX = 50.0;

// Feature volumes are named <prefix>#<id>, the id matched by prefix like the hole features.
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

bool is_fill_volume(const ModelVolume *v)
{
    return v != nullptr && parsed_feature_index(v->name, HOLE_FILL_NAME) >= 0;
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

} // namespace

GLGizmoHoleFill::GLGizmoHoleFill(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

ModelObject *GLGizmoHoleFill::model_object() const
{
    return m_c != nullptr && m_c->selection_info() != nullptr ? m_c->selection_info()->model_object() : nullptr;
}

Transform3d GLGizmoHoleFill::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

double GLGizmoHoleFill::object_scale() const
{
    const Transform3d im = instance_matrix();
    const double      s  = (im.linear().col(0).norm() + im.linear().col(1).norm() + im.linear().col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

double GLGizmoHoleFill::volume_scale(const ModelVolume *mv) const
{
    if (mv == nullptr)
        return 1.0;
    const Eigen::Matrix3d L = mv->get_matrix().linear();
    const double          s = (L.col(0).norm() + L.col(1).norm() + L.col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

int GLGizmoHoleFill::applied_count() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    int n = 0;
    for (const ModelVolume *v : mo->volumes)
        if (is_fill_volume(v))
            ++n;
    return n;
}

int GLGizmoHoleFill::next_feature_id() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    int next = 0;
    for (const ModelVolume *v : mo->volumes)
        next = std::max(next, parsed_feature_index(v->name, HOLE_FILL_NAME) + 1);
    return next;
}

int GLGizmoHoleFill::find_covering_fill(const Vec3d &hit_world) const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return -1;
    const Transform3d to_world = instance_matrix();
    for (size_t i = 0; i < mo->volumes.size(); ++i) {
        const ModelVolume *v = mo->volumes[i];
        if (!is_fill_volume(v))
            continue;
        const indexed_triangle_set &its = v->mesh().its;
        if (its.vertices.empty())
            continue;
        Vec3f c = Vec3f::Zero();
        for (const stl_vertex &p : its.vertices)
            c += p;
        c /= float(its.vertices.size());
        const Vec3d center_world = to_world * (v->get_matrix() * c.cast<double>());
        if ((center_world - hit_world).norm() < m_radius)
            return int(i);
    }
    return -1;
}

void GLGizmoHoleFill::clear_hover()
{
    m_hover                = Hover();
    m_hover_applied        = -1;
    m_preview_dirty        = true;
    m_preview.reset();
}

void GLGizmoHoleFill::update_hover(const Vec2d &screen_pos)
{
    const Hover previous = m_hover;
    m_hover = Hover();

    const ModelObject *mo = model_object();
    if (mo != nullptr) {
        const ClippingPlane *clipping = m_c != nullptr && m_c->object_clipper() != nullptr ? m_c->object_clipper()->get_clipping_plane() : nullptr;
        const GLVolume      *volume   = nullptr;
        const ModelVolume   *mv       = nullptr;
        size_t               facet    = 0;
        Vec3d                hit_world;
        if (raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit_world)) {
            // A click on an applied plug is not a new fill: the surface under it is already covered.
            if (mv != nullptr && !is_fill_volume(mv) && facet < mv->mesh().its.indices.size()) {
                m_hover.valid     = true;
                m_hover.mv        = mv;
                m_hover.facet     = int(facet);
                m_hover.hit_world = hit_world;
                const Vec3d hit_obj = instance_matrix().inverse() * hit_world;
                m_hover.seed_local  = mv->get_matrix().inverse() * hit_obj;
            }
        }
    }

    if (m_hover.valid != previous.valid || (m_hover.valid && (m_hover.mv != previous.mv || m_hover.facet != previous.facet ||
                                                              (m_hover.hit_world - previous.hit_world).norm() > 1e-6)))
        m_preview_dirty = true;

    const int applied = m_hover.valid ? find_covering_fill(m_hover.hit_world) : -1;
    if (applied != m_hover_applied) {
        m_hover_applied = applied;
        m_preview_dirty = true;
    }
}

indexed_triangle_set GLGizmoHoleFill::fill_mesh(const Hover &hover) const
{
    return fill_mesh(std::vector<Hover>{ hover });
}

indexed_triangle_set GLGizmoHoleFill::fill_mesh(const std::vector<Hover> &seeds) const
{
    if (seeds.empty())
        return indexed_triangle_set();

    // A stroke stays on one volume: the plug is built from that volume's own mesh.
    const ModelVolume *mv = seeds.front().mv;
    if (mv == nullptr)
        return indexed_triangle_set();

    std::vector<Vec3d> points;
    std::vector<int>   facets;
    points.reserve(seeds.size());
    facets.reserve(seeds.size());
    for (const Hover &h : seeds)
        if (h.valid && h.mv == mv && h.facet >= 0) {
            points.push_back(h.seed_local);
            facets.push_back(h.facet);
        }
    if (points.empty())
        return indexed_triangle_set();

    const double s       = object_scale() * volume_scale(mv);
    const double r_local = m_radius / (s > 1e-9 ? s : 1.0);
    indexed_triangle_set hull = cavity_fill_hull(mv->mesh().its, points, facets, r_local);
    if (hull.indices.empty())
        return hull;

    // the hull is tessellated in the volume's own space: bring it into object space, where it is
    // added and rendered.
    const Transform3d m = mv->get_matrix();
    if (!m.isApprox(Transform3d::Identity()))
        for (stl_vertex &p : hull.vertices)
            p = (m * p.cast<double>()).cast<float>();
    return hull;
}

void GLGizmoHoleFill::record_stroke_sample()
{
    if (!m_hover.valid || m_hover.mv == nullptr)
        return;
    if (!m_stroke.empty() && m_stroke.front().mv != m_hover.mv)
        return; // a stroke covers a single volume
    for (const Hover &h : m_stroke)
        if (h.facet == m_hover.facet)
            return; // already part of this stroke
    // Ignore samples that are not over a depression: painting a flat wall does nothing, and a drag
    // across the wall to the next depression must not accumulate junk that would bridge the two.
    if (fill_mesh(std::vector<Hover>{m_hover}).empty())
        return;
    m_stroke.push_back(m_hover);
    m_preview_dirty = true;
}

void GLGizmoHoleFill::commit_stroke()
{
    if (m_stroke.empty())
        return;
    const indexed_triangle_set its = fill_mesh(m_stroke);
    m_stroke.clear();
    if (its.indices.empty())
        return; // painted a plain wall: nothing to fill
    add_named_fill(feature_name(HOLE_FILL_NAME, next_feature_id()), its);
    m_hover_applied = -1;
    m_preview_dirty = true;
}

void GLGizmoHoleFill::rebuild_preview()
{
    m_preview_dirty = false;
    // GLModel::init_from only builds a model once: an already initialized one has to be reset, or
    // the preview would keep showing whatever it was first built from.
    m_preview.reset();
    m_preview_applied.reset();

    // The plug for the current stroke, or the hovered one, unless the face already carries a plug:
    // an applied plug is shown in the applied overlay instead, and must not look like something
    // waiting to be applied.
    std::vector<Hover> seeds;
    if (m_painting && !m_stroke.empty())
        seeds = m_stroke;
    else if (m_hover.valid && m_hover_applied < 0)
        seeds.push_back(m_hover);
    if (!seeds.empty()) {
        const indexed_triangle_set its = fill_mesh(seeds);
        if (!its.indices.empty()) {
            m_preview.model.init_from(its);
            m_preview.model.set_color(HOVER_COLOR);
        }
    }

    // Every applied plug, in the same red the hole gizmo marks applied holes with.
    indexed_triangle_set applied;
    if (const ModelObject *mo = model_object(); mo != nullptr) {
        for (const ModelVolume *v : mo->volumes) {
            if (!is_fill_volume(v))
                continue;
            indexed_triangle_set part = v->mesh().its;
            const Transform3d    m    = v->get_matrix();
            if (!m.isApprox(Transform3d::Identity()))
                for (stl_vertex &p : part.vertices)
                    p = (m * p.cast<double>()).cast<float>();
            its_merge(applied, part);
        }
    }
    if (!applied.indices.empty()) {
        m_preview_applied.model.init_from(applied);
        m_preview_applied.model.set_color(APPLIED_COLOR);
    }
}

void GLGizmoHoleFill::add_named_fill(const std::string &name, const indexed_triangle_set &its)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    plater->take_snapshot(_u8L("Fill cavity"));
    indexed_triangle_set copy = its;
    ModelVolume         *v    = mo->add_volume(TriangleMesh(std::move(copy)), ModelVolumeType::MODEL_PART, false);
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

void GLGizmoHoleFill::set_radius(double radius)
{
    m_radius        = std::clamp(radius, FILL_RADIUS_MIN, FILL_RADIUS_MAX);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

bool GLGizmoHoleFill::gizmo_hover_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    rebuild_preview();
    m_parent.set_as_dirty();
    return m_hover.valid;
}

bool GLGizmoHoleFill::gizmo_apply_hovered()
{
    if (!m_hover.valid || m_hover_applied >= 0)
        return false;
    const indexed_triangle_set its = fill_mesh(m_hover);
    if (its.indices.empty())
        return false; // the patch is too flat: there is no cavity to fill
    add_named_fill(feature_name(HOLE_FILL_NAME, next_feature_id()), its);
    return true;
}

bool GLGizmoHoleFill::gizmo_apply_at(const Vec2d &screen_pos)
{
    if (!gizmo_hover_at(screen_pos))
        return false;
    return gizmo_apply_hovered();
}

bool GLGizmoHoleFill::gizmo_remove_hovered()
{
    if (!m_hover.valid || m_hover_applied < 0)
        return false;
    return gizmo_remove_fill(m_hover_applied);
}

bool GLGizmoHoleFill::gizmo_remove_fill(int index)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0 || index < 0 || index >= int(mo->volumes.size()) || !is_fill_volume(mo->volumes[index]))
        return false;

    std::vector<ItemForDelete> items;
    items.emplace_back(ItemType::itVolume, oi, index);
    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Remove cavity fill"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
    m_preview_dirty = true;
    m_hover_applied = -1;
    return true;
}

void GLGizmoHoleFill::gizmo_clear_all()
{
    ModelObject *mo = model_object();
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi)
        if (is_fill_volume(mo->volumes[vi]))
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Clear cavity fills"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
    plater->update();
    m_preview_dirty = true;
    m_hover_applied = -1;
}

void GLGizmoHoleFill::gizmo_refresh()
{
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

std::vector<std::array<double, 3>> GLGizmoHoleFill::gizmo_list_fills() const
{
    std::vector<std::array<double, 3>> out;
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return out;
    for (const ModelVolume *v : mo->volumes) {
        if (!is_fill_volume(v) || v->mesh().its.vertices.empty())
            continue;
        Vec3f c = Vec3f::Zero();
        for (const stl_vertex &p : v->mesh().its.vertices)
            c += p;
        c /= float(v->mesh().its.vertices.size());
        const Vec3d center = v->get_matrix() * c.cast<double>();
        out.push_back({ center.x(), center.y(), center.z() });
    }
    return out;
}

// ---------------------------------------------------------------------------------------------
// GLGizmoBase callbacks
// ---------------------------------------------------------------------------------------------

bool GLGizmoHoleFill::on_init()
{
    m_shortcut_key = WXK_CONTROL_G;

    m_desc["name"]        = _L("Cavity fill");
    m_desc["radius"]      = _L("Fill radius");
    m_desc["apply"]       = _L("Fill under cursor");
    m_desc["clear"]       = _L("Clear all");
    m_desc["hover_hint"]  = _L("Drag over a depression to fill it; a plain wall is left alone.");
    m_desc["no_cavity"]   = _L("Nothing to fill under the cursor.");
    m_desc["cavity"]      = _L("Cavity under the cursor.");
    m_desc["already"]     = _L("Already filled.");
    m_desc["remove_hint"] = _L("Right-click a filled cavity to remove it.");
    m_desc["applied"]     = _L("Filled");
    return true;
}

std::string GLGizmoHoleFill::on_get_name() const
{
    return _u8L("Cavity fill");
}

bool GLGizmoHoleFill::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoHoleFill::on_set_state()
{
    if (get_state() == On) {
        m_preview_dirty  = true;
        m_hover_computed = false;
        m_parent.set_as_dirty();
    } else {
        clear_hover();
    }
}

CommonGizmosDataID GLGizmoHoleFill::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoHoleFill::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_old_object     = nullptr;
        m_hover_computed = false;
        clear_hover();
        return;
    }

    if (mo != m_old_object || int(mo->volumes.size()) != m_old_volume_count || instance_matrix().matrix() != m_old_matrix.matrix()) {
        m_old_object       = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_old_matrix       = instance_matrix();
        m_preview_dirty    = true;
        m_hover_computed   = false;
        m_parent.set_as_dirty();
    }
}

void GLGizmoHoleFill::on_render()
{
    if (get_state() != On)
        return;

    // Track the cursor so the fill under it is highlighted before the click. The work is skipped
    // while neither the cursor nor the camera moves, which keeps a still scene cheap.
    const Camera     &camera = wxGetApp().plater()->get_camera();
    const Vec2d       mouse  = m_parent.get_local_mouse_position();
    const Transform3d view   = camera.get_view_matrix();
    if (!m_hover_computed || mouse.x() != m_last_mouse.x() || mouse.y() != m_last_mouse.y() ||
        !view.matrix().isApprox(m_last_view.matrix(), 1e-12)) {
        update_hover(mouse);
        m_last_mouse     = mouse;
        m_last_view      = view;
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

bool GLGizmoHoleFill::on_mouse(const wxMouseEvent &mouse_event)
{
    if (get_state() != On)
        return false;
    if (mouse_event.RightDown()) {
        // Right-click removes the plug the cursor is on, like the hole gizmo removes a hole.
        gizmo_hover_at(m_parent.get_local_mouse_position());
        return gizmo_remove_hovered();
    }
    if (mouse_event.LeftUp() && m_painting) {
        m_painting = false;
        commit_stroke();
        m_parent.set_as_dirty();
        m_parent.request_extra_frame();
        return true;
    }
    if (m_painting && (mouse_event.Dragging() || mouse_event.Moving())) {
        // Keep painting the cavity while the button is held; the stroke is committed on release.
        update_hover(m_parent.get_local_mouse_position());
        record_stroke_sample();
        m_parent.set_as_dirty();
        m_parent.request_extra_frame();
        return true;
    }
    if (!mouse_event.LeftDown())
        return false;
    // Start a stroke only on the object, so a click on empty space still falls through to the canvas.
    gizmo_hover_at(m_parent.get_local_mouse_position());
    if (!m_hover.valid)
        return false;
    m_painting = true;
    m_stroke.clear();
    record_stroke_sample();
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
    return true; // swallow the press while over the object
}

void GLGizmoHoleFill::on_render_input_window(float x, float y, float bottom_limit)
{
    if (model_object() == nullptr)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(6.0f);
    const float left_width    = m_imgui->calc_text_size(m_desc.at("radius")).x + m_imgui->scaled(2.0f);

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("radius"));
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    float radius = float(m_radius);
    if (m_imgui->bbl_slider_float_style("##fill_radius", &radius, float(FILL_RADIUS_MIN), float(FILL_RADIUS_MAX), "%.1f", 0.1f, true))
        set_radius(radius);
    ImGui::PopItemWidth();
    // A field next to the slider, so an exact radius can be typed instead of dragged.
    ImGui::SameLine(0.f, m_imgui->scaled(1.0f));
    ImGui::PushItemWidth(m_imgui->scaled(4.5f));
    float typed = float(m_radius);
    if (ImGui::InputFloat("##fill_radius_in", &typed, 0.1f, 1.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
        set_radius(typed);
    ImGui::PopItemWidth();

    if (m_imgui->button(m_desc.at("apply")))
        gizmo_apply_hovered();
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("clear")))
        gizmo_clear_all();

    if (m_hover.valid && m_hover_applied >= 0)
        m_imgui->text(m_desc.at("already"));
    else
        m_imgui->text(m_hover.valid ? m_desc.at("cavity") : m_desc.at("no_cavity"));
    m_imgui->text(m_desc.at("hover_hint"));
    if (applied_count() > 0)
        m_imgui->text(m_desc.at("remove_hint"));
    const wxString applied = wxString::Format("%s: %d", m_desc.at("applied").c_str(), applied_count());
    m_imgui->text(applied);

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoHoleFill::on_register_raycasters_for_picking()
{
    // Face picking uses raycast_object_face on the scene volumes, so no gizmo raycaster is added.
}

void GLGizmoHoleFill::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
}

} // namespace GUI
} // namespace Slic3r
