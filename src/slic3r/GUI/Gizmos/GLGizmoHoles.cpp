#include "GLGizmoHoles.hpp"
#include "GLGizmoUtils.hpp"

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Utils.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/ObjectDataViewModel.hpp"
#include "slic3r/GUI/Plater.hpp"
#include "slic3r/Utils/UndoRedo.hpp"

#include <glad/gl.h>

#include <wx/utils.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char *TEARDROP_NAME = "Teardrop";
constexpr const char *POCKET_NAME   = "HolePocket";
// |axis . up| below this marks a hole as horizontal (the top of the wall is an overhang).
constexpr double       HORIZONTAL_COS = 0.5;
constexpr float        ANGLE_MIN = 45.f;
constexpr float        ANGLE_MAX = 60.f;
const ColorRGBA        ALL_COLOR{0.25f, 0.70f, 1.00f, 0.40f};      // candidate
const ColorRGBA        APPLIED_COLOR{1.00f, 0.15f, 0.15f, 0.80f};   // applied
const ColorRGBA        HOVER_COLOR{0.10f, 1.00f, 0.20f, 0.90f};     // hover

void merge_into(indexed_triangle_set &dst, indexed_triangle_set &src)
{
    if (src.indices.empty())
        return;
    if (dst.indices.empty())
        dst = std::move(src);
    else
        its_merge(dst, src);
}

// Applied features are named "<prefix>#<hole index>". The index identifies the hole even for
// coaxial holes (a counterbore and its shaft), where a geometric match is ambiguous.
std::string feature_name(const char *prefix, int idx)
{
    return std::string(prefix) + "#" + std::to_string(idx);
}

int parsed_hole_index(const std::string &name, const char *prefix)
{
    const size_t n = std::strlen(prefix);
    if (name.compare(0, n, prefix) != 0 || name.size() <= n || name[n] != '#')
        return -1;
    try {
        return std::stoi(name.substr(n + 1));
    } catch (...) {
        return -1;
    }
}

bool category_matches(HoleCategory cat, HoleStandardKind kind)
{
    switch (cat) {
    case HoleCategory::Screw:  return kind == HoleStandardKind::Screw;
    case HoleCategory::Nut:    return kind == HoleStandardKind::Nut;
    case HoleCategory::Magnet: return kind == HoleStandardKind::Magnet;
    case HoleCategory::Insert: return kind == HoleStandardKind::Insert;
    case HoleCategory::Custom: return false;
    }
    return false;
}

// Category button icon, loaded once from resources/images.
ImTextureID category_icon(HoleCategory cat)
{
    static std::map<int, ImTextureID> cache;
    auto                              it = cache.find(int(cat));
    if (it != cache.end())
        return it->second;

    const char *file = nullptr;
    switch (cat) {
    case HoleCategory::Screw:  file = "hole_cat_screw.svg"; break;
    case HoleCategory::Nut:    file = "hole_cat_nut.svg"; break;
    case HoleCategory::Magnet: file = "hole_cat_magnet.svg"; break;
    case HoleCategory::Insert: file = "hole_cat_insert.svg"; break;
    case HoleCategory::Custom: file = "hole_cat_custom.svg"; break;
    }
    ImTextureID tex = nullptr;
    if (file != nullptr)
        IMTexture::load_from_svg_file(Slic3r::resources_dir() + "/images/" + file, 40, 40, tex);
    cache[int(cat)] = tex;
    return tex;
}

wxString category_label(HoleCategory cat)
{
    switch (cat) {
    case HoleCategory::Screw:  return _L("Screw");
    case HoleCategory::Nut:    return _L("Nut");
    case HoleCategory::Magnet: return _L("Magnet");
    case HoleCategory::Insert: return _L("Insert");
    case HoleCategory::Custom: return _L("Custom");
    }
    return {};
}

// Right-handed frame so the pick-cylinder face normals point outward and SceneRaycaster accepts
// the hit.
indexed_triangle_set make_pick_cylinder(const DetectedHole &h, const Vec3d &up)
{
    const double         len = std::max(h.depth, 1.0);
    indexed_triangle_set its = its_make_cylinder(h.radius * 1.25, len, PI / 12.);

    const Vec3d a = h.axis.normalized();
    Vec3d       u = up - a * a.dot(up);
    u = (u.norm() < 1e-9) ? Vec3d::UnitY() : u.normalized();
    const Vec3d r = u.cross(a).normalized();

    Eigen::Matrix3d R;
    R.col(0) = Eigen::Vector3d(r(0), r(1), r(2));
    R.col(1) = Eigen::Vector3d(u(0), u(1), u(2));
    R.col(2) = Eigen::Vector3d(a(0), a(1), a(2));

    Transform3d tr = Transform3d::Identity();
    tr.linear()    = R;
    tr.translation() = h.center - R * Eigen::Vector3d(0., 0., 0.5 * len);
    for (stl_vertex &v : its.vertices)
        v = (tr * Eigen::Vector3d(v(0), v(1), v(2))).cast<float>();
    return its;
}

} // namespace

