#include "GLGizmoPerforation.hpp"
#include "GLGizmoUtils.hpp"
#include "GizmoStyle.hpp" // [ORCAPORT:PP-1] fork panel style (docs/handoff.md section 28)

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

const ColorRGBA HIGHLIGHT_COLOR(0.10f, 0.80f, 0.74f, 0.40f);
const ColorRGBA GHOST_COLOR(0.90f, 0.35f, 0.20f, 0.50f);

const char *const PERFORATION_NAME = "Perforation";

// [ORCAPORT:PP-1] The four pattern glyphs. A flat square plate with the pattern cut through it,
// drawn straight into the draw list like the Support Zones glyphs, so it scales with the DPI without
// a resource. The plate and the shared margin live in GizmoStyle.hpp - the Holes operation glyphs
// are built on the same two.
constexpr float PP_PI_F = 3.14159265358979323846f;

// Lines - three parallel slots.
void pp_lines(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float x0 = p.x + kGizmoGlyphInset * s, x1 = p.x + (1.f - kGizmoGlyphInset) * s;
    const float h = 0.095f * s, r = 0.0475f * s;
    for (int i = 0; i < 3; ++i) {
        const float y = p.y + (0.2625f + 0.19f * float(i)) * s;
        dl->AddRectFilled(ImVec2(x0, y), ImVec2(x1, y + h), col, r);
    }
}

// Grid - two families of slots crossing, i.e. the intersections are the holes.
void pp_grid(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float a = kGizmoGlyphInset * s, b = (1.f - kGizmoGlyphInset) * s, t = 0.09f * s;
    const float u1 = 0.34f * s, u2 = 0.57f * s;
    dl->AddRectFilled(ImVec2(p.x + a, p.y + u1), ImVec2(p.x + b, p.y + u1 + t), col);
    dl->AddRectFilled(ImVec2(p.x + a, p.y + u2), ImVec2(p.x + b, p.y + u2 + t), col);
    dl->AddRectFilled(ImVec2(p.x + u1, p.y + a), ImVec2(p.x + u1 + t, p.y + b), col);
    dl->AddRectFilled(ImVec2(p.x + u2, p.y + a), ImVec2(p.x + u2 + t, p.y + b), col);
}

// Circles - four round holes.
void pp_circles(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float r = 0.1225f * s;
    for (int j = 0; j < 2; ++j)
        for (int i = 0; i < 2; ++i)
            dl->AddCircleFilled(ImVec2(p.x + (0.3325f + 0.335f * float(i)) * s,
                                       p.y + (0.3325f + 0.335f * float(j)) * s), r, col, 18);
}

// Honeycomb - a generated close-packed lattice, CLIPPED to the shared pattern box so the cells at the
// border are cut there instead of running into the plate.
void pp_honeycomb(ImDrawList *dl, const ImVec2 &p, float s, ImU32 col)
{
    gizmo_glyph_plate(dl, p, s, col);
    const float r_lat = 0.155f * s;
    // 🔑 The wall between cells is clamped to a minimum PIXEL width. As a fraction of the glyph it is
    // 0.019*s, which is 0.4 px on a 28 px button - and ImGui does not antialias two adjacent filled
    // polygons against each other, so that gap vanished and the comb stopped reading as cells at all:
    // it became a lattice of hairlines sitting on the plate. draw_glyph protects its strokes the same
    // way (max(1.2, s*0.075)). The floor is why this glyph looks the same at 28 px as it does at 40.
    const float wall   = std::max(1.1f, 0.019f * s);
    const float r_cell = std::max(r_lat - wall, 0.5f);
    const float xs = 1.732f * r_lat, ys = 1.5f * r_lat, off = 0.866f * r_lat;
    const ImVec2 clip0(p.x + kGizmoGlyphInset * s, p.y + kGizmoGlyphInset * s);
    const ImVec2 clip1(p.x + (1.f - kGizmoGlyphInset) * s, p.y + (1.f - kGizmoGlyphInset) * s);
    dl->PushClipRect(clip0, clip1, true);
    for (int j = -3; j <= 4; ++j) {
        const float cy = p.y + 0.5f * s + ys * float(j);
        for (int i = -3; i <= 3; ++i) {
            const float cx = p.x + 0.5f * s + xs * float(i) + (j % 2 != 0 ? off : 0.f);
            if (cx + r_cell < clip0.x || cx - r_cell > clip1.x || cy + r_cell < clip0.y || cy - r_cell > clip1.y)
                continue;
            ImVec2 pts[6];
            for (int k = 0; k < 6; ++k) {
                const float a = PP_PI_F / 180.f * (60.f * float(k) - 90.f);
                pts[k] = ImVec2(cx + r_cell * std::cos(a), cy + r_cell * std::sin(a));
            }
            dl->AddConvexPolyFilled(pts, 6, col);
        }
    }
    dl->PopClipRect();
}

} // namespace

