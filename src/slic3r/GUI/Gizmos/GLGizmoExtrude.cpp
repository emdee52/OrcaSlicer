#include "GLGizmoExtrude.hpp"

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/SurfaceExtrude.hpp"   // [ORCAPORT:HF-3] extrude-face geometry
#include "libslic3r/TriangleMesh.hpp"

#include <Eigen/Geometry>   // [ORCAPORT:HF-3] Quaterniond::FromTwoVectors

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
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

// [ORCAPORT:HF-3] New volumes carry the "Extrude#" prefix. The legacy "HoleFill#" prefix is still
// recognised on read, so fills created before the tool was renamed stay editable and removable.
constexpr const char *EXTRUDE_NAME        = "Extrude";
constexpr const char *LEGACY_EXTRUDE_NAME = "HoleFill";

const ColorRGBA HOVER_COLOR{ 0.10f, 1.00f, 0.20f, 0.90f };
// Applied plugs, the same red the hole gizmo uses for applied holes.
const ColorRGBA APPLIED_COLOR{ 1.00f, 0.15f, 0.15f, 0.80f };

// Brush reach, in world mm, that the panel offers. Small enough to stay inside one engraved mark,
// so the local surface the fill is measured against stays local to it.
constexpr double FILL_RADIUS_MIN = 0.5;
constexpr double FILL_RADIUS_MAX = 50.0;

// [ORCAPORT:HF-3] Extrude-face limits, in mm. Height must stay positive; the offset may be negative
// (it shrinks the outline) as long as the shape survives, which extrude_outline() checks.
constexpr double EXTRUDE_HEIGHT_MIN = 0.05;
constexpr double EXTRUDE_HEIGHT_MAX = 200.0;
constexpr double EXTRUDE_OFFSET_MIN = -20.0;
constexpr double EXTRUDE_OFFSET_MAX = 20.0;

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
    return v != nullptr && (parsed_feature_index(v->name, EXTRUDE_NAME) >= 0
                            || parsed_feature_index(v->name, LEGACY_EXTRUDE_NAME) >= 0);
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