GLGizmoHoles::GLGizmoHoles(GLCanvas3D &parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

bool GLGizmoHoles::on_init()
{
    m_shortcut_key = WXK_CONTROL_J;

    m_desc["operation"]        = _L("Operation");
    m_desc["op_teardrop"]      = _L("Teardrop");
    m_desc["op_bore"]          = _L("Bore / pocket");
    m_desc["apex"]             = _L("Apex angle");
    m_desc["standard"]         = _L("Standard");
    m_desc["head"]             = _L("Head");
    m_desc["head_none"]        = _L("None");
    m_desc["head_socket"]      = _L("Socket");
    m_desc["head_button"]      = _L("Button");
    m_desc["head_csink"]       = _L("Sink");
    m_desc["fit"]              = _L("Fit");
    m_desc["fit_tight"]        = _L("Tight");
    m_desc["fit_slip"]         = _L("Slip");
    m_desc["screw_fit"]        = _L("Fit");
    m_desc["fit_free"]         = _L("Free");
    m_desc["fit_tap"]          = _L("Tap");
    m_desc["true_dia"]         = _L("True diameter");
    m_desc["head_fit"]         = _L("Head fit");
    m_desc["fit_flush"]        = _L("Flush");
    m_desc["sink_008"]         = _L("-0.08");
    m_desc["sink_016"]         = _L("-0.16");
    m_desc["diameter"]         = _L("Diameter");
    m_desc["across_flats"]     = _L("Across flats");
    m_desc["tolerance"]        = _L("Tolerance");
    m_desc["through"]          = _L("Through");
    m_desc["depth"]            = _L("Depth");
    m_desc["height"]           = _L("Height");
    m_desc["entry"]            = _L("Flip");
    m_desc["holes"]            = _L("Detected holes");
    m_desc["all"]              = _L("Apply all");
    m_desc["clear"]            = _L("Clear");
    m_desc["clipping_of_view"] = _L("Section view");
    m_desc["reset_direction"]  = _L("Reset direction");

    // Preload the category button icons here (the GL context is current), never inside the
    // ImGui render pass where a texture upload can trip glsafe.
    for (HoleCategory c : {HoleCategory::Screw, HoleCategory::Nut, HoleCategory::Magnet, HoleCategory::Insert,
                           HoleCategory::Custom})
        category_icon(c);
    return true;
}

std::string GLGizmoHoles::on_get_name() const { return _u8L("Holes"); }

ModelObject *GLGizmoHoles::model_object() const
{
    if (m_c == nullptr || m_c->selection_info() == nullptr)
        return nullptr;
    return m_c->selection_info()->model_object();
}

int GLGizmoHoles::object_idx() const
{
    const int sel = m_parent.get_selection().get_object_idx();
    if (sel >= 0)
        return sel;
    // Fall back to locating the selected object, in case the selection lost its index.
    const ModelObject *mo     = model_object();
    Plater            *plater = wxGetApp().plater();
    if (mo == nullptr || plater == nullptr)
        return -1;
    const Model &model = plater->model();
    for (size_t i = 0; i < model.objects.size(); ++i)
        if (model.objects[i] == mo)
            return int(i);
    return -1;
}

Transform3d GLGizmoHoles::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

Vec3d GLGizmoHoles::object_up() const
{
    Vec3d up = instance_matrix().linear().inverse() * Vec3d::UnitZ();
    if (!up.allFinite() || up.norm() < 1e-9)
        return Vec3d::UnitZ();
    return up.normalized();
}

double GLGizmoHoles::teardrop_depth(const DetectedHole &hole) const
{
    const double margin = hole.through ? std::max(0.5, 0.25 * hole.radius) : 0.0;
    return hole.depth + 2.0 * margin;
}

int GLGizmoHoles::standard_count() const { return 1 + int(hole_standards().size()); }

std::string GLGizmoHoles::standard_name(int idx) const
{
    if (idx <= 0)
        return "Custom";
    const int i = idx - 1;
    if (i >= int(hole_standards().size()))
        return "Custom";
    return hole_standards()[i].designation;
}

double GLGizmoHoles::bore_diameter() const
{
    const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                ? &hole_standards()[m_standard]
                                : nullptr;
    // Insert / magnet pockets derive their tolerance from the fit; screws, nuts and custom use
    // the editable tolerance field.
    const bool fitted = s != nullptr && (s->kind == HoleStandardKind::Insert || s->kind == HoleStandardKind::Magnet);
    const double tol = fitted ? hole_fit_diameter_delta(s->kind, m_diameter, HoleFit(m_fit)) : m_tolerance;
    return std::max(0.1, m_diameter + tol);
}

void GLGizmoHoles::feature_frame(int idx, Vec3d &dir, Vec3d &entry) const
{
    const DetectedHole &h = m_holes[idx].hole;
    const Vec3d         a = h.axis.normalized();
    dir   = m_flip ? -a : a;
    entry = h.center - dir * (0.5 * h.depth);
}

indexed_triangle_set GLGizmoHoles::teardrop_mesh(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    const DetectedHole &h = m_holes[idx].hole;
    return its_make_teardrop_for_hole(h, teardrop_depth(h), m_angle_deg, HOLE_SHAPE_SEGMENTS, object_up());
}

indexed_triangle_set GLGizmoHoles::bore_negative_mesh(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    const DetectedHole &h = m_holes[idx].hole;

    Vec3d dir, entry;
    feature_frame(idx, dir, entry);

    const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                ? &hole_standards()[m_standard]
                                : nullptr;
    const bool is_nut    = s != nullptr && s->kind == HoleStandardKind::Nut;
    const bool is_pocket = s != nullptr && (s->kind == HoleStandardKind::Insert || s->kind == HoleStandardKind::Magnet);

    const double margin = std::max(0.5, 0.25 * h.radius);
    double       depth  = 0.;
    if (is_pocket || is_nut)
        depth = std::max(0.1, m_depth); // editable pocket depth
    else if (m_through)
        depth = h.depth + 2.0 * margin;
    else
        depth = std::max(0.1, m_depth);

    const double sink = std::max(0.0, -m_head_fit); // depth the head/pocket is sunk below the surface

    if (is_nut) {
        const double across = std::max(0.1, m_diameter + m_tolerance);
        return its_make_nut_pocket(across, depth + sink, s->clearance_d, depth, dir, entry);
    }

    const double d = bore_diameter();
    if (d <= 0. || depth <= 0.)
        return {};

    if (s != nullptr && s->kind == HoleStandardKind::Screw) {
        if (m_head == BoreHead::SocketHead && s->socket_d > d)
            return its_make_counterbore(d, s->socket_d, s->socket_k + sink, depth, dir, entry);
        if (m_head == BoreHead::ButtonHead && s->button_d > d)
            return its_make_counterbore(d, s->button_d, s->button_k + sink, depth, dir, entry);
        // A sunk countersunk head only deepens the cone's top: the entry stays at the surface and
        // the cone grows by 2*sink in diameter at the same angle, so the head sits `sink` lower.
        if (m_head == BoreHead::Countersink && s->csink_d > d)
            return its_make_countersink(d, s->csink_d + 2.0 * sink, s->csink_angle, depth, dir, entry);
    }
    if (s != nullptr && s->kind == HoleStandardKind::Magnet)
        depth += sink; // sink the magnet pocket
    return its_make_bore(d, depth, dir, entry);
}

indexed_triangle_set GLGizmoHoles::bore_tube_mesh(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    const DetectedHole &h = m_holes[idx].hole;

    const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                ? &hole_standards()[m_standard]
                                : nullptr;
    if (s != nullptr && s->kind == HoleStandardKind::Nut)
        return {}; // a hex pocket has no round shrink tube

    const double target_d = bore_diameter();
    const double exist_d  = 2.0 * h.radius;
    if (target_d <= 0. || target_d >= exist_d - 0.01)
        return {}; // enlarging or matching: no fill needed

    Vec3d dir, entry;
    feature_frame(idx, dir, entry);
    const double outer_d = exist_d + 0.4; // overlap the existing wall
    const double depth   = h.depth + std::max(0.4, 0.2 * h.depth);
    return its_make_tube(outer_d, target_d, depth, dir, entry);
}

indexed_triangle_set GLGizmoHoles::shape_mesh(int idx) const
{
    return m_operation == HoleOperation::Teardrop ? teardrop_mesh(idx) : bore_negative_mesh(idx);
}

bool GLGizmoHoles::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

CommonGizmosDataID GLGizmoHoles::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoHoles::on_set_state()
{
    if (get_state() == On) {
        m_dirty         = true;
        m_preview_dirty = true;
        m_parent.set_as_dirty();
    } else {
        m_preview_all.reset();
        m_preview_applied.reset();
        m_preview_hover.reset();
    }
}

void GLGizmoHoles::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_holes.clear();
        m_teardrop.clear();
        m_bore.clear();
        m_pick_its.clear();
        m_preview_all.reset();
        m_preview_applied.reset();
        m_preview_hover.reset();
        m_dirty = false;
        return;
    }

    const int         n  = int(mo->volumes.size());
    const Transform3d im = instance_matrix();
    if (mo != m_old_model_object || n != m_old_volume_count || !im.isApprox(m_old_instance_matrix)) {
        m_dirty = true;
        m_parent.set_as_dirty();
    }
}