GLGizmoPerforation::GLGizmoPerforation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id)
    : GLGizmoBase(parent, icon_filename, sprite_id)
{}

ModelObject *GLGizmoPerforation::model_object() const
{
    if (m_c == nullptr || m_c->selection_info() == nullptr)
        return nullptr;
    return m_c->selection_info()->model_object();
}

Transform3d GLGizmoPerforation::instance_matrix() const
{
    const ModelObject *mo = model_object();
    if (mo == nullptr || mo->instances.empty())
        return Transform3d::Identity();
    return mo->instances.front()->get_matrix();
}

bool GLGizmoPerforation::on_init()
{
    m_shortcut = Shortcut::GizmoPerforation; // [ORCAPORT:PP-1] registered in Shortcuts.cpp

    m_desc["pattern"]    = _L("Pattern");
    m_desc["spacing"]    = _L("Spacing");
    m_desc["width"]      = _L("Hole width");
    m_desc["boundary"]   = _L("Boundary");
    m_desc["angle"]      = _L("Angle");
    m_desc["invert"]     = _L("Invert (grooves)");
    m_desc["depth_hint"] = _L("Depth follows the face to the first surface behind it.");
    m_desc["click_hint"] = _L("Click a flat face to perforate it.");
    m_desc["forbidden"]  = _L("Only flat faces can be perforated.");
    m_desc["horizontal"] = _L("Near-horizontal face: holes may bridge or leave gaps.");
    m_desc["reduced"]    = _L("Hole width is too large for the spacing.");
    return true;
}

std::string GLGizmoPerforation::on_get_name() const { return _u8L("Perforations"); }

bool GLGizmoPerforation::on_is_activable() const
{
    return m_parent.get_selection().is_single_full_instance();
}

CommonGizmosDataID GLGizmoPerforation::on_get_requirements() const
{
    return CommonGizmosDataID(int(CommonGizmosDataID::SelectionInfo) | int(CommonGizmosDataID::ObjectClipper));
}

void GLGizmoPerforation::on_set_state()
{
    if (get_state() == On) {
        m_merged_dirty = true;
        m_parent.set_as_dirty();
    } else {
        clear_hover();
    }
}

void GLGizmoPerforation::data_changed(bool /*is_serializing*/)
{
    const ModelObject *mo = model_object();
    if (mo == nullptr) {
        m_merged_its.indices.clear();
        m_merged_its.vertices.clear();
        m_merged_dirty = true;
        clear_hover();
        return;
    }
    if (mo != m_old_model_object || int(mo->volumes.size()) != m_old_volume_count) {
        m_old_model_object = mo;
        m_old_volume_count = int(mo->volumes.size());
        m_merged_dirty     = true;
        m_parent.set_as_dirty();
    }
}

void GLGizmoPerforation::rebuild_merged_mesh()
{
    TriangleMesh mesh;
    const ModelObject *mo = model_object();
    if (mo != nullptr)
        for (const ModelVolume *v : mo->volumes) {
            if (v == nullptr || !v->is_model_part())
                continue;
            TriangleMesh part = v->mesh();
            part.transform(v->get_matrix());
            mesh.merge(part);
        }
    m_merged_its = mesh.its;
}

void GLGizmoPerforation::clear_hover()
{
    m_hover_mv     = nullptr;
    m_hover_facet  = -1;
    m_hover_horizontal = false;
    m_hover_region.clear();
    m_last_hit = Vec3d::Constant(1e30);
    m_face_highlight.reset();
    m_face_ghost.reset();
    m_parent.set_as_dirty();
}

