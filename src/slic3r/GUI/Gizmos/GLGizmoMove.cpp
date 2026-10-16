#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
#include "slic3r/GUI/Shortcuts.hpp"
//BBS: GUI refactor
#include "slic3r/GUI/Plater.hpp"
// ORCAPORT: face-aligned move frame
#include "GLGizmosCommon.hpp"
#include "libslic3r/CutUtils.hpp"


#include <cassert>
#include <array>
#include <cmath>
#include <glad/gl.h>

#include <string>
#include "slic3r/GUI/Gizmos/GLGizmoBase.hpp"
#include "slic3r/GUI/Selection.hpp"
#include "libslic3r/Point.hpp"
#include "libslic3r/libslic3r.h"
#include "slic3r/GUI/GUI_Geometry.hpp"
#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/OpenGLManager.hpp"
#include <utility>
#include "slic3r/GUI/GLShader.hpp"
#include "libslic3r/Geometry.hpp"
#include <wx/intl.h>
#include "libslic3r/Model.hpp"
#include <wx/string.h>
#include <wx/utils.h>
#include "libslic3r/Color.hpp"
#include "libslic3r/Line.hpp"
#include "slic3r/GUI/Camera.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include "slic3r/GUI/Gizmos/GizmoObjectManipulation.hpp"
#include "slic3r/GUI/Gizmos/GLGizmoAlignment.hpp"
#include "libslic3r/BoundingBox.hpp"
#include <algorithm>
#include <cstddef>
#include <vector>
#include "slic3r/GUI/I18N.hpp"