void GLGizmoHoles::on_set_hover_id()
{
    if (m_hover_id < -1 || m_hover_id >= int(m_holes.size()))
        m_hover_id = -1;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::on_register_raycasters_for_picking() { register_pickers(); }

void GLGizmoHoles::on_unregister_raycasters_for_picking()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_parent.set_raycaster_gizmos_on_top(false);
    m_pick_items.clear();
    m_pick_raycasters.clear();
}

void GLGizmoHoles::register_pickers()
{
    m_parent.remove_raycasters_for_picking(SceneRaycaster::EType::Gizmo);
    m_pick_items.clear();
    m_pick_raycasters.clear();
    if (get_state() != On || m_pick_its.empty())
        return;

    m_parent.set_raycaster_gizmos_on_top(true);
    const Transform3d trafo = instance_matrix();
    for (size_t i = 0; i < m_pick_its.size(); ++i) {
        auto raycaster = std::make_unique<MeshRaycaster>(std::make_shared<const TriangleMesh>(m_pick_its[i]));
        m_pick_items.emplace_back(m_parent.add_raycaster_for_picking(SceneRaycaster::EType::Gizmo, int(i), *raycaster, trafo));
        m_pick_raycasters.emplace_back(std::move(raycaster));
    }
}

void GLGizmoHoles::detect()
{
    m_dirty = false;
    m_holes.clear();
    m_teardrop.clear();
    m_bore.clear();
    m_pick_its.clear();
    m_preview_all.reset();
    m_preview_applied.reset();
    m_preview_hover.reset();
    m_old_model_object = nullptr;
    m_old_volume_count = -1;

    ModelObject *mo = model_object();
    if (mo == nullptr) {
        register_pickers();
        m_preview_dirty = true;
        return;
    }

    wxBusyCursor wait;
    const Vec3d  up = object_up();

    // Detect on the object's own parts only: the positive shrink tube we add carries its own
    // cylindrical walls, which would otherwise appear as extra/candidate holes and shift indices.
    TriangleMesh mesh;
    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr || !v->is_model_part() || v->name.rfind(POCKET_NAME, 0) == 0)
            continue;
        TriangleMesh part = v->mesh();
        part.transform(v->get_matrix());
        mesh.merge(part);
    }
    std::vector<DetectedHole> holes = detect_holes(mesh.its);

    for (DetectedHole &h : holes) {
        HoleView v;
        v.horizontal = std::abs(h.axis.dot(up)) < HORIZONTAL_COS;
        v.hole       = std::move(h);
        m_holes.push_back(std::move(v));
    }

    m_pick_its.reserve(m_holes.size());
    for (const HoleView &v : m_holes)
        m_pick_its.push_back(make_pick_cylinder(v.hole, up));

    refresh_applied();
    m_old_model_object    = mo;
    m_old_volume_count    = int(mo->volumes.size());
    m_old_instance_matrix = instance_matrix();

    register_pickers();
    m_preview_dirty = true;
}

