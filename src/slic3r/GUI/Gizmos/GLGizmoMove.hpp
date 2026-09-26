#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"
#include "GLGizmosCommon.hpp"


namespace Slic3r {
namespace GUI {

//BBS: GUI refactor: add object manipulation
class GizmoObjectManipulation;
class GLGizmoMove3D : public GLGizmoBase
{
    static const double Offset;

    Vec3d m_displacement{ Vec3d::Zero() };
    Vec3d m_center{ Vec3d::Zero() };
    BoundingBoxf3 m_bounding_box;
    double m_snap_step{ 1.0 };
    Vec3d m_starting_drag_position{ Vec3d::Zero() };
    Vec3d m_starting_box_center{ Vec3d::Zero() };
    Vec3d m_starting_box_bottom_center{ Vec3d::Zero() };

    struct GrabberConnection
    {
        GLModel model;
        Vec3d old_center{ Vec3d::Zero() };
    };
    std::array<GrabberConnection, 3> m_grabber_connections;

    //BBS: add size adjust related
    GizmoObjectManipulation* m_object_manipulation;

public:
    //BBS: add obj manipulation logic
    //GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    GLGizmoMove3D(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id, GizmoObjectManipulation* obj_manipulation);
    virtual ~GLGizmoMove3D() = default;

    double get_snap_step(double step) const { return m_snap_step; }
    void set_snap_step(double step) { m_snap_step = step; }

    std::string get_tooltip() const override;

    /// <summary>
    /// Postpone to Grabber for move
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

    bool render_follows_cursor() const override;

    /// <summary>
    /// Detect reduction of move for wipetover on selection change
    /// </summary>
    void data_changed(bool is_serializing) override;
protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    virtual void on_set_state() override;
    void on_start_dragging() override;
    void on_stop_dragging() override;
    void on_dragging(const UpdateData& data) override;
    void on_render() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;
    //BBS: GUI refactor: add object manipulation
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

private:
    double calc_projection(const UpdateData& data) const;
    void   change_cs_by_selection(); //cs mean Coordinate System

    // ORCAPORT: face-aligned move frame
    bool          m_pick_face_mode{ false };
    bool          m_face_frame_active{ false };
    // frame stored relative to the picked volume, so it follows the object
    Transform3d   m_face_frame_local{ Transform3d::Identity() };
    unsigned int  m_face_frame_volume_idx{ 0 };
    GLModel       m_face_highlight;
    FaceRegionCache m_face_cache;
    const GLVolume* m_hover_volume{ nullptr };
    int           m_hover_facet{ -1 };
    Transform3d   face_frame_world() const;
    void          pick_face_at(const Vec2d& mouse_pos);
    void          set_face_frame(const Vec3d& hit_world, const Vec3d& normal, unsigned int volume_idx);
    void          clear_face_frame();
    void          render_extra_move_ui();
    void          update_face_highlight();
    void          render_face_highlight();
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