namespace Slic3r {
namespace GUI {

#if ENABLE_FIXED_GRABBER
const double GLGizmoMove3D::Offset = 50.0;
#else
const double GLGizmoMove3D::Offset = 10.0;
#endif

//BBS: GUI refactor: add obj manipulation
GLGizmoMove3D::GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation)
    : GLGizmoBase(parent, icon_filename, sprite_id)
    //BBS: GUI refactor: add obj manipulation
    , m_object_manipulation(obj_manipulation)
{
    // ORCAPORT: face-aligned move frame
    if (m_object_manipulation)
        m_object_manipulation->m_move_window_extra_ui = [this]() { render_extra_move_ui(); };
}

std::string GLGizmoMove3D::get_tooltip() const
{
    const Selection& selection = m_parent.get_selection();
    bool show_position = selection.is_single_full_instance();
    const Vec3d& position = selection.get_bounding_box().center();

    if (m_hover_id == 0 || m_grabbers[0].dragging)
        return "X: " + format(show_position ? position(0) : m_displacement(0), 2);
    else if (m_hover_id == 1 || m_grabbers[1].dragging)
        return "Y: " + format(show_position ? position(1) : m_displacement(1), 2);
    else if (m_hover_id == 2 || m_grabbers[2].dragging)
        return "Z: " + format(show_position ? position(2) : m_displacement(2), 2);
    else
        return "";
}

bool GLGizmoMove3D::on_mouse(const wxMouseEvent &mouse_event) {
    // ORCAPORT: face-aligned move frame. The placing click belongs to the pick session: if its release
    // were left to the canvas it would run the "left-up on empty space -> deselect all" path and close
    // the gizmo - and the snap coordinates sit on silhouette edges, where the hover raycast that gates
    // that path is a coin flip.
    //
    // Motion is deliberately NOT consumed: the canvas's own move branch is what marks the overlay
    // dirty, and on_idle renders nothing when neither dirty flag is set. Swallowing it stopped the
    // scene re-rendering, which killed the hover highlight and the snap markers.
    if (m_pick_click_pending) {
        if (mouse_event.LeftUp() || mouse_event.Leaving())
            m_pick_click_pending = false;
        return !mouse_event.Moving();
    }
    if (m_pick_face_mode) {
        if (mouse_event.LeftDown()) {
            const wxPoint p = mouse_event.GetPosition();
            pick_face_at(Vec2d(double(p.x), double(p.y)));
            m_pick_face_mode     = false;
            m_pick_click_pending = true;
            return true;
        }
        if (mouse_event.RightDown()) {
            m_pick_face_mode = false;
            m_face_highlight.reset();
            m_hover_volume = nullptr;
            m_hover_facet  = -1;
            clear_pivot_candidates();
            m_parent.set_as_dirty();
            return true;
        }
        return false;
    }
    return use_grabbers(mouse_event);
}

bool GLGizmoMove3D::render_follows_cursor() const {
    // ORCAPORT: keep the cursor style update while picking a face
    return get_state() == On && m_pick_face_mode;
}

void GLGizmoMove3D::data_changed(bool is_serializing) {
    m_grabbers[2].enabled = !m_parent.get_selection().is_wipe_tower();
    change_cs_by_selection();
}

bool GLGizmoMove3D::on_init()
{
    for (int i = 0; i < 3; ++i) {
        m_grabbers.push_back(Grabber());
        m_grabbers.back().extensions = GLGizmoBase::EGrabberExtension::PosZ;
    }

    m_grabbers[0].angles = { 0.0, 0.5 * double(PI), 0.0 };
    m_grabbers[1].angles = { -0.5 * double(PI), 0.0, 0.0 };

    m_shortcut = Shortcut::GizmoMove;

    return true;
}

std::string GLGizmoMove3D::on_get_name() const
{
    if (!on_is_activable() && m_state == EState::Off) {
        return _u8L("Move") + ":\n" + _u8L("Please select at least one object.");
    } else {
        return _u8L("Move");
    }
}

bool GLGizmoMove3D::on_is_activable() const
{
    return !m_parent.get_selection().is_empty();
}

void GLGizmoMove3D::on_set_state() {
    if (get_state() == On) {
        m_last_selected_obejct_idx = -1;
        m_last_selected_volume_idx = -1;
        clear_face_frame();
        change_cs_by_selection();
    }
    if (m_object_manipulation)
        m_object_manipulation->reset_align_state();
}

void GLGizmoMove3D::on_start_dragging()
{
    assert(m_hover_id != -1);

    m_displacement = Vec3d::Zero();
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    m_starting_drag_position = m_grabbers[m_hover_id].matrix * m_grabbers[m_hover_id].center;
    // ORCAPORT: in face mode the drag runs along the face axis, not from the selection center
    if (m_face_frame_active)
        m_starting_box_center = m_starting_drag_position - face_frame_world().linear().col(m_hover_id).normalized();
    else
        m_starting_box_center = box.center();
    m_starting_box_bottom_center = box.center();
    m_starting_box_bottom_center(2) = box.min(2);

    // ORCAPORT:MV-3 the pivot (the gizmo origin) lands on another object's coordinate while Alt is
    // held. Its drag-start world position is fixed for the rest of the drag, because the drag
    // displacement is measured from the drag-start cache (see apply_pivot_snap).
    clear_pivot_snap();
    m_parent.clear_objsnap_markers();
    m_snap_pivot_start_world = gizmo_base_matrix().translation();
    m_snap_drag_latched      = true;
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_parent.do_move(L("Gizmo-Move"));
    m_displacement = Vec3d::Zero();
    m_snap_drag_latched = false;
    clear_pivot_snap();
}

void GLGizmoMove3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);

    // [ORCAPORT:MV-3] Alt held: override the axis displacement so the placed pivot lands on the
    // nearest snap coordinate of another object (see apply_pivot_snap).
    if (wxGetKeyState(WXK_ALT))
        apply_pivot_snap(data.mouse_pos.cast<double>());
    else
        clear_pivot_snap();

    Selection &selection = m_parent.get_selection();
    TransformationType trafo_type;
    trafo_type.set_relative();
    // ORCAPORT: move along the picked face axes (world-space displacement)
    if (m_face_frame_active) {
        selection.translate(face_frame_world().linear() * m_displacement, trafo_type);
        return;
    }
    switch (wxGetApp().obj_manipul()->get_coordinates_type())
    {
    case ECoordinatesType::Instance: { trafo_type.set_instance(); break; }
    case ECoordinatesType::Local: { trafo_type.set_local(); break; }
    default: { break; }
    }
    selection.translate(m_displacement, trafo_type);
}