void GLGizmoHoles::refresh_applied()
{
    m_teardrop.assign(m_holes.size(), 0);
    m_bore.assign(m_holes.size(), 0);
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    for (const ModelVolume *v : mo->volumes) {
        if (v == nullptr)
            continue;
        if (v->is_negative_volume()) {
            const int i = parsed_hole_index(v->name, TEARDROP_NAME);
            if (i >= 0 && i < int(m_holes.size()))
                m_teardrop[i] = 1;
        }
        const int i = parsed_hole_index(v->name, POCKET_NAME);
        if (i >= 0 && i < int(m_holes.size()))
            m_bore[i] = 1;
    }
}

void GLGizmoHoles::rebuild_previews()
{
    m_preview_dirty = false;
    m_preview_all.reset();
    m_preview_applied.reset();
    m_preview_hover.reset();

    indexed_triangle_set all_its, applied_its, hover_its;

    // Applied features are shown from the actual model volumes, so the red preview always matches
    // what was baked in when each hole was placed (per-hole parameter capture).
    const ModelObject *mo = model_object();
    if (mo != nullptr) {
        for (const ModelVolume *v : mo->volumes) {
            if (v == nullptr || !v->is_negative_volume())
                continue;
            if (parsed_hole_index(v->name, TEARDROP_NAME) < 0 && parsed_hole_index(v->name, POCKET_NAME) < 0)
                continue;
            indexed_triangle_set its = v->mesh().its;
            const Transform3d    m   = v->get_matrix();
            if (!m.isApprox(Transform3d::Identity()))
                for (stl_vertex &p : its.vertices)
                    p = (m * Eigen::Vector3d(p(0), p(1), p(2))).cast<float>();
            merge_into(applied_its, its);
        }
    }

    for (size_t i = 0; i < m_holes.size(); ++i) {
        const bool applied = m_operation == HoleOperation::Teardrop ? m_teardrop[i] : m_bore[i];
        if (applied)
            continue; // already drawn from the actual volume
        indexed_triangle_set ghost = shape_mesh(int(i));
        if (ghost.indices.empty())
            continue;
        merge_into(all_its, ghost);
        if (int(i) == m_hover_id) {
            indexed_triangle_set s = shape_mesh(int(i));
            merge_into(hover_its, s);
        }
    }

    if (!all_its.indices.empty()) {
        m_preview_all.model.init_from(all_its);
        m_preview_all.model.set_color(ALL_COLOR);
    }
    if (!applied_its.indices.empty()) {
        m_preview_applied.model.init_from(applied_its);
        m_preview_applied.model.set_color(APPLIED_COLOR);
    }
    if (!hover_its.indices.empty()) {
        m_preview_hover.model.init_from(hover_its);
        m_preview_hover.model.set_color(HOVER_COLOR);
    }
}

void GLGizmoHoles::on_render()
{
    if (m_dirty)
        detect();
    if (m_preview_dirty)
        rebuild_previews();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    shader->start_using();
    // The previews sit inside the hole: force the depth mask before clearing (glClear honours
    // it) and draw without depth testing so they are never occluded by the solid.
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));

    const Camera   &camera = wxGetApp().plater()->get_camera();
    const Transform3d view_model_matrix = camera.get_view_matrix() * instance_matrix();
    shader->set_uniform("view_model_matrix", view_model_matrix);
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    m_preview_all.model.render(shader);
    m_preview_applied.model.render(shader);
    m_preview_hover.model.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

// ---------------------------------------------------------------------------------------------
// Model edits
// ---------------------------------------------------------------------------------------------

void GLGizmoHoles::add_named_volume(int idx, const indexed_triangle_set &its, ModelVolumeType type, const std::string &name, bool snapshot)
{
    (void) idx;
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    if (snapshot)
        plater->take_snapshot(_u8L("Apply hole feature"));
    indexed_triangle_set copy = its;
    ModelVolume        *v    = mo->add_volume(TriangleMesh(std::move(copy)), type, false);
    v->name                  = name;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

void GLGizmoHoles::remove_named_volumes(int idx, const char *prefix, const std::string &snapshot_name)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0 || idx < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi)
        if (parsed_hole_index(mo->volumes[vi]->name, prefix) == idx)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, snapshot_name, UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
}

