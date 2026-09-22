#include "GLGizmoHoles.hpp"
#include "GLGizmoUtils.hpp"

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/CutUtils.hpp"
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
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>

namespace Slic3r {
namespace GUI {

namespace {

constexpr const char *TEARDROP_NAME = "Teardrop";
constexpr const char *POCKET_NAME   = "HolePocket";
constexpr const char *FACE_POCKET_NAME = "FacePocket";
// |axis . up| below this marks a hole as horizontal (the top of the wall is an overhang).
constexpr double HORIZONTAL_COS = 0.5;
// How long the face highlight and the snap markers survive after the cursor leaves the face.
constexpr double FACE_LEAVE_GRACE_SEC = 0.35;
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

double GLGizmoHoles::object_scale() const
{
    // Uniform scale of the instance's linear part. A circle cannot stay exact under a non-uniform
    // scale, so the mean of the three axis scales is used as the best single factor.
    const Matrix3d linear = instance_matrix().linear();
    const double   s      = (linear.col(0).norm() + linear.col(1).norm() + linear.col(2).norm()) / 3.0;
    return (std::isfinite(s) && s > 1e-9) ? s : 1.0;
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

void GLGizmoHoles::feature_frame(const DetectedHole &h, Vec3d &dir, Vec3d &entry) const
{
    const Vec3d a = h.axis.normalized();
    dir   = m_flip ? -a : a;
    entry = h.center - dir * (0.5 * h.depth);
}

indexed_triangle_set GLGizmoHoles::teardrop_mesh(const DetectedHole &h) const
{
    return its_make_teardrop_for_hole(h, teardrop_depth(h), m_angle_deg, HOLE_SHAPE_SEGMENTS, object_up());
}

indexed_triangle_set GLGizmoHoles::bore_negative_mesh(const DetectedHole &h) const
{
    Vec3d dir, entry;
    feature_frame(h, dir, entry);

    const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                ? &hole_standards()[m_standard]
                                : nullptr;
    const bool is_nut    = s != nullptr && s->kind == HoleStandardKind::Nut;
    const bool is_pocket = s != nullptr && (s->kind == HoleStandardKind::Insert || s->kind == HoleStandardKind::Magnet);

    // All dimensions below are mm in world space and are converted to object space with `scale`
    // so the feature measures its stated size even when the object is scaled.
    const double scale = object_scale();

    const double margin = std::max(0.5, 0.25 * h.radius); // h.radius is already object space
    double       depth  = 0.;
    if (is_pocket || is_nut)
        depth = std::max(0.1, m_depth / scale); // editable pocket depth
    else if (m_through)
        depth = h.depth + 2.0 * margin;
    else
        depth = std::max(0.1, m_depth / scale);

    const double sink = std::max(0.0, -m_head_fit) / scale; // depth the head/pocket is sunk below the surface

    if (is_nut) {
        const double across = std::max(0.1, (m_diameter + m_tolerance) / scale);
        return its_make_nut_pocket(across, depth + sink, s->clearance_d / scale, depth, dir, entry);
    }

    const double d = bore_diameter() / scale;
    if (d <= 0. || depth <= 0.)
        return {};

    if (s != nullptr && s->kind == HoleStandardKind::Screw) {
        const double socket_d = s->socket_d / scale;
        const double button_d = s->button_d / scale;
        const double csink_d  = s->csink_d / scale;
        if (m_head == BoreHead::SocketHead && socket_d > d)
            return its_make_counterbore(d, socket_d, s->socket_k / scale + sink, depth, dir, entry);
        if (m_head == BoreHead::ButtonHead && button_d > d)
            return its_make_counterbore(d, button_d, s->button_k / scale + sink, depth, dir, entry);
        // A sunk countersunk head only deepens the cone's top: the entry stays at the surface and
        // the cone grows by 2*sink in diameter at the same angle, so the head sits `sink` lower.
        if (m_head == BoreHead::Countersink && csink_d > d)
            return its_make_countersink(d, csink_d + 2.0 * sink, s->csink_angle, depth, dir, entry);
    }
    if (s != nullptr && s->kind == HoleStandardKind::Magnet)
        depth += sink; // sink the magnet pocket
    return its_make_bore(d, depth, dir, entry);
}

indexed_triangle_set GLGizmoHoles::bore_tube_mesh(const DetectedHole &h) const
{
    const HoleStandard *s = (m_standard >= 0 && m_standard < int(hole_standards().size()))
                                ? &hole_standards()[m_standard]
                                : nullptr;
    if (s != nullptr && s->kind == HoleStandardKind::Nut)
        return {}; // a hex pocket has no round shrink tube

    const double target_d = bore_diameter() / object_scale(); // object space, to match exist_d
    const double exist_d  = 2.0 * h.radius;
    if (target_d <= 0. || target_d >= exist_d - 0.01)
        return {}; // enlarging or matching: no fill needed

    Vec3d dir, entry;
    feature_frame(h, dir, entry);
    const double outer_d = exist_d + 0.4; // overlap the existing wall
    const double depth   = h.depth + std::max(0.4, 0.2 * h.depth);
    return its_make_tube(outer_d, target_d, depth, dir, entry);
}

indexed_triangle_set GLGizmoHoles::shape_mesh(const DetectedHole &h) const
{
    return m_operation == HoleOperation::Teardrop ? teardrop_mesh(h) : bore_negative_mesh(h);
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
        exit_place_face_mode();
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
        exit_place_face_mode();
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
    if (m_place_face_mode)
        m_hover_id = -1; // detected-hole picking is off while placing on a face
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

    m_merged_its = mesh.its;

    // Placed face features are tracked by their own monotonic id, independent of detected holes.
    int                     max_face_id = 0;
    std::vector<PlacedFace> kept_placed;
    for (const ModelVolume *v : mo->volumes)
        if (v != nullptr)
            max_face_id = std::max(max_face_id, parsed_hole_index(v->name, FACE_POCKET_NAME));
    for (const PlacedFace &f : m_placed)
        for (const ModelVolume *v : mo->volumes)
            if (v != nullptr && v->is_negative_volume() && parsed_hole_index(v->name, FACE_POCKET_NAME) == f.id) {
                kept_placed.push_back(f);
                break;
            }
    m_placed       = std::move(kept_placed);
    m_next_face_id = max_face_id + 1;

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
            if (parsed_hole_index(v->name, TEARDROP_NAME) < 0 && parsed_hole_index(v->name, POCKET_NAME) < 0 &&
                parsed_hole_index(v->name, FACE_POCKET_NAME) < 0)
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
        indexed_triangle_set ghost = shape_mesh(m_holes[i].hole);
        if (ghost.indices.empty())
            continue;
        merge_into(all_its, ghost);
        if (int(i) == m_hover_id) {
            indexed_triangle_set s = shape_mesh(m_holes[i].hole);
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
    if (m_place_face_mode) {
        // A tap of Alt must not leave the hole pinned to a coordinate: on release the snap session
        // ends at once and the ghost goes back to the raw hit, without waiting for the mouse to move.
        const bool alt = wxGetKeyState(WXK_ALT);
        if (alt != m_alt_held) {
            m_alt_held = alt;
            if (!alt)
                clear_snap_session();
        }
        update_face_highlight();
    }

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
    // The detected-hole previews only get in the way of picking a face to place on.
    if (!m_place_face_mode) {
        m_preview_all.model.render(shader);
        m_preview_applied.model.render(shader);
        m_preview_hover.model.render(shader);
    }
    if (m_place_face_mode)
        m_face_ghost.render(shader);

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();

    if (m_place_face_mode) {
        render_face_highlight();
        render_snap_markers();
    }
}

// ---------------------------------------------------------------------------------------------
// Place on a picked face
// ---------------------------------------------------------------------------------------------

void GLGizmoHoles::set_place_face_mode(bool on)
{
    if (m_place_face_mode == on)
        return;
    m_place_face_mode = on;
    if (on)
        m_hover_id = -1; // suppress detected-hole hover while placing
    else
        exit_place_face_mode();
    m_preview_dirty = true;
    m_parent.set_as_dirty();
}

void GLGizmoHoles::exit_place_face_mode()
{
    m_place_face_mode = false;
    m_face_highlight.reset();
    m_face_ghost.reset();
    m_hover_face_mv    = nullptr;
    m_hover_face_facet = -1;
    clear_snap_session();
    m_alt_held = false;
    for (GLModel &m : m_snap_markers)
        m.reset();
}

// Ends the Alt snap session: the next Alt press picks the face under the cursor afresh. Dropping
// the hit also makes the ghost rebuild from the raw cursor position on the next pass.
void GLGizmoHoles::clear_snap_session()
{
    m_snap_locked       = false;
    m_snap_mv           = nullptr;
    m_snap_facet        = -1;
    m_hover_snap        = FaceSnapKind::None;
    m_snap_marker_active.reset();
    m_last_face_hit     = Vec3d::Constant(1e30);
}

bool GLGizmoHoles::gizmo_place_face_at(const Vec2d &screen_pos)
{
    return m_place_face_mode && place_face_at(screen_pos);
}

bool GLGizmoHoles::gizmo_face_info_at(const Vec2d &screen_pos, int &facet, int &region_facets, Vec3d &normal)
{
    facet         = -1;
    region_facets = 0;
    normal        = Vec3d::Zero();

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return false;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             f      = 0;
    Vec3d              hit    = Vec3d::Zero();
    if (!raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, f, hit))
        return false;

    facet         = int(f);
    normal        = facet_normal_in_world(mv->mesh().its, int(f), mv->get_matrix());
    region_facets = int(coplanar_region(mv, f, m_face_cache).size());
    return true;
}

DetectedHole GLGizmoHoles::face_hole(const Vec3d &hit_world, const Vec3d &outward_normal) const
{
    const Vec3d n   = (outward_normal.allFinite() && outward_normal.norm() > 1e-9) ? outward_normal.normalized() : Vec3d::UnitZ();
    const Vec3d dir = -n; // into the material

    // `hit_world` is in object space; the editable dimensions are mm in world space, so convert.
    const double scale = object_scale();
    const double d_obj = bore_diameter() / scale;

    double depth = std::max(0.1, m_depth / scale);
    if (m_through) {
        const double d = hole_through_depth(m_merged_its, hit_world, dir, std::max(0.5, 0.25 * d_obj));
        if (d > 0.)
            depth = d;
    }

    DetectedHole h;
    h.axis    = dir;
    h.center  = hit_world + dir * (0.5 * depth);
    h.depth   = depth;
    h.radius  = 0.5 * d_obj;
    h.through = m_through;
    return h;
}

// A teardrop only has meaning on a near-vertical face; fall back to a plain bore otherwise. The
// `flip` control is meaningless for a placed face (the entry is fixed by the picked surface).
indexed_triangle_set GLGizmoHoles::face_shape_mesh(const DetectedHole &hole)
{
    const bool saved_flip = m_flip;
    m_flip = false;
    indexed_triangle_set shape = shape_mesh(hole);
    if (shape.indices.empty())
        shape = bore_negative_mesh(hole);
    m_flip = saved_flip;
    return shape;
}

bool GLGizmoHoles::place_face_at(const Vec2d &screen_pos)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return false;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit    = Vec3d::Zero();
    if (!raycast_object_face(screen_pos, m_parent.get_selection(), mo, clipping, volume, mv, facet, hit))
        return false;

    // Work in object space: the added volume and the previews both live there.
    const Vec3d hit_obj = instance_matrix().inverse() * hit;
    FaceSnapKind snap_kind = FaceSnapKind::None;
    const Vec3d  place_obj = snap_face_hit(hit_obj, screen_pos, mv, facet, snap_kind);
    const DetectedHole  hole = face_hole(place_obj, facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()));
    indexed_triangle_set neg     = face_shape_mesh(hole);
    if (neg.indices.empty())
        return false;