void GLGizmoMove3D::on_render()
{
    const Selection& selection = m_parent.get_selection();

    glsafe(::glClear(GL_DEPTH_BUFFER_BIT));
    glsafe(::glEnable(GL_DEPTH_TEST));

    render_align_preview();

    const auto &[ref_box, box_trafo]    = selection.get_bounding_box_in_current_reference_system();
    Transform3d  base_matrix = m_face_frame_active ? face_frame_world() : box_trafo;
    BoundingBoxf3 render_box = ref_box;
    // ORCAPORT: face-aligned move frame
    if (m_face_frame_active) {
        const Transform3d inv = base_matrix.inverse();
        const BoundingBoxf3 world_box = selection.get_bounding_box();
        BoundingBoxf3 local_box;
        for (int c = 0; c < 8; ++c) {
            const Vec3d corner((c & 1) ? world_box.max.x() : world_box.min.x(),
                               (c & 2) ? world_box.max.y() : world_box.min.y(),
                               (c & 4) ? world_box.max.z() : world_box.min.z());
            local_box.merge(inv * corner);
        }
        render_box = local_box;
    }
    m_bounding_box                  = render_box;
    m_center                        = base_matrix.translation();
    if (m_object_manipulation) {
        m_object_manipulation->cs_center = m_center;
    }
    float space_size = 20.f *INV_ZOOM;

    for (int i = 0; i < 3; ++i) {
        m_grabbers[i].matrix = base_matrix;
    }

    const Vec3d zero = Vec3d::Zero();

    // ORCAPORT: the face frame's origin is the picked coordinate, not the box centre, so the stock
    // "box.max" arrow length collapses on the axis the origin already sits at the end of (usually the
    // face normal). The half-extent equals box.max when the frame is centred and stays proportional to
    // the object otherwise; the floor keeps a thin axis grabbable.
    Vec3d arrow = render_box.max;
    if (m_face_frame_active) {
        const Vec3d extent = render_box.max - render_box.min;
        arrow = (0.5 * extent).cwiseMax(Vec3d::Constant(0.1 * extent.maxCoeff()));
    }

    // x axis
    m_grabbers[0].center = {arrow.x() + space_size, 0, 0};
    // y axis
    m_grabbers[1].center = {0, arrow.y() + space_size,0};
    // z axis
    m_grabbers[2].center = {0,0, arrow.z() + space_size};

    for (int i = 0; i < 3; ++i) {
        m_grabbers[i].color       = AXES_COLOR[i];
        m_grabbers[i].hover_color = AXES_HOVER_COLOR[i];
    }

#if !SLIC3R_OPENGL_ES
    if (!OpenGLManager::get_gl_info().is_core_profile())
        glsafe(::glLineWidth((m_hover_id != -1) ? 2.0f : 1.5f));
#endif // !SLIC3R_OPENGL_ES

    auto render_grabber_connection = [this, &zero](unsigned int id) {
        if (m_grabbers[id].enabled) {
            //if (!m_grabber_connections[id].model.is_initialized() || !m_grabber_connections[id].old_center.isApprox(center)) {
                m_grabber_connections[id].old_center = m_grabbers[id].center;
                m_grabber_connections[id].model.reset();

                GLModel::Geometry init_data;
                init_data.format = { GLModel::Geometry::EPrimitiveType::Lines, GLModel::Geometry::EVertexLayout::P3 };
                init_data.color = AXES_COLOR[id];
                init_data.reserve_vertices(2);
                init_data.reserve_indices(2);

                // vertices
                init_data.add_vertex((Vec3f)zero.cast<float>());
                init_data.add_vertex((Vec3f)m_grabbers[id].center.cast<float>());

                // indices
                init_data.add_line(0, 1);

                m_grabber_connections[id].model.init_from(std::move(init_data));
            //}

            // ORCA: OpenGL Core Profile
#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile()) {
                glLineStipple(1, 0x0FFF);
                glEnable(GL_LINE_STIPPLE);
            }
#endif // !SLIC3R_OPENGL_ES
            m_grabber_connections[id].model.render();
#if !SLIC3R_OPENGL_ES
            if (!OpenGLManager::get_gl_info().is_core_profile())
                glDisable(GL_LINE_STIPPLE);
#endif // !SLIC3R_OPENGL_ES
        }
    };