void GLGizmoHoles::toggle_teardrop(int idx)
{
    if (m_teardrop[idx])
        remove_named_volumes(idx, TEARDROP_NAME, _u8L("Remove teardrop"));
    else
        add_named_volume(idx, teardrop_mesh(idx), ModelVolumeType::NEGATIVE_VOLUME, feature_name(TEARDROP_NAME, idx), true);
}

void GLGizmoHoles::toggle_bore(int idx)
{
    if (m_bore[idx]) {
        remove_named_volumes(idx, POCKET_NAME, _u8L("Remove pocket"));
        return;
    }
    const std::string name = feature_name(POCKET_NAME, idx);
    // Shrink first (positive tube), then the negative bore; the negative clips the tube too.
    indexed_triangle_set tube = bore_tube_mesh(idx);
    if (!tube.empty())
        add_named_volume(idx, tube, ModelVolumeType::MODEL_PART, name, true);
    indexed_triangle_set neg = bore_negative_mesh(idx);
    if (!neg.empty())
        add_named_volume(idx, neg, ModelVolumeType::NEGATIVE_VOLUME, name, tube.empty());
}

void GLGizmoHoles::clear_all()
{
    ModelObject *mo = model_object();
    const int    oi = object_idx();
    if (mo == nullptr || oi < 0)
        return;

    std::vector<ItemForDelete> items;
    for (size_t vi = 0; vi < mo->volumes.size(); ++vi) {
        const ModelVolume *v = mo->volumes[vi];
        if ((v->is_negative_volume() && v->name.rfind(TEARDROP_NAME, 0) == 0) || v->name.rfind(POCKET_NAME, 0) == 0)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    if (items.empty())
        return;

    Plater *plater = wxGetApp().plater();
    Plater::TakeSnapshot snapshot(plater, _u8L("Clear hole features"), UndoRedo::SnapshotType::GizmoAction);
    if (ObjectList *ol = wxGetApp().obj_list())
        ol->delete_from_model_and_list(items);
}

// ---------------------------------------------------------------------------------------------
// MCP control surface
// ---------------------------------------------------------------------------------------------

int GLGizmoHoles::hole_count()
{
    if (m_dirty)
        detect();
    return int(m_holes.size());
}

DetectedHole GLGizmoHoles::hole(int idx) const
{
    if (idx < 0 || idx >= int(m_holes.size()))
        return {};
    return m_holes[idx].hole;
}

bool GLGizmoHoles::hole_horizontal(int idx) const { return idx >= 0 && idx < int(m_holes.size()) && m_holes[idx].horizontal; }
bool GLGizmoHoles::hole_has_teardrop(int idx) const { return idx >= 0 && idx < int(m_teardrop.size()) && m_teardrop[idx] != 0; }
bool GLGizmoHoles::hole_has_bore(int idx) const { return idx >= 0 && idx < int(m_bore.size()) && m_bore[idx] != 0; }

void GLGizmoHoles::set_operation(HoleOperation op)
{
    if (m_operation == op)
        return;
    m_operation     = op;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_angle(float deg)
{
    m_angle_deg     = std::clamp(deg, ANGLE_MIN, ANGLE_MAX);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_standard(int idx)
{
    m_standard = std::clamp(idx, 0, standard_count() - 1) - 1;
    if (m_standard >= 0) {
        const HoleStandard &s = hole_standards()[m_standard];
        if (s.kind == HoleStandardKind::Screw) {
            if (m_screw_fit == ScrewFit::Tap && s.tap_d <= 0.)
                m_screw_fit = ScrewFit::Free;
            if (m_screw_fit == ScrewFit::Free && s.clearance_d <= 0.)
                m_screw_fit = ScrewFit::Tap; // tap-only size
            m_diameter = screw_nominal_diameter(s, m_screw_fit == ScrewFit::Tap);
            // Drop a head style this screw has no dimensions for.
            if ((m_head == BoreHead::SocketHead && s.socket_d <= 0.) ||
                (m_head == BoreHead::ButtonHead && s.button_d <= 0.) ||
                (m_head == BoreHead::Countersink && s.csink_d <= 0.))
                m_head = BoreHead::None;
        } else if (s.kind == HoleStandardKind::Nut) {
            m_diameter = s.across_flats;
            m_depth    = s.pocket_depth;
        } else {
            m_diameter = s.pocket_d;
            m_depth    = s.pocket_depth;
        }
    }
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_head(BoreHead h)
{
    m_head          = h;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_screw_fit(ScrewFit f)
{
    m_screw_fit = f;
    if (m_standard >= 0 && m_standard < int(hole_standards().size())) {
        const HoleStandard &s = hole_standards()[m_standard];
        if (s.kind == HoleStandardKind::Screw)
            m_diameter = screw_nominal_diameter(s, f == ScrewFit::Tap);
    }
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_fit(HoleFit f)
{
    m_fit           = int(f);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_category(HoleCategory c)
{
    m_category = c;
    if (c == HoleCategory::Custom) {
        m_standard = -1;
    } else {
        const std::vector<HoleStandard> &t = hole_standards();
        for (size_t i = 0; i < t.size(); ++i)
            if (category_matches(c, t[i].kind)) {
                set_standard(int(i) + 1);
                break;
            }
    }
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_diameter(double d)
{
    m_diameter = std::max(0.1, d);
    // Relabel Custom <-> a standard only within the current category, so editing a screw's size
    // to a magnet's diameter does not switch the screw to a magnet.
    m_standard = -1;
    const std::vector<HoleStandard> &t = hole_standards();
    for (size_t i = 0; i < t.size(); ++i) {
        if (!category_matches(m_category, t[i].kind))
            continue;
        if (t[i].kind == HoleStandardKind::Screw) {
            if (t[i].clearance_d > 0. && std::abs(t[i].clearance_d - m_diameter) <= 0.01) {
                m_standard  = int(i);
                m_screw_fit = ScrewFit::Free;
                break;
            }
            if (t[i].tap_d > 0. && std::abs(t[i].tap_d - m_diameter) <= 0.01) {
                m_standard  = int(i);
                m_screw_fit = ScrewFit::Tap;
                break;
            }
        } else if (t[i].kind == HoleStandardKind::Nut) {
            if (t[i].across_flats > 0. && std::abs(t[i].across_flats - m_diameter) <= 0.01) {
                m_standard = int(i);
                break;
            }
        } else if (std::abs(t[i].pocket_d - m_diameter) <= 0.01) {
            m_standard = int(i);
            break;
        }
    }
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_tolerance(double t)
{
    m_tolerance     = t;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_head_fit(double f)
{
    m_head_fit      = f;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_through(bool t)
{
    m_through       = t;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_depth(double d)
{
    m_depth         = std::max(0.1, d);
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::set_flip(bool f)
{
    m_flip          = f;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::gizmo_toggle_hole(int idx)
{
    if (m_dirty)
        detect();
    if (idx < 0 || idx >= int(m_holes.size()))
        return;
    if (m_operation == HoleOperation::Teardrop) {
        if (!m_holes[idx].horizontal)
            return; // a vertical hole has no teardrop
        toggle_teardrop(idx);
    } else {
        toggle_bore(idx);
    }
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::gizmo_apply_all()
{
    if (m_dirty)
        detect();
    for (size_t i = 0; i < m_holes.size(); ++i) {
        if (m_operation == HoleOperation::Teardrop) {
            if (m_holes[i].horizontal && !m_teardrop[i])
                toggle_teardrop(int(i));
        } else if (!m_bore[i]) {
            toggle_bore(int(i));
        }
    }
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::gizmo_clear_all()
{
    if (m_dirty)
        detect();
    clear_all();
    refresh_applied();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::gizmo_refresh()
{
    m_dirty         = true;
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

// ---------------------------------------------------------------------------------------------
// Input
// ---------------------------------------------------------------------------------------------

bool GLGizmoHoles::on_mouse(const wxMouseEvent &mouse_event)
{
    const bool on_hole = m_hover_id >= 0 && m_hover_id < int(m_holes.size());
    if (mouse_event.LeftDown() && on_hole) {
        gizmo_toggle_hole(m_hover_id);
        return true;
    }
    if (mouse_event.LeftUp() && on_hole)
        return true;
    return false;
}

void GLGizmoHoles::on_render_input_window(float x, float y, float bottom_limit)
{
    ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    if (m_dirty)
        detect();

    const float scale = m_parent.get_scale();
    y = std::min(y, bottom_limit - m_imgui->scaled(22.f));
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    const float sliders_width = m_imgui->scaled(7.0f);
    const float left_width    = m_imgui->calc_text_size(m_desc.at("true_dia")).x + m_imgui->scaled(1.0f);

    // Operation: two mutually exclusive buttons; the active one is highlighted.
    {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("operation"));
        ImGui::SameLine(left_width);
        auto op_button = [&](const wxString &label, HoleOperation op) {
            const bool on = (m_operation == op);
            if (on) {
                ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
                ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
            }
            const bool clicked = m_imgui->button(label);
            if (on)
                ImGui::PopStyleColor(3);
            if (clicked)
                set_operation(op);
        };
        op_button(m_desc.at("op_teardrop"), HoleOperation::Teardrop);
        ImGui::SameLine();
        op_button(m_desc.at("op_bore"), HoleOperation::Bore);
    }

    ImGui::Separator();

    if (m_operation == HoleOperation::Teardrop) {
        // Apex angle: slider plus a typed value.
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("apex"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        float angle = m_angle_deg;
        if (m_imgui->bbl_slider_float_style("##apex", &angle, ANGLE_MIN, ANGLE_MAX, "%.0f", 1.0f, true))
            set_angle(angle);
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(m_imgui->scaled(4.0f));
        if (ImGui::InputFloat("##apex_in", &angle, 1.f, 5.f, "%.0f", ImGuiInputTextFlags_EnterReturnsTrue))
            set_angle(angle);
        ImGui::PopItemWidth();
    } else {
        // Category buttons (image buttons in Orca's toolbar style).
        const HoleCategory categories[] = {HoleCategory::Screw, HoleCategory::Nut, HoleCategory::Magnet,
                                           HoleCategory::Insert, HoleCategory::Custom};
        // `scaled()` multiplies by the font size, so keep this a small factor (~1.5 line heights).
        const float icon_sz = m_imgui->scaled(1.5f);
        for (HoleCategory cat : categories) {
            ImTextureID  tex = category_icon(cat);
            const bool   on  = (m_category == cat);
            const ImVec4 tint = on ? ImVec4(0.10f, 0.59f, 0.53f, 1.f) : ImVec4(1.f, 1.f, 1.f, 1.f);
            const ImVec4 bg   = on ? ImVec4(0.92f, 0.92f, 0.92f, 1.f) : ImVec4(0.f, 0.f, 0.f, 0.f);
            if (tex != nullptr) {
                if (m_imgui->image_button(tex, ImVec2(icon_sz, icon_sz), ImVec2(0, 0), ImVec2(1, 1), -1, bg, tint))
                    set_category(cat);
            } else if (m_imgui->button(category_label(cat), ImVec2(icon_sz, icon_sz), true)) {
                set_category(cat);
            }
            ImGui::SameLine();
        }
        ImGui::NewLine();

        // Items of the chosen category.
        {
            std::vector<std::string>         names;
            std::vector<int>                 idxs;
            const std::vector<HoleStandard> &t = hole_standards();
            for (size_t i = 0; i < t.size(); ++i)
                if (category_matches(m_category, t[i].kind)) {
                    names.push_back(t[i].designation);
                    idxs.push_back(int(i) + 1);
                }
            if (!names.empty()) {
                int sel = 0;
                for (size_t k = 0; k < idxs.size(); ++k)
                    if (idxs[k] == m_standard + 1) {
                        sel = int(k);
                        break;
                    }
                const int prev = sel;
                if (render_combo(m_desc.at("standard").ToStdString(), names, sel, left_width, m_imgui->scaled(12.0f)) && sel != prev)
                    set_standard(idxs[sel]);
            }
        }

        const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                    ? &hole_standards()[m_standard]
                                    : nullptr;
        const bool is_screw  = s != nullptr && s->kind == HoleStandardKind::Screw;
        const bool is_nut    = s != nullptr && s->kind == HoleStandardKind::Nut;
        const bool is_pocket = s != nullptr && (s->kind == HoleStandardKind::Insert || s->kind == HoleStandardKind::Magnet);

        // Diameter is always editable; typing an existing standard's size relabels the combo.
        ImGui::AlignTextToFramePadding();
        m_imgui->text(is_nut ? m_desc.at("across_flats") : m_desc.at("diameter"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        float d = float(m_diameter);
        if (ImGui::InputFloat("##dia", &d, 0.1f, 1.f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
            set_diameter(d);
        ImGui::PopItemWidth();

        // Tolerance: read-only and fit-derived for inserts/magnets, editable otherwise.
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("tolerance"));
        ImGui::SameLine(left_width);
        ImGui::PushItemWidth(sliders_width);
        if (is_pocket) {
            float t = float(hole_fit_diameter_delta(s->kind, m_diameter, HoleFit(m_fit)));
            m_imgui->disabled_begin(true);
            ImGui::InputFloat("##tol", &t, 0.f, 0.f, "%.2f");
            m_imgui->disabled_end();
        } else {
            float t = float(m_tolerance);
            if (ImGui::InputFloat("##tol", &t, 0.05f, 0.2f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
                set_tolerance(t);
        }
        ImGui::PopItemWidth();

        // Read-only effective diameter = diameter + tolerance.
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("true_dia"));
        ImGui::SameLine(left_width);
        m_imgui->disabled_begin(true);
        ImGui::PushItemWidth(sliders_width);
        float true_d = float(bore_diameter());
        ImGui::InputFloat("##true_dia", &true_d, 0.f, 0.f, "%.2f");
        ImGui::PopItemWidth();
        m_imgui->disabled_end();

        if (is_screw) {
            // Free (clearance) or Tap (thread-forming into plastic). Only the fits this screw
            // actually has are shown.
            const bool has_free = s->clearance_d > 0.;
            const bool has_tap  = s->tap_d > 0.;
            if (has_free || has_tap) {
                ImGui::AlignTextToFramePadding();
                m_imgui->text(m_desc.at("screw_fit"));
                ImGui::SameLine(left_width);
                bool first = true;
                if (has_free) {
                    if (m_imgui->button(m_desc.at("fit_free")))
                        set_screw_fit(ScrewFit::Free);
                    first = false;
                }
                if (has_tap) {
                    if (!first)
                        ImGui::SameLine();
                    if (m_imgui->button(m_desc.at("fit_tap")))
                        set_screw_fit(ScrewFit::Tap);
                }
            }

            // Head styles this screw has dimensions for; hidden entirely when it has none.
            const bool has_head = s->socket_d > 0. || s->button_d > 0. || s->csink_d > 0.;
            if (has_head) {
                ImGui::AlignTextToFramePadding();
                m_imgui->text(m_desc.at("head"));
                ImGui::SameLine(left_width);
                if (m_imgui->button(m_desc.at("head_none")))
                    set_head(BoreHead::None);
                if (s->socket_d > 0.) {
                    ImGui::SameLine();
                    if (m_imgui->button(m_desc.at("head_socket")))
                        set_head(BoreHead::SocketHead);
                }
                if (s->button_d > 0.) {
                    ImGui::SameLine();
                    if (m_imgui->button(m_desc.at("head_button")))
                        set_head(BoreHead::ButtonHead);
                }
                if (s->csink_d > 0.) {
                    ImGui::SameLine();
                    if (m_imgui->button(m_desc.at("head_csink")))
                        set_head(BoreHead::Countersink);
                }
            }
        }

        if (is_pocket) {
            ImGui::AlignTextToFramePadding();
            m_imgui->text(m_desc.at("fit"));
            ImGui::SameLine(left_width);
            if (m_imgui->button(m_desc.at("fit_tight")))
                set_fit(HoleFit::Tight);
            ImGui::SameLine();
            if (m_imgui->button(m_desc.at("fit_slip")))
                set_fit(HoleFit::Slip);
        }

        // Head fit: sink the head / pocket below the surface by 0.08 or 0.16 mm (Z tolerance).
        {
            const bool head_active = is_screw && m_head != BoreHead::None &&
                                     ((m_head == BoreHead::SocketHead && s->socket_d > 0.) ||
                                      (m_head == BoreHead::ButtonHead && s->button_d > 0.) ||
                                      (m_head == BoreHead::Countersink && s->csink_d > 0.));
            const bool show = head_active || is_nut || (s != nullptr && s->kind == HoleStandardKind::Magnet);
            if (show) {
                ImGui::AlignTextToFramePadding();
                m_imgui->text(m_desc.at("head_fit"));
                ImGui::SameLine(left_width);
                auto fit_button = [&](const wxString &label, double v) {
                    const bool on = std::abs(m_head_fit - v) < 1e-6;
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
                    }
                    const bool clicked = m_imgui->button(label);
                    if (on)
                        ImGui::PopStyleColor(3);
                    if (clicked)
                        set_head_fit(v);
                };
                fit_button(m_desc.at("fit_flush"), 0.0);
                ImGui::SameLine();
                fit_button(m_desc.at("sink_008"), -0.08);
                ImGui::SameLine();
                fit_button(m_desc.at("sink_016"), -0.16);
            }
        }

        if (is_pocket || is_nut) {
            // Insert heights map to the pocket depth; the depth can still be fine-tuned below.
            if (s != nullptr && s->kind == HoleStandardKind::Insert && !s->insert_heights.empty()) {
                ImGui::AlignTextToFramePadding();
                m_imgui->text(m_desc.at("height"));
                ImGui::SameLine(left_width);
                for (size_t hi = 0; hi < s->insert_heights.size(); ++hi) {
                    const double hv = s->insert_heights[hi];
                    const bool   on = std::abs(m_depth - hv) < 1e-6;
                    if (on) {
                        ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
                        ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
                    }
                    const bool clicked = m_imgui->button(wxString::Format("%.1f", hv));
                    if (on)
                        ImGui::PopStyleColor(3);
                    if (clicked)
                        set_depth(hv);
                    if (hi + 1 < s->insert_heights.size())
                        ImGui::SameLine();
                }
            }
            // Editable pocket depth (insert / magnet / nut).
            ImGui::AlignTextToFramePadding();
            m_imgui->text(m_desc.at("depth"));
            ImGui::SameLine(left_width);
            ImGui::PushItemWidth(sliders_width);
            float pocket_depth = float(m_depth);
            if (ImGui::InputFloat("##depth", &pocket_depth, 0.5f, 2.f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
                m_depth = std::max(0.1, double(pocket_depth));
            ImGui::PopItemWidth();
        } else {
            bool through = m_through;
            ImGui::AlignTextToFramePadding();
            m_imgui->text(m_desc.at("through"));
            ImGui::SameLine(left_width);
            if (ImGui::Checkbox("##through", &through))
                set_through(through);
            if (!m_through) {
                ImGui::SameLine();
                ImGui::PushItemWidth(sliders_width);
                float depth = float(m_depth);
                if (ImGui::InputFloat("##depth", &depth, 0.5f, 2.f, "%.2f", ImGuiInputTextFlags_EnterReturnsTrue))
                    m_depth = std::max(0.1, double(depth));
                ImGui::PopItemWidth();
            }
        }

        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("entry"));
        ImGui::SameLine(left_width);
        if (m_imgui->button(m_desc.at("entry")))
            set_flip(!m_flip);
    }

    ImGui::Separator();

    const int applied = int(std::count_if(m_operation == HoleOperation::Teardrop ? m_teardrop.begin() : m_bore.begin(),
                                          m_operation == HoleOperation::Teardrop ? m_teardrop.end() : m_bore.end(),
                                          [](char a) { return a != 0; }));
    m_imgui->text(wxString::Format("%s: %d (%d applied)", m_desc.at("holes").c_str(), int(m_holes.size()), applied));

    m_imgui->disabled_begin(m_holes.empty());
    if (m_imgui->button(m_desc.at("all")))
        gizmo_apply_all();
    m_imgui->disabled_end();
    ImGui::SameLine();
    if (m_imgui->button(m_desc.at("clear")))
        gizmo_clear_all();

    ImGui::Separator();

    if (m_c->object_clipper()->get_position() == 0.f) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(m_desc.at("clipping_of_view"));
    } else if (m_imgui->button(m_desc.at("reset_direction"))) {
        wxGetApp().CallAfter([this]() { m_c->object_clipper()->set_position_by_ratio(-1., false); });
    }
    auto clp_dist = float(m_c->object_clipper()->get_position());
    ImGui::SameLine(left_width);
    ImGui::PushItemWidth(sliders_width);
    if (m_imgui->bbl_slider_float_style("##clp_dist", &clp_dist, 0.f, 1.f, "%.2f", 1.0f, true))
        m_c->object_clipper()->set_position_by_ratio(clp_dist, true);
    ImGui::PopItemWidth();

    ImGui::Separator();

    GLGizmoUtils::begin_right_aligned_buttons({_L("Done")});
    if (m_imgui->button(_L("Done")))
        m_parent.reset_all_gizmos();

    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
}

} // namespace GUI
} // namespace Slic3r
