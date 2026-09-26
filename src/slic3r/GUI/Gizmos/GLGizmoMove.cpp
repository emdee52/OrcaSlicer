#include "GLGizmoMove.hpp"
#include "slic3r/GUI/GLCanvas3D.hpp"
#include "slic3r/GUI/GUI_App.hpp"
//BBS: GUI refactor
#include "slic3r/GUI/Plater.hpp"
#include "libslic3r/AppConfig.hpp"
// ORCAPORT: face-aligned move frame
#include "GLGizmosCommon.hpp"
#include "libslic3r/CutUtils.hpp"


#include <glad/gl.h>

#include <wx/utils.h>

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
    // ORCAPORT: face-aligned move frame
    if (m_pick_face_mode) {
        if (mouse_event.LeftDown()) {
            const wxPoint p = mouse_event.GetPosition();
            pick_face_at(Vec2d(double(p.x), double(p.y)));
            return true;
        }
        if (mouse_event.RightDown()) {
            m_pick_face_mode = false;
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

    m_shortcut_key = WXK_CONTROL_M;

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
}

void GLGizmoMove3D::on_start_dragging()
{
    assert(m_hover_id != -1);

    m_displacement = Vec3d::Zero();
    const BoundingBoxf3& box = m_parent.get_selection().get_bounding_box();
    m_starting_drag_position = m_grabbers[m_hover_id].matrix * m_grabbers[m_hover_id].center;
    // ORCAPORT: in face mode the drag runs along the face axis, not from the selection center
    if (m_face_frame_active)
        m_starting_box_center = m_starting_drag_position - m_face_frame.linear().col(m_hover_id).normalized();
    else
        m_starting_box_center = box.center();
    m_starting_box_bottom_center = box.center();
    m_starting_box_bottom_center(2) = box.min(2);
}

void GLGizmoMove3D::on_stop_dragging()
{
    m_parent.do_move(L("Gizmo-Move"));
    m_displacement = Vec3d::Zero();
}

void GLGizmoMove3D::on_dragging(const UpdateData& data)
{
    if (m_hover_id == 0)
        m_displacement.x() = calc_projection(data);
    else if (m_hover_id == 1)
        m_displacement.y() = calc_projection(data);
    else if (m_hover_id == 2)
        m_displacement.z() = calc_projection(data);
        
    Selection &selection = m_parent.get_selection();
    TransformationType trafo_type;
    trafo_type.set_relative();
    // ORCAPORT: move along the picked face axes (world-space displacement)
    if (m_face_frame_active) {
        selection.translate(m_face_frame.linear() * m_displacement, trafo_type);
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

    const auto &[ref_box, box_trafo]    = selection.get_bounding_box_in_current_reference_system();
    Transform3d  base_matrix = box_trafo;
    BoundingBoxf3 render_box = ref_box;
    // ORCAPORT: face-aligned move frame
    if (m_face_frame_active) {
        base_matrix = m_face_frame;
        const Transform3d inv = m_face_frame.inverse();
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

    // x axis
    m_grabbers[0].center = {render_box.max.x() + space_size, 0, 0};
    // y axis
    m_grabbers[1].center = {0, render_box.max.y() + space_size,0};
    // z axis
    m_grabbers[2].center = {0,0, render_box.max.z() + space_size};

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
void GLGizmoMove3D::set_face_frame(const Vec3d& hit_world, const Vec3d& normal)
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

    m_face_frame = Transform3d::Identity();
    m_face_frame.linear()      = rot;
    m_face_frame.translation() = hit_world;
    m_face_frame_active = true;
    m_pick_face_mode    = false;
    if (m_object_manipulation)
        m_object_manipulation->m_move_window_combo_disabled = true;
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

void GLGizmoMove3D::clear_face_frame()
{
    m_face_frame_active = false;
    m_pick_face_mode    = false;
    if (m_object_manipulation)
        m_object_manipulation->m_move_window_combo_disabled = false;
    m_parent.set_as_dirty();
    m_parent.request_extra_frame();
}

void GLGizmoMove3D::pick_face_at(const Vec2d& mouse_pos)
{
    int obj_idx = -1;
    ModelObject* mo = m_parent.get_selection().get_selected_single_object(obj_idx);
    if (mo != nullptr) {
        const GLVolume* vol = nullptr;
        const ModelVolume* mv = nullptr;
        size_t facet = 0;
        Vec3d  hit_world;
        if (raycast_object_face(mouse_pos, m_parent.get_selection(), mo, nullptr, vol, mv, facet, hit_world, nullptr)) {
            const Vec3d normal = facet_normal_in_world(mv->mesh().its, int(facet), vol->world_matrix());
            set_face_frame(hit_world, normal);
            return;
        }
    }
    m_pick_face_mode = false;
    m_parent.set_as_dirty();
}

void GLGizmoMove3D::render_extra_move_ui()
{
    if (get_state() != On)
        return;

    ImGui::Spacing();
    if (m_pick_face_mode) {
        if (m_imgui->button(_L("Cancel face pick")))
            { m_pick_face_mode = false; m_parent.set_as_dirty(); }
        m_imgui->text(_L("Click a flat face on the model."));
    } else if (m_face_frame_active) {
        if (m_imgui->button(_L("Reset axis")))
            clear_face_frame();
    } else {
        if (m_imgui->button(_L("Pick face")))
            { m_pick_face_mode = true; m_parent.set_as_dirty(); }
    }
}


} // namespace GUI
} // namespace Slic3r