#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
    if (shader != nullptr) {
        shader->start_using();
        const Camera& camera = wxGetApp().plater()->get_camera();
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * base_matrix);
        shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif // !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.25f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES

        // draw axes
        for (unsigned int i = 0; i < 3; ++i) {
            render_grabber_connection(i);
        }

        shader->stop_using();
    }
	
	// draw grabbers
    render_grabbers(render_box);

    // ORCAPORT: skip the instance-origin cross mark while the face-aligned frame is active
    if (m_object_manipulation->is_instance_coordinates() && !m_face_frame_active) {
#if SLIC3R_OPENGL_ES
    GLShaderProgram* shader = wxGetApp().get_shader("dashed_lines");
#else
    GLShaderProgram* shader = OpenGLManager::get_gl_info().is_core_profile() ? wxGetApp().get_shader("dashed_thick_lines") : wxGetApp().get_shader("flat");
#endif // SLIC3R_OPENGL_ES
        if (shader != nullptr) {
            shader->start_using();
            const Camera& camera = wxGetApp().plater()->get_camera();

            Geometry::Transformation cur_tran;
            if (auto mi = m_parent.get_selection().get_selected_single_intance()) {
                cur_tran = mi->get_transformation();
            } else {
                cur_tran = selection.get_first_volume()->get_instance_transformation();
            }

            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * cur_tran.get_matrix());
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
#if !SLIC3R_OPENGL_ES
            if (OpenGLManager::get_gl_info().is_core_profile()) {
#endif /// !SLIC3R_OPENGL_ES
                const std::array<int, 4>& viewport = camera.get_viewport();
                shader->set_uniform("viewport_size", Vec2d(double(viewport[2]), double(viewport[3])));
                shader->set_uniform("width", 0.5f);
                shader->set_uniform("gap_size", 0.0f);
#if !SLIC3R_OPENGL_ES
            }
#endif // !SLIC3R_OPENGL_ES

            render_cross_mark(Vec3f::Zero(), true);

            shader->stop_using();
        }
    }

    // ORCAPORT: highlight the face under the cursor while picking, and show the pivot's snap
    // coordinates on it
    if (m_pick_face_mode) {
        update_face_highlight(m_parent.get_local_mouse_position());
        render_face_highlight();
    }

    // ORCAPORT:MV-3 the Alt pivot-snap markers, dropped the moment Alt is released
    if (!wxGetKeyState(WXK_ALT))
        clear_pivot_snap();
    const Camera& mk_camera = wxGetApp().plater()->get_camera();
    m_objsnap_pivot_marker.render(mk_camera);
    m_objsnap_target_markers.render(mk_camera);
}

void GLGizmoMove3D::on_register_raycasters_for_picking()
{
    // the gizmo grabbers are rendered on top of the scene, so the raytraced picker should take it into account
    m_parent.set_raycaster_gizmos_on_top(true);
}

void GLGizmoMove3D::on_unregister_raycasters_for_picking()
{
    m_parent.set_raycaster_gizmos_on_top(false);
}

//BBS: add input window for move
void GLGizmoMove3D::on_render_input_window(float x, float y, float bottom_limit)
{
    if (m_object_manipulation)
        m_object_manipulation->do_render_move_window(m_imgui, "Move", x, y, bottom_limit);
}


double GLGizmoMove3D::calc_projection(const UpdateData& data) const
{
    double projection = 0.0;

    const Vec3d starting_vec = m_starting_drag_position - m_starting_box_center;
    const double len_starting_vec = starting_vec.norm();
    if (len_starting_vec != 0.0) {
        const Vec3d mouse_dir = data.mouse_ray.unit_vector();
        // finds the intersection of the mouse ray with the plane parallel to the camera viewport and passing throught the starting position
        // use ray-plane intersection see i.e. https://en.wikipedia.org/wiki/Line%E2%80%93plane_intersection algebric form
        // in our case plane normal and ray direction are the same (orthogonal view)
        // when moving to perspective camera the negative z unit axis of the camera needs to be transformed in world space and used as plane normal
        const Vec3d inters = data.mouse_ray.a + (m_starting_drag_position - data.mouse_ray.a).dot(mouse_dir) * mouse_dir;
        // vector from the starting position to the found intersection
        const Vec3d inters_vec = inters - m_starting_drag_position;

        // finds projection of the vector along the staring direction
        projection = inters_vec.dot(starting_vec.normalized());
    }

    if (wxGetKeyState(WXK_SHIFT))
        projection = m_snap_step * (double)std::round(projection / m_snap_step);

    return projection;
}

