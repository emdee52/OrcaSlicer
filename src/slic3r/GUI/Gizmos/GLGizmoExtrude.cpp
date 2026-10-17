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

// [ORCAPORT:HF-3] The highlight colours, lifted from the fork's Align & Construct preview so the two
// tools read the same: teal is "the face under your cursor", green is "the face you picked", orange
// is "the solid you are about to make". Alpha is part of the value - these are overlays.
const ColorRGBA HOVER_COLOR { 0.10f, 0.80f, 0.74f, 0.55f };  // teal
const ColorRGBA PICK_COLOR  { 0.16f, 0.85f, 0.30f, 0.60f };  // green
const ColorRGBA PRISM_COLOR { 1.00f, 0.55f, 0.00f, 0.55f };  // orange
// Applied fills, the same red the hole gizmo uses for applied holes.
const ColorRGBA APPLIED_COLOR{ 1.00f, 0.15f, 0.15f, 0.80f };

// How far the object is pushed towards the panel background while a face is picked. The fork ghosts
// it fairly hard; a light ghost is enough to let the green read without hiding the part's shape.
constexpr float GHOST_MIX = 0.62f;

// Brush reach, in world mm, that the panel offers. Small enough to stay inside one engraved mark,
// so the local surface the fill is measured against stays local to it.
constexpr double FILL_RADIUS_MIN = 0.5;
constexpr double FILL_RADIUS_MAX = 50.0;

// [ORCAPORT:HF-3] Extrude-face limits, in mm. The distance is signed (positive grows out of the face,
// negative cuts into it); the offset may be negative
// (it shrinks the outline) as long as the shape survives, which extrude_outline() checks.
// [ORCAPORT:HF-4] How far the picked face is grown, in mm. SIGNED: positive grows a solid out of the
// face, negative cuts a pocket into it. The negative side only has to reach through a real part, which
// is what fixes its bound; the typed field beside the slider is what sets a precise value.
constexpr double EXTRUDE_DISTANCE_MIN = -50.0;
constexpr double EXTRUDE_DISTANCE_MAX = 200.0;
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
    m_preview_hover.reset();
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

// ---------------------------------------------------------------------------------------------
// [ORCAPORT:HF-3] Extrude face: pick, cache, prism
// ---------------------------------------------------------------------------------------------

// Facet normals + neighbours are O(mesh) and the hover asks for them on every facet change, so they
// are cached for the volume in play. The stamp is the mesh's own size: a volume whose mesh is
// rebuilt to the same vertex and triangle counts is the one case this misses, and it is rare enough
// (and harmless: the region would just be computed with stale normals until the cursor moves again).
const std::vector<Vec3f>* GLGizmoExtrude::cached_normals(const ModelVolume *mv) const
{
    if (mv == nullptr)
        return nullptr;
    const indexed_triangle_set &its = mv->mesh().its;
    if (m_cache_mv != mv || m_cache_verts != its.vertices.size() || m_cache_tris != its.indices.size()) {
        m_cache_mv        = mv;
        m_cache_verts     = its.vertices.size();
        m_cache_tris      = its.indices.size();
        m_cache_normals   = its_face_normals(its);
        m_cache_neighbors = its_face_neighbors(its);
    }
    return &m_cache_normals;
}

const std::vector<Vec3i32>* GLGizmoExtrude::cached_neighbors(const ModelVolume *mv) const
{
    if (cached_normals(mv) == nullptr)
        return nullptr;
    return &m_cache_neighbors;
}