// The picked face as a clean 2D polygon in the face plane: the coplanar region's triangles,
// projected onto the plane and unioned. The plane origin is `origin_obj` (object space).
ExPolygons GLGizmoPerforation::face_domain(const ModelVolume *mv, size_t facet, const Vec3d &normal_obj,
                                           const Vec3d &origin_obj)
{
    const std::vector<int> region = coplanar_region(mv, facet, m_face_cache);
    if (region.empty())
        return {};

    Vec3d ux, uy;
    face_plane_axes(normal_obj, ux, uy);

    const Transform3d to_obj = mv->get_matrix();
    Polygons          tris;
    tris.reserve(region.size());
    for (const int f : region) {
        const Vec3i32 &idx = mv->mesh().its.indices[f];
        Polygon        tri;
        for (int k = 0; k < 3; ++k) {
            const Vec3d v   = to_obj * mv->mesh().its.vertices[idx(k)].cast<double>();
            const Vec3d rel = v - origin_obj;
            tri.points.emplace_back(Point::new_scale(rel.dot(ux), rel.dot(uy)));
        }
        tris.emplace_back(std::move(tri));
    }
    return union_ex(tris);
}

bool GLGizmoPerforation::build_pattern_at(const Vec3d &hit_obj, const ModelVolume *mv, size_t facet,
                                          ExPolygons &pattern, indexed_triangle_set &prism)
{
    if (mv == nullptr)
        return false;

    const Vec3d normal_obj = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix());
    if (!normal_obj.allFinite() || normal_obj.norm() < 1e-9)
        return false;
    const Vec3d n = normal_obj.normalized();

    const ExPolygons domain = face_domain(mv, facet, n, hit_obj);
    if (domain.empty())
        return false;

    pattern = make_perforation_pattern(domain, m_params);
    if (pattern.empty())
        return false;

    // Depth: the first surface the inward ray crosses. That is the far side of the local wall
    // (a hollow shell) or the far side of the object (solid). Fall back to the whole object.
    double z_out = 0.5;
    double z_in  = -m_params.margin - 5.;
    if (!m_merged_its.indices.empty()) {
        const AABBMesh mesh(m_merged_its);
        const Vec3d    start = hit_obj - n * 1e-3;
        double         depth = 0.;
        for (const AABBMesh::hit_result &h : mesh.query_ray_hits(start, -n))
            if (h.is_hit() && h.distance() > 1e-3 && (depth == 0. || h.distance() < depth))
                depth = h.distance();
        if (depth > 0.) {
            z_in = -(depth + 0.5);
        } else {
            double lo = 0., hi = 0.;
            for (const stl_vertex &v : m_merged_its.vertices) {
                const double d = (v.cast<double>() - hit_obj).dot(n);
                lo             = std::min(lo, d);
                hi             = std::max(hi, d);
            }
            z_out = hi + 0.5;
            z_in  = lo - 0.5;
        }
    }

    Vec3d ux, uy;
    face_plane_axes(n, ux, uy);
    prism = extrude_perforation_pattern(pattern, hit_obj, ux, uy, n, z_in, z_out);
    return !prism.indices.empty();
}

void GLGizmoPerforation::update_hover()
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
        if (m_hover_mv != nullptr)
            clear_hover();
        return;
    }

    const Vec3d hit_obj      = instance_matrix().inverse() * hit;
    const bool  face_changed = (mv != m_hover_mv || int(facet) != m_hover_facet);
    if (!face_changed && (hit_obj - m_last_hit).norm() < 0.25)
        return;

    if (face_changed) {
        m_hover_mv     = mv;
        m_hover_facet  = int(facet);
        // A near-horizontal face (normal close to bed Z) means the holes bridge or leave gaps.
        const Vec3d n_obj = facet_normal_in_world(mv->mesh().its, int(facet), mv->get_matrix()).normalized();
        const Vec3d up    = instance_matrix().linear().inverse() * Vec3d::UnitZ();
        m_hover_horizontal =
            up.allFinite() && up.norm() > 1e-9 && n_obj.allFinite() && n_obj.norm() > 1e-9 &&
            std::abs(n_obj.dot(up.normalized())) > 0.9;
        m_hover_region = coplanar_region(mv, facet, m_face_cache);
        m_face_highlight.reset();
        if (!m_hover_region.empty()) {
            const indexed_triangle_set patch = build_coplanar_patch(mv, m_hover_region, m_face_cache, 0.05f);
            if (!patch.indices.empty()) {
                m_face_highlight.init_from(patch);
                m_face_highlight.set_color(HIGHLIGHT_COLOR);
            }
        }
    }
    m_last_hit = hit_obj;

    // Do not rebuild the ghost while a slider is being dragged: the prism is a full mesh and
    // rebuilding it every frame flickers. The parameter is committed on release and the ghost
    // catches up on the next pass (m_last_hit then differs, so this runs).
    if (m_editing)
        return;

    ExPolygons            pattern;
    indexed_triangle_set  prism;
    m_face_ghost.reset();
    if (build_pattern_at(hit_obj, mv, facet, pattern, prism) && !prism.indices.empty()) {
        m_face_ghost.init_from(prism);
        m_face_ghost.set_color(GHOST_COLOR);
    }
    m_parent.set_as_dirty();
}