void GLGizmoMove3D::render_align_preview()
{
    using AlignType = GLGizmoAlignment::AlignType;
    if (m_object_manipulation == nullptr || m_object_manipulation->get_hovered_align_type() == AlignType::NONE)
        return;

    // The box the hovered operation aligns to, in world coordinates like the operation itself.
    const AlignType         type   = m_object_manipulation->get_hovered_align_type();
    const GLGizmoAlignment &helper = m_object_manipulation->get_alignment_helper();
    const BoundingBoxf3     box    = m_object_manipulation->get_align_to_parent() ? helper.get_parent_box() : helper.get_items_box();
    if (!box.defined)
        return;
    m_parent.get_selection().render_bounding_box(box, {0.0f, 150.0f / 255.0f, 136.0f / 255.0f}, m_parent.get_scale());

    GLShaderProgram* shader = wxGetApp().get_shader("flat");
    if (shader == nullptr)
        return;

    if (!m_align_plane.is_initialized()) {
        GLModel::Geometry init_data;
        init_data.format = { GLModel::Geometry::EPrimitiveType::Triangles, GLModel::Geometry::EVertexLayout::P3 };
        init_data.reserve_vertices(4);
        init_data.reserve_indices(6);
        init_data.add_vertex(Vec3f(-0.5f, -0.5f, 0.0f));
        init_data.add_vertex(Vec3f(0.5f, -0.5f, 0.0f));
        init_data.add_vertex(Vec3f(0.5f, 0.5f, 0.0f));
        init_data.add_vertex(Vec3f(-0.5f, 0.5f, 0.0f));
        init_data.add_triangle(0, 1, 2);
        init_data.add_triangle(0, 2, 3);
        m_align_plane.init_from(std::move(init_data));
        m_align_plane.set_color({ 1.0f, 1.0f, 1.0f, 0.5f });
    }

    // A plane across the box at its min, center or max on the hovered axis. Distributing shows up to six evenly
    // spaced planes.
    int    axis = Z;
    double t    = 0.5;
    switch (type) {
    case AlignType::X_MIN: axis = X; t = 0.0; break;
    case AlignType::X_MAX: axis = X; t = 1.0; break;
    case AlignType::CENTER_X:
    case AlignType::DISTRIBUTE_X: axis = X; break;
    case AlignType::Y_MIN: axis = Y; t = 0.0; break;
    case AlignType::Y_MAX: axis = Y; t = 1.0; break;
    case AlignType::CENTER_Y:
    case AlignType::DISTRIBUTE_Y: axis = Y; break;
    case AlignType::Z_MIN: t = 0.0; break;
    case AlignType::Z_MAX: t = 1.0; break;
    default: break;
    }
    const Vec3d         size = box.size();
    std::vector<double> positions;
    if (type >= AlignType::DISTRIBUTE_X) {
        const size_t count = std::min<size_t>(helper.get_items_count(type, false), 6);
        for (size_t i = 1; i <= count; ++i)
            positions.push_back(box.min[axis] + i * size[axis] / (count + 1));
    } else
        positions.push_back(box.min[axis] + t * size[axis]);

    const int   u            = (axis + 1) % 3;
    const int   v            = (axis + 2) % 3;
    Transform3d plane_matrix = Transform3d::Identity();
    plane_matrix.linear().col(0) = size[u] * Vec3d::Unit(u);
    plane_matrix.linear().col(1) = size[v] * Vec3d::Unit(v);
    plane_matrix.linear().col(2) = Vec3d::Unit(axis);
    Vec3d plane_center           = box.center();

    glsafe(::glDisable(GL_CULL_FACE));
    glsafe(::glEnable(GL_BLEND));
    glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
    // Translucent planes must hide neither each other nor the grabbers drawn after them.
    glsafe(::glDepthMask(GL_FALSE));
    shader->start_using();
    const Camera& camera = wxGetApp().plater()->get_camera();
    shader->set_uniform("projection_matrix", camera.get_projection_matrix());
    for (double position : positions) {
        plane_center[axis]         = position;
        plane_matrix.translation() = plane_center;
        shader->set_uniform("view_model_matrix", camera.get_view_matrix() * plane_matrix);
        m_align_plane.render();
    }
    shader->stop_using();
    glsafe(::glDepthMask(GL_TRUE));
    glsafe(::glEnable(GL_CULL_FACE));
    glsafe(::glDisable(GL_BLEND));
}

void GLGizmoMove3D::change_cs_by_selection() {
    int          obejct_idx, volume_idx;
    ModelVolume *model_volume = m_parent.get_selection().get_selected_single_volume(obejct_idx, volume_idx);
    if (m_last_selected_obejct_idx == obejct_idx && m_last_selected_volume_idx == volume_idx) {
        return;
    }
    m_last_selected_obejct_idx = obejct_idx;
    m_last_selected_volume_idx = volume_idx;
    if (m_parent.get_selection().is_multiple_full_object()) {
        m_object_manipulation->set_use_object_cs(false);
    }
    else if (model_volume) {
         m_object_manipulation->set_use_object_cs(true);
    } else {
        m_object_manipulation->set_use_object_cs(false);
    }
    if (m_object_manipulation->get_use_object_cs()) {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::Instance);
    } else {
        m_object_manipulation->set_coordinates_type(ECoordinatesType::World);
    }
}

// ORCAPORT: face-aligned move frame
Transform3d GLGizmoMove3D::face_frame_world() const
{
    if (!m_face_frame_active)
        return Transform3d::Identity();
    // frame is stored relative to the picked volume, so it follows the object
    const GLVolume* vol = m_parent.get_selection().get_volume(m_face_frame_volume_idx);
    if (vol != nullptr)
        return vol->world_matrix() * m_face_frame_local;
    return m_face_frame_local;
}