// Turn a hovered facet into a PICK: the flat frame, the region's outline and its holes. This is the
// expensive half, and it runs once per pick - not per frame, and not per slider drag.
bool GLGizmoExtrude::make_pick(const Hover &hover, Pick *out) const
{
    *out = Pick();
    if (! hover.valid || hover.mv == nullptr || hover.facet < 0)
        return false;

    const std::vector<Vec3f>   *normals   = cached_normals(hover.mv);
    const std::vector<Vec3i32> *neighbors = cached_neighbors(hover.mv);
    if (normals == nullptr || neighbors == nullptr || hover.facet >= int(normals->size())
        || int(normals->size()) != int(hover.mv->mesh().its.indices.size()))
        return false;

    // The picked facet's normal in object space. Normals do not transform with the matrix itself, so
    // the inverse transpose of the volume's linear part is the right thing.
    const Transform3d to_obj  = hover.mv->get_matrix();
    const Matrix3d    nrm_mat = to_obj.linear().inverse().transpose();
    Vec3d             obj_n   = nrm_mat * (*normals)[size_t(hover.facet)].cast<double>();
    const double      nl      = obj_n.norm();
    if (nl < 1e-12)
        return false;
    obj_n /= nl;

    // A pure rotation (nothing is distorted) that brings the face normal to +Z, so the extrusion is
    // built exactly as it would be on a top face and turned back afterwards.
    const Eigen::Quaterniond q_flat = Eigen::Quaterniond::FromTwoVectors(obj_n, Vec3d::UnitZ());
    Transform3d              to_flat = Transform3d::Identity();
    to_flat.linear() = q_flat.toRotationMatrix();

    // ~2.6 deg: the region stops where the surface stops being that plane.
    constexpr float kPickedFaceCos = 0.999f;
    const std::vector<int> region = SurfaceExtrude::coplanar_region(*normals, *neighbors, hover.facet, kPickedFaceCos);
    double     face_z = 0.;
    ExPolygons outline = SurfaceExtrude::region_outline(hover.mv->mesh().its, region, to_flat * to_obj, &face_z);
    if (outline.empty())
        return false;

    out->valid   = true;
    out->mv      = hover.mv;
    out->facet   = hover.facet;
    out->to_flat = to_flat;
    out->outline = std::move(outline);
    out->holes   = SurfaceExtrude::outline_holes(out->outline);   // cheap, and Holes mode wants it
    out->face_z  = face_z;
    return true;
}

bool GLGizmoExtrude::build_pick(const Hover &hover)
{
    return make_pick(hover, &m_pick);
}

void GLGizmoExtrude::invalidate_pick()
{
    m_pick = Pick();
    m_preview_dirty = true;
}

// The prism for the current pick and parameters, in OBJECT space (the same space cavity-fill's plug
// comes back in, so add_named_fill treats both modes identically).
indexed_triangle_set GLGizmoExtrude::picked_prism(std::string *why) const
{
    auto fail = [&](const char *msg) -> indexed_triangle_set {
        if (why != nullptr)
            *why = msg;
        return {};
    };

    if (! m_pick.valid)
        return fail(_u8L("Click a flat face of the object.").c_str());
    const ExPolygons &outline = m_extrude_holes ? m_pick.holes : m_pick.outline;
    if (outline.empty())
        return fail(_u8L("That face has no holes.").c_str());

    // Out of the face, or half out and half into the model. `face_z` grows OUT of the face, so a
    // positive distance starts at the face and a negative one starts inside the model - the same
    // prism either way, and the sign alone decides whether it is added as a solid or as a cut (see
    // create_from_pick). Both directions straddles the face, which the sign cannot change, so it
    // takes the magnitude.
    const double d = m_extrude_distance;
    if (std::abs(d) < 1e-9)
        return fail(_u8L("Set a distance: a positive one grows out of the face, a negative one cuts into it.").c_str());
    const double h  = std::abs(d);
    const double z0 = m_extrude_both ? m_pick.face_z - 0.5 * h : m_pick.face_z + std::min(d, 0.);
    indexed_triangle_set out = SurfaceExtrude::extrude_outline(outline, m_extrude_offset, z0, h);
    if (out.indices.empty())
        return fail(_u8L("With that offset nothing is left of the shape.").c_str());
    its_transform(out, m_pick.to_flat.inverse());
    return out;
}