    const int id = m_next_face_id++;
    add_named_volume(id, neg, ModelVolumeType::NEGATIVE_VOLUME, feature_name(FACE_POCKET_NAME, id), true);

    PlacedFace placed;
    placed.id   = id;
    placed.hole = hole;
    m_placed.push_back(placed);

    m_preview_dirty = true;
    m_parent.set_as_dirty();
    return true;
}

void GLGizmoHoles::update_face_highlight()
{
    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    const ClippingPlane *clipping = nullptr;
    if (auto oc = m_c->object_clipper())
        clipping = oc->get_clipping_plane();

    const GLVolume    *volume = nullptr;
    const ModelVolume *mv     = nullptr;
    size_t             facet  = 0;
    Vec3d              hit    = Vec3d::Zero();
    if (!raycast_object_face(m_parent.get_local_mouse_position(), m_parent.get_selection(), mo, clipping, volume, mv, facet, hit)) {
        // Leaving the face near an edge must not wipe the highlight and the snap markers at once:
        // hold them for a moment so the cursor can come back.
        const double now = std::chrono::duration<double>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (m_face_leave_time == 0.0)
            m_face_leave_time = now;
        if (now - m_face_leave_time < FACE_LEAVE_GRACE_SEC) {
            m_parent.set_as_dirty();
            return;
        }
        m_face_leave_time = 0.0;
        if (m_hover_face_mv != nullptr || m_face_highlight.is_initialized()) {
            m_face_highlight.reset();
            m_face_ghost.reset();
            m_hover_face_mv    = nullptr;
            m_hover_face_facet = -1;
            m_last_face_hit    = Vec3d::Constant(1e30);
            m_hover_snap       = FaceSnapKind::None;
            m_parent.set_as_dirty();
        }
        return;
    }
    m_face_leave_time = 0.0;

    const Vec3d hit_obj      = instance_matrix().inverse() * hit;
    const bool  face_changed = (mv != m_hover_face_mv || int(facet) != m_hover_face_facet);
    // The ghost follows the cursor, so it must be rebuilt whenever the hit point moves, not only
    // when the facet changes: a flat face is a few large triangles, so the facet can stay the same
    // across most of the face. The coplanar patch depends only on the facet.
    FaceSnapKind snap_kind = FaceSnapKind::None;
    const Vec3d  place_obj = snap_face_hit(hit_obj, m_parent.get_local_mouse_position(), mv, facet, snap_kind);
    const double move_eps  = std::max(1e-4, 0.01 * std::max(0.1, m_diameter / object_scale()));
    if (!face_changed && (place_obj - m_last_face_hit).norm() <= move_eps && snap_kind == m_hover_snap)
        return;

    if (face_changed) {
        m_hover_face_mv    = mv;
        m_hover_face_facet = int(facet);
        build_face_highlight(mv, facet);
    }
    m_last_face_hit = place_obj;
    m_hover_snap    = snap_kind;

    const DetectedHole  hole  = face_hole(place_obj, facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()));
    indexed_triangle_set ghost = face_shape_mesh(hole);
    m_face_ghost.reset();
    if (!ghost.indices.empty()) {
        m_face_ghost.init_from(ghost);
        m_face_ghost.set_color(HOVER_COLOR);
    }
    m_parent.set_as_dirty();
}

void GLGizmoHoles::build_face_highlight(const ModelVolume *mv, size_t facet)
{
    m_face_highlight.reset();
    if (mv == nullptr)
        return;
    const std::vector<int> region = coplanar_region(mv, facet, m_face_cache);
    if (region.empty())
        return;
    indexed_triangle_set patch = build_coplanar_patch(mv, region, m_face_cache, 0.05f);
    if (patch.indices.empty())
        return;
    m_face_highlight.init_from(patch);
    m_face_highlight.set_color(ColorRGBA(0.10f, 0.80f, 0.74f, 0.55f));
}

void GLGizmoHoles::render_face_highlight()
{
    if (m_hover_face_mv == nullptr || !m_face_highlight.is_initialized())
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->start_using();
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("view_model_matrix", camera.get_view_matrix() * instance_matrix() * m_hover_face_mv->get_matrix());
    m_face_highlight.render();
    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    shader->stop_using();
}

// Candidates of the hovered face, rebuilt only when the face changes.
const std::vector<FaceSnapPoint> &GLGizmoHoles::face_snap_points(const ModelVolume *mv, size_t facet)
{
    if (mv != m_snap_mv || int(facet) != m_snap_facet) {
        m_snap_mv     = mv;
        m_snap_facet  = int(facet);
        m_snap_points = build_face_snap_points(mv, coplanar_region(mv, facet, m_face_cache));
        build_snap_markers();
    }
    return m_snap_points;
}

// Marker spheres for the snap coordinates of the session face, rebuilt with the candidate list.
void GLGizmoHoles::build_snap_markers()
{
    for (GLModel &m : m_snap_markers)
        m.reset();
    m_snap_marker_active.reset();

    if (m_snap_points.empty() || m_snap_mv == nullptr) {
        m_snap_radius = 0.0;
        return;
    }

    // Face-relative size, so a big face gets markers you can see and a small one is not swamped.
    double diag = 0.0;
    for (const FaceSnapPoint &a : m_snap_points)
        for (const FaceSnapPoint &b : m_snap_points)
            diag = std::max(diag, (a.pos - b.pos).norm());
    m_snap_radius = std::max(1e-4, 0.012 * diag);

    static const ColorRGBA KIND_COLOR[5] = { ColorRGBA(1.00f, 0.30f, 0.85f, 1.0f),   // corner, magenta
                                             ColorRGBA(0.35f, 0.90f, 0.75f, 1.0f),   // edge midpoint, teal
                                             ColorRGBA(0.30f, 0.80f, 1.00f, 1.0f),   // face centre, cyan
                                             ColorRGBA(0.60f, 0.95f, 0.45f, 1.0f),   // edge quarter, green
                                             ColorRGBA(0.55f, 0.72f, 1.00f, 1.0f) }; // face quarter, pale blue
    std::vector<indexed_triangle_set> acc(5);
    for (const FaceSnapPoint &p : m_snap_points) {
        const int k = int(p.kind) - 1;
        if (k < 0 || k > 4)
            continue;
        indexed_triangle_set sphere = its_make_sphere(m_snap_radius, PI / 12.0);
        its_translate(sphere, p.pos.cast<float>());
        its_merge(acc[k], sphere);
    }
    for (int k = 0; k < 5; ++k) {
        if (acc[k].indices.empty())
            continue;
        m_snap_markers[k].init_from(acc[k]);
        m_snap_markers[k].set_color(KIND_COLOR[k]);
    }
}

// A single bigger sphere marks the coordinate the cursor is currently locked to.
void GLGizmoHoles::set_active_snap_marker(const FaceSnapPoint &p)
{
    m_snap_marker_active.reset();
    if (m_snap_radius <= 0.0)
        return;
    indexed_triangle_set sphere = its_make_sphere(m_snap_radius * 1.4, PI / 12.0);
    its_translate(sphere, p.pos.cast<float>());
    m_snap_marker_active.init_from(sphere);
    m_snap_marker_active.set_color(HOVER_COLOR);
}

// Draws the marker spheres of the session face while Alt is held. Depth test is off so the surface
// and the lifted coplanar patch cannot hide them.
void GLGizmoHoles::render_snap_markers()
{
    if (!wxGetKeyState(WXK_ALT) || m_snap_mv == nullptr)
        return;
    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera &camera = wxGetApp().plater()->get_camera();
    shader->start_using();
    glsafe(::glDisable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    shader->set_uniform("view_model_matrix",
                        camera.get_view_matrix() * instance_matrix() * m_snap_mv->get_matrix());
    for (GLModel &m : m_snap_markers)
        m.render();
    m_snap_marker_active.render();
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glEnable(GL_DEPTH_TEST));
    shader->stop_using();
}

// With Alt held, moves an object-space hit onto the nearest face coordinate within a few pixels;
// returns the hit unchanged otherwise. `kind` is None when nothing was snapped.
Vec3d GLGizmoHoles::snap_face_hit(const Vec3d &hit_obj, const Vec2d &screen_pos, const ModelVolume *mv,
                                 size_t facet, FaceSnapKind &kind)
{
    kind = FaceSnapKind::None;
    if (mv == nullptr || !wxGetKeyState(WXK_ALT)) {
        m_snap_locked = false;
        m_snap_marker_active.reset();
        return hit_obj;
    }

    // Gated to the face the cursor was on when Alt was pressed: the hole keeps following the cursor
    // onto another face, it just never snaps to coordinates of that face.
    if (m_snap_mv != nullptr && (mv != m_snap_mv || int(facet) != m_snap_facet)) {
        m_snap_locked = false;
        m_snap_marker_active.reset();
        return hit_obj;
    }

    const std::vector<FaceSnapPoint> &pts = face_snap_points(mv, facet);
    if (pts.empty()) {
        m_snap_locked = false;
        m_snap_marker_active.reset();
        return hit_obj;
    }

    const Camera     &camera  = wxGetApp().plater()->get_camera();
    const Transform3d inst    = instance_matrix();
    const Transform3d to_obj  = mv->get_matrix();
    const auto        project = [&](const Vec3d &p) { return world_to_screen(camera, inst * to_obj * p); };

    // Magnetic: hold the current coordinate until the cursor leaves its stick distance by a margin,
    // so neighbouring candidates do not flicker into each other at the boundary.
    if (m_snap_locked && m_snap_lock_mv == mv && m_snap_lock_facet == int(facet)) {
        const Vec2d locked_px = project(m_snap_lock.pos);
        if (locked_px.allFinite() && (locked_px - screen_pos).norm() <= 1.25 * m_snap_lock_tol) {
            kind = m_snap_lock.kind;
            return to_obj * m_snap_lock.pos;
        }
    }

    FaceSnapPoint best;
    double        tol = 0.0;
    if (!nearest_face_snap(pts, project, screen_pos, best, 8.0, 48.0, &tol)) {
        m_snap_locked = false;
        m_snap_marker_active.reset();
        return hit_obj;
    }

    const bool target_changed = !m_snap_locked || m_snap_lock_mv != mv || m_snap_lock_facet != int(facet) ||
                                (m_snap_lock.pos - best.pos).norm() > 1e-9;
    m_snap_lock       = best;
    m_snap_lock_tol   = tol;
    m_snap_locked     = true;
    m_snap_lock_mv    = mv;
    m_snap_lock_facet = int(facet);
    if (target_changed)
        set_active_snap_marker(best);

    kind = best.kind;
    return to_obj * best.pos;
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
        add_named_volume(idx, teardrop_mesh(m_holes[idx].hole), ModelVolumeType::NEGATIVE_VOLUME, feature_name(TEARDROP_NAME, idx), true);
}

void GLGizmoHoles::toggle_bore(int idx)
{
    if (m_bore[idx]) {
        remove_named_volumes(idx, POCKET_NAME, _u8L("Remove pocket"));
        return;
    }
    const std::string name = feature_name(POCKET_NAME, idx);
    // Shrink first (positive tube), then the negative bore; the negative clips the tube too.
    indexed_triangle_set tube = bore_tube_mesh(m_holes[idx].hole);
    if (!tube.empty())
        add_named_volume(idx, tube, ModelVolumeType::MODEL_PART, name, true);
    indexed_triangle_set neg = bore_negative_mesh(m_holes[idx].hole);
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
        if ((v->is_negative_volume() && v->name.rfind(TEARDROP_NAME, 0) == 0) || v->name.rfind(POCKET_NAME, 0) == 0 ||
            v->name.rfind(FACE_POCKET_NAME, 0) == 0)
            items.emplace_back(ItemType::itVolume, oi, int(vi));
    }
    m_placed.clear();
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
    if (m_place_face_mode) {
        if (mouse_event.LeftDown()) {
            place_face_at(m_parent.get_local_mouse_position());
            return true;
        }
        if (mouse_event.RightDown()) {
            exit_place_face_mode();
            m_parent.set_as_dirty();
            return true;
        }
        return false;
    }

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

    // Place a new pocket on any flat face, instead of only reworking detected holes.
    {
        const bool on = m_place_face_mode;
        if (on) {
            ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.10f, 0.59f, 0.53f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.12f, 0.68f, 0.61f, 1.f));
            ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.08f, 0.50f, 0.45f, 1.f));
        }
        const bool clicked = m_imgui->button(on ? _L("Cancel face pick") : _L("Place on face"));
        if (on)
            ImGui::PopStyleColor(3);
        if (clicked)
            set_place_face_mode(!on);
        if (m_place_face_mode)
            m_imgui->text(_L("Click a flat face to place the feature; right-click to finish."));
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