void GLGizmoPerforation::set_params(const PerforationParams &p)
{
    m_params = p;
    m_parent.set_as_dirty();
}

bool GLGizmoPerforation::gizmo_apply_at(const Vec2d &screen_pos)
{
    return apply_at(screen_pos);
}

bool GLGizmoPerforation::gizmo_face_info_at(const Vec2d &screen_pos, int &facet, int &region_facets, Vec3d &normal)
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

bool GLGizmoPerforation::apply_at(const Vec2d &screen_pos)
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

    const Vec3d hit_obj = instance_matrix().inverse() * hit;
    ExPolygons            pattern;
    indexed_triangle_set  prism;
    if (!build_pattern_at(hit_obj, mv, facet, pattern, prism) || prism.indices.empty())
        return false;

    add_pattern_volume(prism);
    return true;
}

void GLGizmoPerforation::add_pattern_volume(const indexed_triangle_set &prism)
{
    ModelObject *mo = model_object();
    const int    oi = m_parent.get_selection().get_object_idx();
    if (mo == nullptr || oi < 0 || prism.indices.empty())
        return;

    Plater *plater = wxGetApp().plater();
    if (plater == nullptr)
        return;
    plater->take_snapshot(_u8L("Perforate face"));

    indexed_triangle_set copy = prism;
    ModelVolume        *v     = mo->add_volume(TriangleMesh(std::move(copy)), ModelVolumeType::NEGATIVE_VOLUME, false);
    v->name                   = PERFORATION_NAME;
    if (ObjectList *ol = wxGetApp().obj_list()) {
        ol->add_volumes_to_object_in_list(oi);
        ol->update_info_items(oi);
    }
    plater->update();
}

bool GLGizmoPerforation::on_mouse(const wxMouseEvent &mouse_event)
{
    if (mouse_event.LeftDown() && apply_at(m_parent.get_local_mouse_position()))
        return true;
    return false;
}

void GLGizmoPerforation::on_render()
{
    if (get_state() != On)
        return;
    if (m_merged_dirty) {
        rebuild_merged_mesh();
        m_merged_dirty = false;
    }
    update_hover();

    GLShaderProgram *shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    const Camera  &camera = wxGetApp().plater()->get_camera();
    const Transform3d view = camera.get_view_matrix();

    shader->start_using();
    glsafe(::glEnable(GL_DEPTH_TEST));
    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());

    if (m_hover_mv != nullptr && m_face_highlight.is_initialized()) {
        // The highlight is built from the volume's own mesh, so it carries the volume matrix.
        shader->set_uniform("view_model_matrix", view * instance_matrix() * m_hover_mv->get_matrix());
        m_face_highlight.render();
    }
    if (m_face_ghost.is_initialized()) {
        // The ghost is built in object space, like the negative volume it will become.
        shader->set_uniform("view_model_matrix", view * instance_matrix());
        m_face_ghost.render();
    }

    glsafe(::glDisable(GL_BLEND));
    glsafe(::glEnable(GL_CULL_FACE));
    shader->stop_using();
}

void GLGizmoPerforation::on_render_input_window(float x, float y, float bottom_limit)
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
    // [ORCAPORT:PP-1] Fork panel style (docs/handoff.md section 28): the shared palette, one fixed
    // narrow width with wrapped hints, and the shared widget layer. The panel used to measure its
    // own width from its controls; measure it once here instead, so no string can move it.
    ImGui::SetNextWindowSize(ImVec2(gizmo_panel_width(), 0.f), ImGuiCond_Always);
    gizmo_push_window_style();
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);
    gizmo_push_panel_style();
    render_panel_body();
    gizmo_pop_panel_style();
    GizmoImguiEnd();
    ImGuiWrapper::pop_toolbar_style();
    gizmo_pop_window_style();
}