// The outline as a flat surface on the face: a hair-thick slab, so it lies on the surface without
// z-fighting with it. Used for both the hover (teal) and the pick (green).
indexed_triangle_set GLGizmoExtrude::face_slab(const ExPolygons &outline, const Transform3d &to_flat, double face_z) const
{
    if (outline.empty())
        return {};
    indexed_triangle_set out = SurfaceExtrude::extrude_outline(outline, 0., face_z, 0.02);
    if (out.indices.empty())
        return {};
    its_transform(out, to_flat.inverse());
    return out;
}

// ---------------------------------------------------------------------------------------------
// [ORCAPORT:HF-3] Ghost: dim the object while a face is picked
// ---------------------------------------------------------------------------------------------

void GLGizmoExtrude::restore_ghost()
{
    // 🚨 Restore FIRST, always - see the note on apply_ghost(). This also runs on gizmo close, so it
    // must not depend on get_state().
    for (GLVolume *v : m_parent.get_volumes().volumes) {
        if (v == nullptr)
            continue;
        auto it = m_saved_colors.find(std::make_tuple(v->object_idx(), v->volume_idx(), v->instance_idx()));
        if (it != m_saved_colors.end())
            v->set_color(it->second);
    }
    m_saved_colors.clear();
}

void GLGizmoExtrude::apply_ghost()
{
    // 🚨 Restore first. Without it a second call would save an already-tinted colour as "the
    // original" and the object would fade a little more on every refresh, down to nothing.
    restore_ghost();

    if (get_state() != On || m_mode != Mode::ExtrudeFace || ! m_pick.valid)
        return;

    const ModelObject *mo = model_object();
    if (mo == nullptr)
        return;

    // Push every volume of this object towards a dark grey and let it go translucent, so the green
    // face and the orange solid are what the eye lands on.
    const ColorRGBA dim(0.16f, 0.18f, 0.20f, 1.f);
    for (GLVolume *v : m_parent.get_volumes().volumes) {
        if (v == nullptr || v->object_idx() < 0)
            continue;
        const Model &model = wxGetApp().plater()->model();
        if (v->object_idx() >= int(model.objects.size()) || model.objects[size_t(v->object_idx())] != mo)
            continue;
        const auto key = std::make_tuple(v->object_idx(), v->volume_idx(), v->instance_idx());
        m_saved_colors.emplace(key, v->color);
        // ColorRGBA's components are accessor functions, not fields.
        ColorRGBA c = v->color;
        c.r(c.r() * (1.f - GHOST_MIX) + dim.r() * GHOST_MIX);
        c.g(c.g() * (1.f - GHOST_MIX) + dim.g() * GHOST_MIX);
        c.b(c.b() * (1.f - GHOST_MIX) + dim.b() * GHOST_MIX);
        c.a(std::min(c.a(), 0.55f));
        v->set_color(c);
    }
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
    m_preview_hover.reset();
    m_preview_pick.reset();
    m_preview_prism.reset();
    m_preview_applied.reset();

    if (m_mode == Mode::ExtrudeFace) {
        // [ORCAPORT:HF-3] Three overlays, in the fork's colours:
        //   teal   - the face under the cursor, so a click's target is visible before you commit;
        //   green  - the face you picked;
        //   orange - the solid the current Distance/Offset would make.
        // The candidate is skipped when the cursor is already on the picked facet: the teal would sit
        // exactly under the green and only muddy it.
        if (m_hover.valid && ! (m_pick.valid && m_pick.mv == m_hover.mv && m_pick.facet == m_hover.facet)) {
            Pick candidate;
            if (make_pick(m_hover, &candidate)) {
                const indexed_triangle_set its = face_slab(candidate.outline, candidate.to_flat, candidate.face_z);
                if (! its.indices.empty()) {
                    m_preview_hover.model.init_from(its);
                    m_preview_hover.model.set_color(HOVER_COLOR);
                }
            }
        }
        if (m_pick.valid) {
            const indexed_triangle_set slab = face_slab(m_pick.outline, m_pick.to_flat, m_pick.face_z);
            if (! slab.indices.empty()) {
                m_preview_pick.model.init_from(slab);
                m_preview_pick.model.set_color(PICK_COLOR);
            }
            // The reason is cached here, with the prism: the panel reads it instead of rebuilding.
            const indexed_triangle_set prism = picked_prism(&m_pick_why);
            if (! prism.indices.empty()) {
                m_preview_prism.model.init_from(prism);
                m_preview_prism.model.set_color(PRISM_COLOR);
            }
        } else {
            m_pick_why.clear();
        }
    } else {
        // Cavity fill, unchanged: the plug for the stroke, or for the face under the cursor unless it
        // already carries one (an applied plug is drawn in the applied overlay instead of here).
        std::vector<Hover> seeds;
        if (m_painting && ! m_stroke.empty())
            seeds = m_stroke;
        else if (m_hover.valid && m_hover_applied < 0)
            seeds.push_back(m_hover);
        if (! seeds.empty()) {
            const indexed_triangle_set its = fill_mesh(seeds);
            if (! its.indices.empty()) {
                m_preview_pick.model.init_from(its);
                m_preview_pick.model.set_color(PICK_COLOR);
            }
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

void GLGizmoExtrude::add_named_fill(const std::string &name, const indexed_triangle_set &its, const std::string &action,
                                    bool negative)
{
    ModelObject *mo = model_object();
    const int    oi = object_idx_of(m_parent.get_selection(), mo);
    if (mo == nullptr || oi < 0 || its.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    plater->take_snapshot(action);
    indexed_triangle_set copy = its;
    // A negative distance adds a CUT, the same NEGATIVE_VOLUME the other fork gizmos use; it is named
    // like any other extrude, so is_fill_volume() still claims it (applied count, red preview, removal).
    ModelVolume *v = mo->add_volume(TriangleMesh(std::move(copy)),
                                    negative ? ModelVolumeType::NEGATIVE_VOLUME : ModelVolumeType::MODEL_PART, false);
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

void GLGizmoExtrude::set_extrude_distance(double d)
{
    m_extrude_distance = std::clamp(d, EXTRUDE_DISTANCE_MIN, EXTRUDE_DISTANCE_MAX);
    m_preview_dirty    = true;
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

// [ORCAPORT:HF-3] Clicking a face PICKS it. Nothing is added until create_from_pick(), so Distance and
// Offset can be dialled in against a live preview first - the release note's workflow.
bool GLGizmoExtrude::pick_at(const Vec2d &screen_pos)
{
    update_hover(screen_pos);
    if (! m_hover.valid || ! build_pick(m_hover))
        return false;
    m_preview_dirty = true;
    apply_ghost();
    m_parent.set_as_dirty();
    return true;
}

void GLGizmoExtrude::clear_pick()
{
    if (! m_pick.valid)
        return;
    invalidate_pick();
    apply_ghost();   // restores first, and with no pick left it restores only
    m_parent.set_as_dirty();
}

bool GLGizmoExtrude::create_from_pick()
{
    if (m_mode != Mode::ExtrudeFace) {
        // Cavity fill keeps its own, stroke-based commit path (see commit_stroke()).
        if (! m_hover.valid || m_hover_applied >= 0)
            return false;
        const indexed_triangle_set its = fill_mesh(m_hover);
        if (its.indices.empty())
            return false;
        add_named_fill(feature_name(EXTRUDE_NAME, next_feature_id()), its, _u8L("Fill cavity"));
        return true;
    }
    if (! m_pick.valid)
        return false;
    const indexed_triangle_set its = picked_prism();
    if (its.indices.empty())
        return false;
    // The sign picks the volume type: a positive distance adds a solid, a negative one a cut. Both
    // directions always produces a solid that straddles the face.
    const bool cut = ! m_extrude_both && m_extrude_distance < 0.;
    add_named_fill(feature_name(EXTRUDE_NAME, next_feature_id()), its,
                   cut ? _u8L("Cut into face") : _u8L("Extrude face"), cut);
    // The pick is kept, so the same face can be extruded again with different values; the button and
    // right-click/Esc are the only ways in and out.
    m_preview_dirty = true;
    return true;
}

// Kept so MCP scripts written before the rework still work: it now picks the face under the cursor
// and commits it in one call, which is what it used to do.
bool GLGizmoExtrude::gizmo_apply_hovered()
{
    if (! m_hover.valid)
        return false;
    if (m_mode == Mode::ExtrudeFace)
        return build_pick(m_hover) && create_from_pick();
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

    // [ORCAPORT:HF-3] The panel's captions and hints are passed inline to the shared widgets in
    // GizmoStyle.hpp (the way Support Zones does it); only the count line needs a stored string, and
    // it reads differently in each mode.
    m_desc["applied_ext"] = _L("Extruded");
    m_desc["applied_cav"] = _L("Filled");
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
        // [ORCAPORT:HF-3] Drop the pick and put the object's colours back. Closing the gizmo with the
        // object still ghosted would leave the scene tinted for the rest of the session.
        invalidate_pick();
        restore_ghost();
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
        // [ORCAPORT:HF-3] The picked face belonged to the old mesh: a rebuilt volume list makes the
        // cached region meaningless, and the saved colours may not exist any more. Restore first.
        if (m_pick.valid) {
            invalidate_pick();
            restore_ghost();
        }
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
    // Paint order matters: depth test is off here, so later draws win. Hover underneath, then the
    // picked face, then the solid sitting on it (its inset leaves the green margin showing), then
    // every fill already committed.
    m_preview_hover.model.render(shader);
    m_preview_pick.model.render(shader);
    m_preview_prism.model.render(shader);
    m_preview_applied.model.render(shader);

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
        // [ORCAPORT:HF-3] Extrude face: right-click drops the pick. Cavity fill: it removes the plug
        // the cursor is on, like the hole gizmo removes a hole.
        if (m_mode == Mode::ExtrudeFace) {
            clear_pick();
            return true;
        }
        gizmo_hover_at(m_parent.get_local_mouse_position());
        return gizmo_remove_hovered();
    }
    // [ORCAPORT:HF-3] Extrude face: a click PICKS the face, it does not extrude. Distance/Offset are
    // dialled in against the preview and the panel's Extrude button commits. Right-click drops the
    // pick instead of removing a fill.
    if (m_mode == Mode::ExtrudeFace) {
        if (! mouse_event.LeftUp())
            return false;
        if (! pick_at(m_parent.get_local_mouse_position()))
            return false;
        m_parent.request_extra_frame();
        return true;
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
    if (gizmo_panel_near_right_edge(x, last_input_window_width, m_parent.get_canvas_size().get_width(),
                                    m_parent.get_gizmos_manager().get_scaled_icons_size()))
        GizmoImguiSetNextWIndowPos(right, y, 0.f, 0.f, ImGuiCond_Always, 1.0f, 0.0f);
    else
        GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 0.0f, 0.0f);
#else
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
#endif
    // [ORCAPORT:HF-3] Fixed narrow width. The window used to auto-size to the longest hint and to the
    // numeric fields and came out about three times wider than the content needed; the hints now wrap
    // inside this width. SetNextWindowSize overrides AlwaysAutoResize on the axes it sets, so the
    // height still follows the content.
    ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f), ImGuiCond_Always);
    // El orden importa: push_toolbar_style empuja un WindowBg OPACO (COL_WINDOW_BG(_DARK) tiene
    // alfa 1.0) y el ultimo push es el que gana en el Begin. Va PRIMERO, y gizmo_push_window_style()
    // despues, o el panel pierde la transparencia de PanelBg que es parte del estilo: los paneles que
    // nunca empujaron el toolbar style (Align & Stack, Support Zones) si la tenian.
    ImGuiWrapper::push_toolbar_style(scale);
    gizmo_push_window_style();
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize | ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    gizmo_push_panel_style();

    // The widest caption the rows below use, so the label column is exact rather than a magic number.
    const float label_w = gizmo_label_w(_u8L("Both directions").c_str());

    // Tool mode: one icon, two jobs.
    {
        const std::string m0 = _u8L("Extrude face");
        const std::string m1 = _u8L("Cavity fill");
        const char       *labels[] = { m0.c_str(), m1.c_str() };
        const int         seg      = gizmo_segment("##mode", labels, 2, int(m_mode));
        if (seg >= 0)
            set_mode(seg == 0 ? Mode::ExtrudeFace : Mode::CavityFill);
    }

    ImGui::Separator();

    if (m_mode == Mode::ExtrudeFace) {
        // Face | Holes.
        {
            const std::string f0 = _u8L("Face");
            const std::string f1 = _u8L("Holes");
            const char       *labels[] = { f0.c_str(), f1.c_str() };
            const int         seg = gizmo_segment("##target", labels, 2, m_extrude_holes ? 1 : 0,
                                                  _u8L("Face extrudes the picked face; Holes extrudes only its holes, each one filled.").c_str());
            if (seg >= 0)
                set_extrude_holes(seg == 1);
        }

        ImGui::Separator();

        float h = float(m_extrude_distance);
        if (gizmo_row_slider("##eh", _u8L("Distance").c_str(), &h, float(EXTRUDE_DISTANCE_MIN), float(EXTRUDE_DISTANCE_MAX),
                             "%.2f", label_w,
                             _u8L("How far the new geometry reaches along the face normal, in mm: positive grows a solid out of the face, negative cuts a pocket into it. With Both directions on the sign is ignored and half goes each way.").c_str()))
            set_extrude_distance(h);

        float off = float(m_extrude_offset);
        if (gizmo_row_slider("##eo", _u8L("Offset").c_str(), &off, float(EXTRUDE_OFFSET_MIN), float(EXTRUDE_OFFSET_MAX),
                             "%.2f", label_w,
                             _u8L("Grows the outline before extruding; a negative value shrinks it. A negative offset is what leaves the green face showing around the solid.").c_str()))
            set_extrude_offset(off);

        bool both = m_extrude_both;
        if (gizmo_row_switch("##eb", _u8L("Both directions").c_str(), &both,
                             _u8L("Splits the distance: half grows out of the face, half goes into the model, so the solid passes through the face without being moved. The sign of the distance is ignored.").c_str()))
            set_extrude_both(both);

        // Nothing picked, a pick that cannot be extruded, or go.
        if (! m_pick.valid) {
            gizmo_hint(_u8L("Click a flat face of the object to pick it, then set the distance and the offset and press Extrude. Right-click or Esc drops the pick.").c_str());
        } else if (! m_pick_why.empty()) {
            gizmo_hint(m_pick_why.c_str(), GizmoCol::Warn);
        }

        // The primary action, right-aligned and in the accent colour, on its own line.
        if (gizmo_primary_button(_L("Extrude").utf8_str().data()))
            create_from_pick();
    } else {
        float radius = float(m_radius);
        if (gizmo_row_slider("##fr", _u8L("Brush width").c_str(), &radius, float(FILL_RADIUS_MIN), float(FILL_RADIUS_MAX),
                             "%.1f", label_w,
                             _u8L("How far the brush reaches, in mm. Small enough to stay inside one engraved mark, so the surface the plug is measured against stays local to it.").c_str()))
            set_radius(radius);

        // Left-dragging over a cavity already fills it (on_mouse), so there is no separate fill button.
        if (m_imgui->button(_L("Clear all")))
            gizmo_clear_all();

        if (m_hover.valid && m_hover_applied >= 0)
            gizmo_hint(_u8L("Already filled. Right-click it to remove the plug.").c_str());
        else if (m_hover.valid)
            gizmo_hint(_u8L("Depression under the cursor. Drag over an engraved mark to fill it.").c_str());
        else
            gizmo_hint(_u8L("Nothing to fill under the cursor.").c_str());
    }

    const wxString applied = wxString::Format("%s: %d",
        (m_mode == Mode::ExtrudeFace ? m_desc.at("applied_ext") : m_desc.at("applied_cav")).c_str(), applied_count());
    m_imgui->text(applied);

    gizmo_pop_panel_style();
    GizmoImguiEnd();
    gizmo_pop_window_style();
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