// The gizmo's own frame: the face frame once a pivot is picked, the reference-system box otherwise.
// This is the base matrix on_render builds, so the pivot it reports matches the drawn axes.
Transform3d GLGizmoMove3D::gizmo_base_matrix() const
{
    if (m_face_frame_active)
        return face_frame_world();
    return m_parent.get_selection().get_bounding_box_in_current_reference_system().second;
}

void GLGizmoMove3D::set_face_frame(const Vec3d& hit_world, const Vec3d& normal, unsigned int volume_idx, FaceSnapKind kind)
{
    Vec3d z = normal.normalized();
    if (z.squaredNorm() < 0.5)
        z = Vec3d::UnitZ();
    // point the blue axis toward the camera so the gizmo is not buried in the model
    const Camera& camera = wxGetApp().plater()->get_camera();
    if (z.dot(camera.get_position() - hit_world) < 0.0)
        z = -z;

    const Vec3d ref = (std::abs(z.z()) < 0.9) ? Vec3d::UnitZ() : Vec3d::UnitY();
    Vec3d x = ref.cross(z);
    if (x.squaredNorm() < 0.5)
        x = Vec3d::UnitX();
    x.normalize();
    const Vec3d y = z.cross(x);

    Matrix3d rot;
    rot.col(0) = x;
    rot.col(1) = y;
    rot.col(2) = z;

    Transform3d world_frame = Transform3d::Identity();
    world_frame.linear()      = rot;
    world_frame.translation() = hit_world;

    const GLVolume* vol = m_parent.get_selection().get_volume(volume_idx);
    m_face_frame_local      = (vol != nullptr) ? (vol->world_matrix().inverse() * world_frame) : world_frame;
    m_face_frame_volume_idx = volume_idx;
    m_face_frame_active = true;
    m_pick_face_mode    = false;
    m_pivot_kind        = kind;
    m_face_highlight.reset();
    m_hover_volume = nullptr;
    m_hover_facet  = -1;
    clear_pivot_candidates();
    if (m_object_manipulation)
        m_object_manipulation->m_move_window_combo_disabled = true;
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

void GLGizmoMove3D::clear_face_frame()
{
    m_face_frame_active = false;
    m_pick_face_mode    = false;
    m_pick_click_pending = false;
    m_face_frame_local  = Transform3d::Identity();
    m_pivot_kind        = FaceSnapKind::None;
    m_face_highlight.reset();
    m_hover_volume = nullptr;
    m_hover_facet  = -1;
    clear_pivot_candidates();
    if (m_object_manipulation)
        m_object_manipulation->m_move_window_combo_disabled = false;
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

// [ORCAPORT:MV-1] Enter face picking. The pick-pivot shortcut and the panel button both come through
// here, so picking works whether or not a pivot is already set; cancelling keeps the one that was set.
bool GLGizmoMove3D::start_pick_pivot()
{
    if (get_state() != On)
        return false;
    m_pick_face_mode = true;
    m_parent.set_as_dirty();
    return true;
}

// The pivot candidate set belongs to the face under the cursor; drop it with the highlight.
void GLGizmoMove3D::clear_pivot_candidates()
{
    m_pivot_points.clear();
    m_pivot_candidates.clear();
    m_pivot_active = OrcaExt::Gui::ObjectSnap::SnapHit::no_candidate;
    m_pivot_markers.clear();
}

void GLGizmoMove3D::pick_face_at(const Vec2d& mouse_pos)
{
    int obj_idx = -1;
    ModelObject* mo = m_parent.get_selection().get_selected_single_object(obj_idx);
    if (mo != nullptr) {
        // [ORCAPORT:MV-2] refresh the coordinates of the face under the click, so the pivot can stick
        // to the one the cursor is on instead of to the raw raycast hit.
        update_face_highlight(mouse_pos);

        const GLVolume* vol = nullptr;
        const ModelVolume* mv = nullptr;
        size_t facet = 0;
        Vec3d  hit_world;
        if (raycast_object_face(mouse_pos, m_parent.get_selection(), mo, nullptr, vol, mv, facet, hit_world, nullptr)) {
            const Vec3d normal = facet_normal_in_world(mv->mesh().its, int(facet), vol->world_matrix());
            unsigned int vol_idx = 0;
            for (unsigned int vi : m_parent.get_selection().get_volume_idxs()) {
                if (m_parent.get_selection().get_volume(vi) == vol) {
                    vol_idx = vi;
                    break;
                }
            }
            Vec3d        pivot = hit_world;
            FaceSnapKind kind  = FaceSnapKind::None;
            if (m_pivot_active < m_pivot_candidates.size()) {
                pivot = m_pivot_candidates[m_pivot_active].world;
                kind  = m_pivot_candidates[m_pivot_active].kind;
            }
            set_face_frame(pivot, normal, vol_idx, kind);
            return;
        }
    }
    m_pick_face_mode = false;
    m_face_highlight.reset();
    m_hover_volume = nullptr;
    m_hover_facet  = -1;
    clear_pivot_candidates();
    m_parent.set_as_dirty();
}

// ORCAPORT: hover highlight while picking a face (reused from the cut gizmo), plus the face's snap
// coordinates (MV-2): the pivot sticks to the nearest one under the cursor.
void GLGizmoMove3D::update_face_highlight(const Vec2d& mouse_pos)
{
    int obj_idx = -1;
    ModelObject* mo = m_parent.get_selection().get_selected_single_object(obj_idx);
    const GLVolume* hover_volume = nullptr;
    const ModelVolume* hover_mv = nullptr;
    size_t hover_facet = 0;
    Vec3d  hit_world;
    const bool hit = mo != nullptr && raycast_object_face(mouse_pos, m_parent.get_selection(), mo,
                                                          nullptr, hover_volume, hover_mv, hover_facet, hit_world, nullptr);
    if (!hit) {
        if (m_hover_volume != nullptr || m_face_highlight.is_initialized())
            m_face_highlight.reset();
        m_hover_volume = nullptr;
        m_hover_facet  = -1;
        clear_pivot_candidates();
        return;
    }

    // The face changed: rebuild the highlight patch and its snap coordinates (world space). Only the
    // cursor moves between frames, so the pick below is all that runs while it stays on one face.
    if (hover_volume != m_hover_volume || int(hover_facet) != m_hover_facet) {
        m_hover_volume = hover_volume;
        m_hover_facet  = int(hover_facet);

        m_face_highlight.reset();
        const std::vector<int> region = coplanar_region(hover_mv, hover_facet, m_face_cache);
        if (!region.empty()) {
            indexed_triangle_set patch = build_coplanar_patch(hover_mv, region, m_face_cache, 0.10f);
            if (!patch.indices.empty()) {
                m_face_highlight.init_from(patch);
                m_face_highlight.set_color(ColorRGBA(0.10f, 0.80f, 0.74f, 0.55f));
            }
        }

        clear_pivot_candidates();
        const Transform3d to_world = hover_volume->world_matrix();
        for (const FaceSnapPoint& p : build_face_snap_points(hover_mv, region)) {
            m_pivot_points.push_back({ p.kind, to_world * p.pos });
            m_pivot_candidates.push_back({ to_world * p.pos, p.kind });
        }
        m_pivot_markers.set(m_pivot_candidates, OrcaExt::Gui::ObjectSnap::SnapHit::no_candidate);
    }

    // Which coordinate the cursor is on, through the shared adaptive-tolerance pick. Only the active
    // sphere is rebuilt, so a moving cursor does not re-merge the per-kind meshes every frame.
    size_t active = OrcaExt::Gui::ObjectSnap::SnapHit::no_candidate;
    if (!m_pivot_points.empty()) {
        const Camera& camera = wxGetApp().plater()->get_camera();
        const auto    project = [&camera](const Vec3d& world) { return world_to_screen(camera, world); };
        FaceSnapPoint best;
        if (nearest_face_snap(m_pivot_points, project, mouse_pos, best, 8.0, 48.0)) {
            for (size_t i = 0; i < m_pivot_points.size(); ++i) {
                if (m_pivot_points[i].pos == best.pos) {
                    active = i;
                    break;
                }
            }
        }
    }
    if (active != m_pivot_active) {
        m_pivot_active = active;
        m_pivot_markers.set_active(m_pivot_candidates, m_pivot_active);
    }
    m_parent.set_as_dirty();
}

void GLGizmoMove3D::render_face_highlight()
{
    const Camera& camera = wxGetApp().plater()->get_camera();

    if (m_face_highlight.is_initialized() && m_hover_volume != nullptr) {
        GLShaderProgram* shader = wxGetApp().get_shader("flat");
        if (shader != nullptr) {
            shader->start_using();
            glsafe(::glEnable(GL_DEPTH_TEST));
            glsafe(::glDisable(GL_CULL_FACE));
            glsafe(::glEnable(GL_BLEND));
            glsafe(::glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA));
            shader->set_uniform("projection_matrix", camera.get_projection_matrix());
            shader->set_uniform("view_model_matrix", camera.get_view_matrix() * m_hover_volume->world_matrix());
            m_face_highlight.render();
            glsafe(::glDisable(GL_BLEND));
            glsafe(::glEnable(GL_CULL_FACE));
            shader->stop_using();
        }
    }

    // ORCAPORT:MV-2 the snap coordinates of the hovered face; the active one is drawn larger
    m_pivot_markers.render(camera);
}

// ORCAPORT:MV-3 Alt held while dragging a grabber: the pivot lands on the nearest snap coordinate of
// another object. Only the dragged axis is free, so the snap slides the object along that axis until
// the pivot's projection reaches the target's - the gizmo's axis constraint is kept.
void GLGizmoMove3D::apply_pivot_snap(const Vec2d& mouse_pos)
{
    if (!m_snap_drag_latched)
        return;

    const Model* model = m_parent.get_model();
    if (model == nullptr) {
        clear_pivot_snap();
        return;
    }

    std::set<std::pair<int, int>> moving;
    for (unsigned int idx : m_parent.get_selection().get_volume_idxs()) {
        const GLVolume* gv = m_parent.get_volumes().volumes[idx];
        if (gv == nullptr || gv->is_wipe_tower || gv->is_modifier)
            continue;
        moving.insert({ gv->object_idx(), gv->instance_idx() });
    }

    const Camera& camera = wxGetApp().plater()->get_camera();
    const std::optional<OrcaExt::Gui::ObjectSnap::SnapHit> hit =
        OrcaExt::Gui::ObjectSnap::hit_under_cursor(*model, m_parent.get_volumes(), camera, moving, mouse_pos);

    m_objsnap_target_markers.clear();
    m_objsnap_pivot_marker.clear();
    if (!hit)
        return;
    m_objsnap_target_markers.set(hit->candidates, hit->active);
    if (!hit->has_point())
        return;

    // Selection::translate moves the selection by `base.linear() * m_displacement`, so column
    // `m_hover_id` is the world direction of the dragged axis and its length scales one unit of
    // m_displacement. Solve m_displacement[k] so the pivot's world motion is the distance from the
    // pivot to the target along that axis.
    const Transform3d base    = gizmo_base_matrix();
    const Vec3d       col     = base.linear().col(m_hover_id);
    const double      col_len = col.norm();
    if (col_len <= 1e-12)
        return;
    const Vec3d  axis   = col / col_len;
    const Vec3d  target = hit->point().world;
    const double dist   = (target - m_snap_pivot_start_world).dot(axis);

    m_displacement[m_hover_id] = dist / col_len;

    const std::vector<OrcaExt::Gui::ObjectSnap::SnapCandidate> pivot = {
        { m_snap_pivot_start_world + axis * dist, FaceSnapKind::None } };
    m_objsnap_pivot_marker.set(pivot, 0, OrcaExt::Gui::ObjectSnap::MarkerRole::Mover);
}

void GLGizmoMove3D::clear_pivot_snap()
{
    m_objsnap_target_markers.clear();
    m_objsnap_pivot_marker.clear();
}

// ORCAPORT:MV-2 the snapped feature, named the way Plasticity's tip help does
static const char* pivot_kind_name(FaceSnapKind kind)
{
    switch (kind) {
    case FaceSnapKind::Corner:      return "Corner";
    case FaceSnapKind::EdgeMid:     return "Edge mid";
    case FaceSnapKind::FaceCenter:  return "Face centre";
    case FaceSnapKind::EdgeQuarter: return "Edge 1/4";
    case FaceSnapKind::FaceQuarter: return "Face 1/4";
    default:                        return "Free";
    }
}

void GLGizmoMove3D::render_extra_move_ui()
{
    if (get_state() != On)
        return;

    ImGui::Spacing();
    if (m_pick_face_mode) {
        if (m_imgui->button(_L("Cancel pivot")))
            {
                m_pick_face_mode = false;
                m_face_highlight.reset();
                m_hover_volume = nullptr;
                m_hover_facet  = -1;
                clear_pivot_candidates();
                m_parent.set_as_dirty();
            }
        m_imgui->text(_L("Click a face to place the pivot."));
        m_imgui->text(_L("It sticks to corners and to edge and face midpoints."));
    } else {
        // [ORCAPORT:MV-1] both actions stay available: the pivot can be moved to another face without
        // resetting it first, and cancelling the re-pick keeps the pivot that was set.
        if (m_imgui->button(_L("Pick pivot")))
            start_pick_pivot();
        if (m_face_frame_active) {
            ImGui::SameLine();
            if (m_imgui->button(_L("Reset pivot")))
                clear_face_frame();
        }
        // Read the flag again: a reset above has just cleared the pivot.
        if (m_face_frame_active)
            m_imgui->text(_L("Pivot") + ": " + pivot_kind_name(m_pivot_kind));
    }
}


} // namespace GUI
} // namespace Slic3r