void GLGizmoPerforation::render_panel_body()
{
    // Fixed, independent ranges. The UI never clamps one value against another: the geometry uses
    // exactly what is set, and the panel only warns when a wall gets too thin, so dragging or
    // typing a value always takes effect.
    constexpr double SPACING_MIN = 1.0,  SPACING_MAX = 25.0;
    constexpr double WIDTH_MIN   = 0.2,  WIDTH_MAX   = 10.0;
    constexpr double MARGIN_MIN  = 1.0,  MARGIN_MAX  = 25.0;
    constexpr double ANGLE_MIN   = 0.0,  ANGLE_MAX   = PERFORATION_MAX_ANGLE_DEG;

    // Pattern: the four choices as icons with the name on hover (section 28 records the icon button
    // standard). Drawn without a caption, like the Extrude panel's own mode row.
    {
        void (*const drawers[4])(ImDrawList *, const ImVec2 &, float, ImU32) =
            {pp_lines, pp_grid, pp_honeycomb, pp_circles};
        // The names are the ones the old dropdown used, untranslated then and now: they are the
        // tooltip, and turning them into msgids is a translation pass of its own.
        const char *const names[4] = {"Lines", "Grid", "Honeycomb", "Circles"};
        for (int i = 0; i < 4; ++i) {
            if (i > 0)
                ImGui::SameLine(0.f, ImGui::GetStyle().ItemSpacing.x);
            if (gizmo_icon_toggle(names[i], int(m_params.kind) == i, names[i], drawers[i]))
                m_params.kind = PerforationKind(i);
        }
    }

    ImGui::Separator();

    // The widest caption the rows below use, so the label column is exact (section 28). Max over the
    // captions actually drawn in the column: measuring one of them is how "Hole width" ended up
    // clipped to "Hole wi".
    float label_w = 0.f;
    for (const char *key : {"spacing", "width", "boundary", "angle"})
        label_w = std::max(label_w, ImGui::CalcTextSize(m_desc.at(key).utf8_str().data()).x);
    label_w += ImGui::GetStyle().ItemSpacing.x * 0.5f;

    // A slider with a typed field beside it, and NO [-][+] step buttons: the buttons cost about a
    // third of the width and the field still takes a typed value, clamped to the same range.
    float spacing = float(m_params.spacing);
    if (gizmo_row_slider("##spacing", m_desc.at("spacing").utf8_str().data(), &spacing,
                         float(SPACING_MIN), float(SPACING_MAX), "%.1f", label_w, nullptr))
        m_params.spacing = double(spacing);

    float width = float(m_params.width);
    if (gizmo_row_slider("##width", m_desc.at("width").utf8_str().data(), &width,
                         float(WIDTH_MIN), float(WIDTH_MAX), "%.1f", label_w,
                         _u8L("Hole width, in mm. Keep it smaller than the spacing or the walls between the holes disappear.").c_str()))
        m_params.width = double(width);

    float margin = float(m_params.margin);
    if (gizmo_row_slider("##boundary", m_desc.at("boundary").utf8_str().data(), &margin,
                         float(MARGIN_MIN), float(MARGIN_MAX), "%.1f", label_w,
                         _u8L("How far the pattern keeps away from the edge of the picked face, in mm.").c_str()))
        m_params.margin = double(margin);

    if (m_params.kind == PerforationKind::Lines || m_params.kind == PerforationKind::Grid) {
        float angle = float(m_params.angle_deg);
        if (gizmo_row_slider("##angle", m_desc.at("angle").utf8_str().data(), &angle,
                             float(ANGLE_MIN), float(ANGLE_MAX), "%.0f", label_w, nullptr))
            m_params.angle_deg = double(angle);
    }

    bool invert = m_params.invert;
    if (gizmo_row_switch("##invert", m_desc.at("invert").utf8_str().data(), &invert,
                         _u8L("Cuts raised ridges instead of holes.").c_str()))
        m_params.invert = invert;

    ImGui::Separator();
    gizmo_hint(m_desc.at("click_hint").utf8_str().data());
    gizmo_hint(m_desc.at("depth_hint").utf8_str().data());

    // Advisory only: the geometry uses the width exactly as set. Amber is reserved for "something
    // is off" and nothing else in this panel uses it (section 28).
    const bool show_reduced    = (m_params.spacing - m_params.width) < PERFORATION_MIN_WALL - 1e-6;
    const bool show_horizontal = m_hover_horizontal;
    if (show_reduced)
        gizmo_hint(m_desc.at("reduced").utf8_str().data(), GizmoCol::Warn);
    if (show_horizontal)
        gizmo_hint(m_desc.at("horizontal").utf8_str().data(), GizmoCol::Warn);

    // The ghost is not rebuilt while an item is being edited, so the preview cannot flicker and
    // the walls-too-thin notice settles after the drag (the slider status this used to read went
    // away with the stock slider; "some item in this panel is active" is the same condition).
    m_editing = ImGui::IsAnyItemActive();
}

} // namespace GUI
} // namespace Slic3r
