#ifndef slic3r_GLGizmoMove_hpp_
#define slic3r_GLGizmoMove_hpp_

#include "GLGizmoBase.hpp"
//BBS: add size adjust related
#include "GizmoObjectManipulation.hpp"
#include "libslic3r/Point.hpp"
#include "slic3r/GUI/GLModel.hpp"
#include <array>
#include <string>
#include <wx/event.h>
#include "GLGizmosCommon.hpp"
// ORCAPORT: pivot snapping and the Alt pivot snap reuse the object-snap core
#include "slic3r/GUI/OrcaExt/ObjectSnap.hpp"


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
    // Unit square previewing the target plane of the hovered align or distribute icon.
    GLModel m_align_plane;

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
    void   render_align_preview();
    void   change_cs_by_selection(); //cs mean Coordinate System

    // ORCAPORT: face-aligned move frame
    bool          m_pick_face_mode{ false };
    bool          m_pick_click_pending{ false }; // the release of the placing click, swallowed by the gizmo
    bool          m_face_frame_active{ false };
    // frame stored relative to the picked volume, so it follows the object
    Transform3d   m_face_frame_local{ Transform3d::Identity() };
    unsigned int  m_face_frame_volume_idx{ 0 };
    GLModel       m_face_highlight;
    FaceRegionCache m_face_cache;
    const GLVolume* m_hover_volume{ nullptr };
    int           m_hover_facet{ -1 };
    Transform3d   face_frame_world() const;
    Transform3d   gizmo_base_matrix() const;
    void          pick_face_at(const Vec2d& mouse_pos);
    void          set_face_frame(const Vec3d& hit_world, const Vec3d& normal, unsigned int volume_idx,
                                 FaceSnapKind kind = FaceSnapKind::None);
    void          clear_face_frame();
    void          render_extra_move_ui();
    void          update_face_highlight(const Vec2d& mouse_pos);
    void          render_face_highlight();

    // ORCAPORT: pivot snapping (MV-2) - the pivot sticks to a face's snap coordinates
    std::vector<FaceSnapPoint>                           m_pivot_points;     // world space
    std::vector<OrcaExt::Gui::ObjectSnap::SnapCandidate> m_pivot_candidates; // world space, drawn
    size_t                                               m_pivot_active{ OrcaExt::Gui::ObjectSnap::SnapHit::no_candidate };
    FaceSnapKind                                         m_pivot_kind{ FaceSnapKind::None };
    OrcaExt::Gui::ObjectSnap::Markers                    m_pivot_markers;
    void clear_pivot_candidates();

    // ORCAPORT: Alt pivot snap while dragging (MV-3) - the pivot lands on another object's coordinate
    OrcaExt::Gui::ObjectSnap::Markers m_objsnap_target_markers;
    OrcaExt::Gui::ObjectSnap::Markers m_objsnap_pivot_marker;
    bool  m_snap_drag_latched{ false };
    Vec3d m_snap_pivot_start_world{ Vec3d::Zero() };
    void  apply_pivot_snap(const Vec2d& mouse_pos);
    void  clear_pivot_snap();
private:
    int m_last_selected_obejct_idx, m_last_selected_volume_idx;
};



} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMove_hpp_