GLGizmoExtrude::GLGizmoExtrude(GLCanvas3D &parent, const std::string &icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

// ---------------------------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------------------------

ModelObject *GLGizmoExtrude::model_object() const
{
    return m_c != nullptr && m_c->selection_info() != nullptr ? m_c->selection_info()->model_object() : nullptr;
}

Transform3d GLGizmoExtrude::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

double GLGizmoExtrude::object_scale() const
{
    const Transform3d im = instance_matrix();
    const double      s  = (im.linear().col(0).norm() + im.linear().col(1).norm() + im.linear().col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

double GLGizmoExtrude::volume_scale(const ModelVolume *mv) const
{
    if (mv == nullptr)
        return 1.0;
    const Eigen::Matrix3d L = mv->get_matrix().linear();
    const double          s = (L.col(0).norm() + L.col(1).norm() + L.col(2).norm()) / 3.;
    return std::isfinite(s) && s > 1e-9 ? s : 1.0;
}

int GLGizmoExtrude::applied_count() const
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

int GLGizmoExtrude::next_feature_id() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return 0;
    int next = 0;
    for (const ModelVolume *v : mo->volumes) {
        next = std::max(next, parsed_feature_index(v->name, EXTRUDE_NAME) + 1);
        // Count the legacy prefix too, so a new id never collides with an old fill's name.
        next = std::max(next, parsed_feature_index(v->name, LEGACY_EXTRUDE_NAME) + 1);
    }
    return next;
}

int GLGizmoExtrude::find_covering_fill(const Vec3d &hit_world) const
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

void GLGizmoExtrude::clear_hover()
{
    m_hover                = Hover();
    m_hover_applied        = -1;
    m_preview_dirty        = true;
    m_preview.reset();
}

void GLGizmoExtrude::update_hover(const Vec2d &screen_pos)
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
        if (raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit_world,
                                [](const ModelVolume *v) { return !is_fill_volume(v); })) {
            // The plugs are picked through: an applied plug covers the wall around it, and the user
            // must still be able to paint the recesses next to it.
            if (mv != nullptr && facet < mv->mesh().its.indices.size()) {
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

indexed_triangle_set GLGizmoExtrude::fill_mesh(const Hover &hover) const
{
    return fill_mesh(std::vector<Hover>{ hover });
}

indexed_triangle_set GLGizmoExtrude::fill_mesh(const std::vector<Hover> &seeds) const
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

    // Every painted facet is measured against the surface fitted to its own neighbourhood, so the
    // plug follows the wall it is engraved into: no reference surface to set, and painting a plain
    // wall finds nothing behind the surface and adds nothing.
    indexed_triangle_set plug = cavity_fill_local(mv->mesh().its, points, facets, r_local);
    if (plug.indices.empty())
        return plug;

    // the plug is tessellated in the volume's own space: bring it into object space, where it is
    // added and rendered.
    const Transform3d m = mv->get_matrix();
    if (!m.isApprox(Transform3d::Identity()))
        for (stl_vertex &p : plug.vertices)
            p = (m * p.cast<double>()).cast<float>();
    return plug;
}

// [ORCAPORT:HF-3] The prism for the picked face, in OBJECT space (the same space the cavity-fill
// plug comes back in, so add_named_fill treats both the same way). The face is grown into a coplanar
// region, projected to an outline in a frame where it points +Z, closed into a solid with the
// requested height and offset, and turned back. The rotation is pure, so an angled face extrudes
// exactly like a top face and nothing is distorted.
indexed_triangle_set GLGizmoExtrude::face_extrude_mesh(const Hover &hover, std::string *why) const
{
    auto fail = [&](const std::string &msg) -> indexed_triangle_set {
        if (why != nullptr)
            *why = msg;
        return {};
    };

    if (!hover.valid || hover.mv == nullptr || hover.facet < 0)
        return fail(_u8L("Hover a flat face of the object."));

    const indexed_triangle_set &its = hover.mv->mesh().its;
    if (hover.facet >= int(its.indices.size()))
        return fail(_u8L("That face is not usable."));

    const std::vector<Vec3f>   normals   = its_face_normals(its);
    const std::vector<Vec3i32> neighbors = its_face_neighbors(its);
    if (int(normals.size()) != int(its.indices.size()))
        return fail(_u8L("That mesh has no usable facet normals."));

    // The picked facet's normal in object space. Normals do not transform with the matrix itself,
    // so the inverse transpose of the volume's linear part is the right thing.
    const Transform3d to_obj  = hover.mv->get_matrix();
    const Matrix3d    nrm_mat = to_obj.linear().inverse().transpose();
    Vec3d             obj_n   = nrm_mat * normals[size_t(hover.facet)].cast<double>();
    const double      nl      = obj_n.norm();
    if (nl < 1e-12)
        return fail(_u8L("That face has no usable normal."));
    obj_n /= nl;

    const Eigen::Quaterniond q_flat = Eigen::Quaterniond::FromTwoVectors(obj_n, Vec3d::UnitZ());
    Transform3d              to_flat = Transform3d::Identity();
    to_flat.linear() = q_flat.toRotationMatrix();

    // ~2.6 deg: the region stops where the surface stops being that plane.
    constexpr float kPickedFaceCos = 0.999f;
    const std::vector<int> region = SurfaceExtrude::coplanar_region(normals, neighbors, hover.facet, kPickedFaceCos);
    double       face_z = 0.;
    ExPolygons   outline = SurfaceExtrude::region_outline(its, region, to_flat * to_obj, &face_z);
    if (outline.empty())
        return fail(_u8L("That face has no usable outline."));
    if (m_extrude_holes) {
        // Only the face's holes, each one filled - e.g. the circle of a screw, to grow into a plug.
        outline = SurfaceExtrude::outline_holes(outline);
        if (outline.empty())
            return fail(_u8L("That face has no holes."));
    }

    // Out of the face, or half out and half into the model.
    const double h  = m_extrude_height;
    const double z0 = m_extrude_both ? face_z - 0.5 * h : face_z;
    indexed_triangle_set out = SurfaceExtrude::extrude_outline(outline, m_extrude_offset, z0, h);
    if (out.indices.empty())
        return fail(_u8L("With that offset nothing is left of the shape."));
    its_transform(out, to_flat.inverse());
    return out;
}

void GLGizmoExtrude::record_stroke_sample()
{
    if (!m_hover.valid || m_hover.mv == nullptr)
        return;
    if (!m_stroke.empty() && m_stroke.front().mv != m_hover.mv)
        return; // a stroke covers a single volume
    if (m_hover_applied >= 0)
        return; // the recess under the cursor already carries a plug
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

void GLGizmoExtrude::commit_stroke()
{
    if (m_stroke.empty())
        return;
    const indexed_triangle_set its = fill_mesh(m_stroke);
    m_stroke.clear();
    if (its.indices.empty())
        return; // painted a plain wall: nothing to fill
    add_named_fill(feature_name(EXTRUDE_NAME, next_feature_id()), its, _u8L("Fill cavity"));
    m_hover_applied = -1;
    m_preview_dirty = true;
}

void GLGizmoExtrude::rebuild_preview()
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
    if (m_mode == Mode::ExtrudeFace) {
        // [ORCAPORT:HF-3] Extrude face: preview the prism that a click would create.
        if (m_hover.valid) {
            const indexed_triangle_set its = face_extrude_mesh(m_hover);
            if (!its.indices.empty()) {
                m_preview.model.init_from(its);
                m_preview.model.set_color(HOVER_COLOR);
            }
        }
    } else if (m_painting && !m_stroke.empty())
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

void GLGizmoExtrude::add_named_fill(const std::string &name, const indexed_triangle_set &its, const std::string &action)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    plater->take_snapshot(action);
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

void GLGizmoExtrude::set_radius(double radius)
{
    m_radius        = std::clamp(radius, FILL_RADIUS_MIN, FILL_RADIUS_MAX);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

// [ORCAPORT:HF-3] Mode and extrude-face parameters.
void GLGizmoExtrude::set_mode(Mode m)
{
    if (m_mode == m)
        return;
    m_mode          = m;
    m_painting      = false;
    m_stroke.clear();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoExtrude::set_extrude_height(double h)
{
    m_extrude_height = std::clamp(h, EXTRUDE_HEIGHT_MIN, EXTRUDE_HEIGHT_MAX);
    m_preview_dirty  = true;
    m_parent.set_as_dirty();
}

void GLGizmoExtrude::set_extrude_offset(double off)
{
    m_extrude_offset = std::clamp(off, EXTRUDE_OFFSET_MIN, EXTRUDE_OFFSET_MAX);
    m_preview_dirty  = true;
    m_parent.set_as_dirty();
}

void GLGizmoExtrude::set_extrude_holes(bool on)
{
    m_extrude_holes = on;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoExtrude::set_extrude_both(bool on)
{
    m_extrude_both  = on;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

bool GLGizmoExtrude::gizmo_hover_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    rebuild_preview();
    m_parent.set_as_dirty();
    return m_hover.valid;
}

bool GLGizmoExtrude::gizmo_apply_hovered()
{
    if (!m_hover.valid)
        return false;
    // [ORCAPORT:HF-3] Extrude face: a new solid every click - unlike a cavity plug, the same face may
    // legitimately be extruded more than once.
    if (m_mode == Mode::ExtrudeFace) {
        const indexed_triangle_set its = face_extrude_mesh(m_hover);
        if (its.indices.empty())
            return false;
        add_named_fill(feature_name(EXTRUDE_NAME, next_feature_id()), its, _u8L("Extrude face"));
        return true;
    }
    if (m_hover_applied >= 0)
        return false;
    const indexed_triangle_set its = fill_mesh(m_hover);
    if (its.indices.empty())
        return false; // the patch is too flat: there is no cavity to fill
    add_named_fill(feature_name(EXTRUDE_NAME, next_feature_id()), its, _u8L("Fill cavity"));
    return true;
}

bool GLGizmoExtrude::gizmo_apply_at(const Vec2d &screen_pos)
{
    if (!gizmo_hover_at(screen_pos))
        return false;
    return gizmo_apply_hovered();
}

bool GLGizmoExtrude::gizmo_remove_hovered()
{
    if (!m_hover.valid || m_hover_applied < 0)
        return false;
    return gizmo_remove_fill(m_hover_applied);
}

bool GLGizmoExtrude::gizmo_remove_fill(int index)
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

void GLGizmoExtrude::gizmo_clear_all()
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

void GLGizmoExtrude::gizmo_refresh()
{
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

std::vector<std::array<double, 3>> GLGizmoExtrude::gizmo_list_fills() const
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

bool GLGizmoExtrude::on_init()
{
    m_shortcut = Shortcut::GizmoExtrude; // [ORCAPORT:HF-3] registered in Shortcuts.cpp

    m_desc["name"]         = _L("Extrude");
    // [ORCAPORT:HF-3] The two modes.
    m_desc["mode_extrude"] = _L("Extrude face");
    m_desc["mode_cavity"]  = _L("Cavity fill");
    m_desc["height"]       = _L("Height");
    m_desc["offset"]       = _L("Offset");
    m_desc["holes"]        = _L("Holes");
    m_desc["both"]         = _L("Both directions");
    m_desc["create"]       = _L("Create");
    m_desc["extrude_hint"] = _L("Click a flat face of the object. Height grows the new solid out of it; a negative offset shrinks its outline.");
    m_desc["extrude_none"] = _L("No face under the cursor.");
    m_desc["radius"]       = _L("Brush width");
    m_desc["clear"]       = _L("Clear all");
    m_desc["hover_hint"]  = _L("Drag over an engraved mark to fill it.");
    m_desc["no_cavity"]   = _L("Nothing to fill under the cursor.");
    m_desc["cavity"]      = _L("Depression under the cursor.");
    m_desc["already"]     = _L("Already filled.");
    m_desc["remove_hint"] = _L("Right-click a filled cavity to remove it.");
    m_desc["applied"]     = _L("Filled");
    return true;
}

std::string GLGizmoExtrude::on_get_name() const
{
    return _u8L("Extrude");
}

bool GLGizmoExtrude::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

void GLGizmoExtrude::on_set_state()
{
    if (get_state() == On) {
        m_preview_dirty  = true;
        m_hover_computed = false;
        m_parent.set_as_dirty();
    } else {
        clear_hover();
    }
}

CommonGizmosDataID GLGizmoExtrude::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoExtrude::data_changed(bool /*is_serializing*/)
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

void GLGizmoExtrude::on_render()
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

bool GLGizmoExtrude::on_mouse(const wxMouseEvent &mouse_event)
{
    if (get_state() != On)
        return false;
    if (mouse_event.RightDown()) {
        // Right-click removes the plug the cursor is on, like the hole gizmo removes a hole.
        gizmo_hover_at(m_parent.get_local_mouse_position());
        return gizmo_remove_hovered();
    }
    // [ORCAPORT:HF-3] Extrude face is a click, not a stroke: one left click makes one solid. The
    // panel's Create button runs the same path, for anyone who would rather not click the model.
    if (m_mode == Mode::ExtrudeFace) {
        if (! mouse_event.LeftUp())
            return false;
        gizmo_hover_at(m_parent.get_local_mouse_position());
        if (! m_hover.valid)
            return false;   // a click on empty space falls through to the canvas
        return gizmo_apply_hovered();
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

void GLGizmoExtrude::on_render_input_window(float x, float y, float bottom_limit)
{
    if (model_object() == nullptr)
        return;

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    // [ORCAPORT:UI-1] Left-aligned under the icon while the panel fits to its right. Once it would run
    // past the canvas edge, hang it to the left instead of letting GizmoImguiSetNextWIndowPos()'s
    // left-aligned clamp slide it along that edge: the panel's right edge then stays under its own
    // icon. Width 0 skips the clamp, which only makes sense for a left edge.
#if BBS_TOOLBAR_ON_TOP
    float right = x + m_parent.get_gizmos_manager().get_scaled_icons_size();
    if (last_input_window_width > 0.f && x + last_input_window_width > m_parent.get_canvas_size().get_width())
        GizmoImguiSetNextWIndowPos(right, y, 0.f, 0.f, ImGuiCond_Always, 1.0f, 0.0f);
    else
        GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(6.0f);

    // [ORCAPORT:HF-3] Mode selector: the one icon does two jobs.
    ImGui::AlignTextToFramePadding();
    if (m_imgui->radio_button(m_desc.at("mode_extrude"), m_mode == Mode::ExtrudeFace))
        set_mode(Mode::ExtrudeFace);
    ImGui::SameLine(0.f, m_imgui->scaled(1.5f));
    if (m_imgui->radio_button(m_desc.at("mode_cavity"), m_mode == Mode::CavityFill))
        set_mode(Mode::CavityFill);

    if (m_mode == Mode::ExtrudeFace) {
        const float left_width = m_imgui->calc_text_size(m_desc.at("height")).x + m_imgui->scaled(2.0f);

        // Height: how thick the new solid is, grown out of the face (or half in, with Both).
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("height"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        float h = float(m_extrude_height);
        if (m_imgui->bbl_slider_float_style("##extrude_h", &h, float(EXTRUDE_HEIGHT_MIN), float(EXTRUDE_HEIGHT_MAX), "%.2f", 0.1f, true))
            set_extrude_height(h);
        ImGui::PopItemWidth();
        ImGui::SameLine(0.f, m_imgui->scaled(1.0f));
        ImGui::PushItemWidth(m_imgui->scaled(5.5f));
        float h_typed = float(m_extrude_height);
        if (ImGui::InputFloat("##extrude_h_in", &h_typed, 0.1f, 1.0f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            set_extrude_height(h_typed);
        ImGui::PopItemWidth();

        // Offset: grow (positive) or shrink (negative) the outline of the face before extruding.
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("offset"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        float off = float(m_extrude_offset);
        if (m_imgui->bbl_slider_float_style("##extrude_off", &off, float(EXTRUDE_OFFSET_MIN), float(EXTRUDE_OFFSET_MAX), "%.2f", 0.05f, true))
            set_extrude_offset(off);
        ImGui::PopItemWidth();
        ImGui::SameLine(0.f, m_imgui->scaled(1.0f));
        ImGui::PushItemWidth(m_imgui->scaled(5.5f));
        float off_typed = float(m_extrude_offset);
        if (ImGui::InputFloat("##extrude_off_in", &off_typed, 0.05f, 0.5f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            set_extrude_offset(off_typed);
        ImGui::PopItemWidth();

        bool holes = m_extrude_holes;
        if (m_imgui->checkbox(m_desc.at("holes"), holes))
            set_extrude_holes(holes);
        bool both = m_extrude_both;
        if (m_imgui->checkbox(m_desc.at("both"), both))
            set_extrude_both(both);

        // Why the face under the cursor cannot be extruded, if it cannot.
        if (m_hover.valid) {
            std::string why;
            if (face_extrude_mesh(m_hover, &why).indices.empty() && ! why.empty())
                m_imgui->text(wxString::FromUTF8(why.c_str()));
        }
        if (m_imgui->button(m_desc.at("create")))
            gizmo_apply_hovered();
        m_imgui->text(m_desc.at("extrude_hint"));
        if (! m_hover.valid)
            m_imgui->text(m_desc.at("extrude_none"));
    } else {
        const float left_width = m_imgui->calc_text_size(m_desc.at("radius")).x + m_imgui->scaled(2.0f);

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("radius"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        float radius = float(m_radius);
        if (m_imgui->bbl_slider_float_style("##fill_radius", &radius, float(FILL_RADIUS_MIN), float(FILL_RADIUS_MAX), "%.1f", 0.1f, true))
            set_radius(radius);
        ImGui::PopItemWidth();
        // A field next to the slider, so an exact radius can be typed instead of dragged. ImGui carves the
        // step buttons out of the item width, so it needs the room the hole tool's typed fields have;
        // scaled(4.5) clipped the value.
        ImGui::SameLine(0.f, m_imgui->scaled(1.0f));
        ImGui::PushItemWidth(m_imgui->scaled(5.5f));
        float typed = float(m_radius);
        if (ImGui::InputFloat("##fill_radius_in", &typed, 0.1f, 1.0f, "%.1f", ImGuiInputTextFlags_EnterReturnsTrue))
            set_radius(typed);
        ImGui::PopItemWidth();

        // Left-dragging over a cavity already fills it (on_mouse), so there is no separate fill button.
        if (m_imgui->button(m_desc.at("clear")))
            gizmo_clear_all();

        if (m_hover.valid && m_hover_applied >= 0)
            m_imgui->text(m_desc.at("already"));
        else if (m_hover.valid)
            m_imgui->text(m_desc.at("cavity"));
        else
            m_imgui->text(m_desc.at("no_cavity"));
        m_imgui->text(m_desc.at("hover_hint"));
        if (applied_count() > 0)
            m_imgui->text(m_desc.at("remove_hint"));
    }
    const wxString applied = wxString::Format("%s: %d", m_desc.at("applied").c_str(), applied_count());
    m_imgui->text(applied);

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

void GLGizmoExtrude::on_register_raycasters_for_picking()
{
    // Face picking uses raycast_object_face on the scene volumes, so no gizmo raycaster is added.
}

void GLGizmoExtrude::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
}

} // namespace GUI
} // namespace Slic3r
