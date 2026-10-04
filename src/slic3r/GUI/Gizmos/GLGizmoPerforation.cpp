#include "GLGizmoPerforation.hpp"
#include "GLGizmoUtils.hpp"

#include "libslic3r/AABBMesh.hpp"
#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/GUI_ObjectList.hpp"
#include "slic3r/GUI/ImGuiWrapper.hpp"
#include "slic3r/GUI/Plater.hpp"

#include <glad/gl.h>

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace Slic3r {
namespace GUI {

namespace {

const ColorRGBA HIGHLIGHT_COLOR(0.10f, 0.80f, 0.74f, 0.40f);
const ColorRGBA GHOST_COLOR(0.90f, 0.35f, 0.20f, 0.50f);

const char *const PERFORATION_NAME = "Perforation";

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
    m_shortcut_key = WXK_CONTROL_I;

    m_desc["pattern"]    = _L("Pattern");
    m_desc["spacing"]    = _L("Spacing");
    m_desc["width"]      = _L("Hole width");
    m_desc["boundary"]   = _L("Boundary");
    m_desc["angle"]      = _L("Angle");
    m_desc["invert"]     = _L("Invert (grooves)");
    m_desc["depth_hint"] = _L("Depth follows the face to the first surface behind it.");
    m_desc["forbidden"]  = _L("Only flat faces can be perforated.");
    m_desc["horizontal"] = _L("This face is near-horizontal: holes here bridge over air or leave gaps.");
    m_desc["reduced"]    = _L("Hole width too large for the spacing; reduced to keep a wall between holes.");
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
    GizmoImguiSetNextWIndowPos(x, y, ImGuiCond_Always, 1.0f, 0.0f);
    ImGuiWrapper::push_toolbar_style(scale);
    GizmoImguiBegin(get_name(), ImGuiWindowFlags_NoMove | ImGuiWindowFlags_AlwaysAutoResize |
                                    ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoTitleBar);

    // Fixed, independent ranges. The UI never clamps one value against another: the pattern
    // builder keeps a wall between holes on its own and the panel says so (the "reduced" note),
    // so dragging or typing a value always takes effect.
    constexpr double SPACING_MIN = 1.0,  SPACING_MAX = 25.0;
    constexpr double WIDTH_MIN   = 0.2,  WIDTH_MAX   = 10.0;
    constexpr double MARGIN_MIN  = 1.0,  MARGIN_MAX  = 25.0;
    constexpr double ANGLE_MIN   = 0.0,  ANGLE_MAX   = PERFORATION_MAX_ANGLE_DEG;

    const float slider_w = m_imgui->scaled(6.0f); // track only; the number input gets its own column
    const float input_w  = m_imgui->scaled(3.2f);
    // Widest label the panel can show, so no slider starts under its own text.
    float label_w = 0.f;
    for (const char *key : {"pattern", "spacing", "width", "boundary", "angle"})
        label_w = std::max(label_w, m_imgui->calc_text_size(m_desc.at(key)).x);
    label_w += m_imgui->scaled(1.5f);

    // A slider followed by a typed number input, in separate columns. The slider writes a
    // scratch and commits only when the drag ends (deactivated_after_edit), so dragging does not
    // rebuild the ghost every frame; the typed input commits on Enter. Neither clamps the other.
    m_editing = false;
    const auto slider_input = [&](const char *id, const char *input_id, const wxString &label, double &value,
                                  double lo, double hi, const char *fmt) {
        ImGui::AlignTextToFramePadding();
        m_imgui->text(label);
        ImGui::SameLine(label_w);
        ImGui::PushItemWidth(slider_w);
        float f = float(value);
        m_imgui->bbl_slider_float_style(id, &f, float(lo), float(hi), fmt, 1.0f, true);
        const ImGuiWrapper::LastSliderStatus &st = m_imgui->get_last_slider_status();
        if (st.deactivated_after_edit)
            value = f; // commit on release
        if (st.edited && !st.deactivated_after_edit)
            m_editing = true; // still dragging: defer the rebuild
        ImGui::PopItemWidth();
        ImGui::SameLine();
        ImGui::PushItemWidth(input_w);
        float g = float(value);
        if (ImGui::InputFloat(input_id, &g, 0.f, 0.f, fmt, ImGuiInputTextFlags_EnterReturnsTrue))
            value = g; // accept what was typed; the builder guards the geometry
        ImGui::PopItemWidth();
    };

    ImGui::AlignTextToFramePadding();
    m_imgui->text(m_desc.at("pattern"));
    ImGui::SameLine(label_w);
    ImGui::PushItemWidth(slider_w + input_w + ImGui::GetStyle().ItemSpacing.x);
    int         kind  = int(m_params.kind);
    const char *kinds[] = {"Lines", "Grid", "Honeycomb", "Circles"};
    if (ImGui::Combo("##kind", &kind, kinds, int(sizeof(kinds) / sizeof(kinds[0]))))
        m_params.kind = PerforationKind(kind);
    ImGui::PopItemWidth();

    slider_input("##spacing", "##spacing_in", m_desc.at("spacing"), m_params.spacing, SPACING_MIN, SPACING_MAX, "%.1f");
    slider_input("##width", "##width_in", m_desc.at("width"), m_params.width, WIDTH_MIN, WIDTH_MAX, "%.1f");
    slider_input("##boundary", "##boundary_in", m_desc.at("boundary"), m_params.margin, MARGIN_MIN, MARGIN_MAX, "%.1f");
    if (m_params.kind == PerforationKind::Lines || m_params.kind == PerforationKind::Grid)
        slider_input("##angle", "##angle_in", m_desc.at("angle"), m_params.angle_deg, ANGLE_MIN, ANGLE_MAX, "%.0f");

    const std::string invert_label = m_desc.at("invert").ToStdString();
    ImGui::Checkbox(invert_label.c_str(), &m_params.invert);

    ImGui::Separator();
    m_imgui->text(_L("Click a flat face to perforate it."));
    m_imgui->text(m_desc.at("depth_hint"));

    // The builder keeps a wall between holes whatever the numbers say. The notice explains a
    // preview that differs from the number, but it is only shown once the drag has settled and it
    // occupies a reserved row always, so toggling it never resizes the window (which would move
    // the slider under the cursor and loop).
    const bool reduced = m_params.width > perforation_effective_width(m_params.spacing, m_params.width) + 1e-6;
    const bool show_reduced    = reduced && !m_editing;
    const bool show_horizontal = m_hover_horizontal;
    ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(1.f, 0.7f, 0.2f, 1.f));
    ImGui::BeginGroup();
    m_imgui->text(show_reduced ? m_desc.at("reduced") : wxString());
    m_imgui->text(show_horizontal ? m_desc.at("horizontal") : wxString());
    ImGui::EndGroup();
    ImGui::PopStyleColor();

    GizmoImguiEnd();
}

} // namespace GUI
} // namespace Slic3r
