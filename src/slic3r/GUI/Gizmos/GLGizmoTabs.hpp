#ifndef slic3r_GLGizmoTabs_hpp_
#define slic3r_GLGizmoTabs_hpp_

// [ORCAPORT:TB-1] Mounting tabs. Pick a face, click where the screw should go, dial the tab in, and
// Add tab: a plate lying in that face's plane with a screw passage through it and an optional boss,
// added as ordinary volumes (MODEL_PART plate + boss, NEGATIVE_VOLUME seat and bore) so there is no
// mesh boolean and one undo takes the tab away.
//
// The tab's own frame is what makes "align it where you want on that face" one gesture:
//   +x  outward along the tab, from the root edge through the passage to the tip
//   +y  the width
//   +z  out of the surface
// and the passage - the drilled hole or the slot's closed end - sits at the ORIGIN, so the click
// marks the screw and every length is measured from it. libslic3r/TabShapes owns the geometry; this
// file owns the frame, the preview, the panel and the volumes.
//
// The screw vocabulary (BoreHead, ScrewFit and the standards table) is the Holes tool's, deliberately:
// the same designations, the same free/tap bores, the same head seats - see GLGizmoHoles.hpp, which is
// included for exactly those two enums rather than duplicated here.

#include "GLGizmoBase.hpp"
#include "GLGizmoHoles.hpp"   // BoreHead, ScrewFit: the same screw vocabulary
#include "GLGizmosCommon.hpp"

#include "libslic3r/TabShapes.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
class ModelVolume;

namespace GUI {

class GLGizmoTabs : public GLGizmoBase
{
public:
    GLGizmoTabs(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    // The ghost follows the cursor, so the frame must not be reused from the scene cache.
    bool render_follows_cursor() const override { return get_state() == On; }

    // --- the parameters. The panel and the embedded MCP tools share these setters. ---
    void set_shape(TabShape s);
    void set_root_w(double mm);
    void set_root_len(double mm);
    void set_tip_len(double mm);
    void set_taper(double deg);
    void set_corner_r(double mm);
    void set_passage(TabPassage p);
    void set_hole_offset(double mm);
    void set_standard(int idx); // 0 = Custom, else 1 + the index into hole_standards()
    void set_diameter(double mm);
    void set_tolerance(double mm);
    void set_screw_fit(ScrewFit f);
    void set_head(BoreHead h);
    void set_head_fit(double f); // 0 flush, negative sinks the head
    void set_depth(double mm);
    void set_through(bool on);
    void set_boss(bool on);
    void set_boss_d(double mm);
    void set_boss_h(double mm);
    void set_thickness(double mm);
    void set_bite(double mm);
    void set_angle(double deg);

    TabShape    get_shape() const { return m_shape; }
    TabPassage  get_passage() const { return m_passage; }
    BoreHead    get_head() const { return m_head; }
    ScrewFit    get_screw_fit() const { return m_screw_fit; }
    int         get_standard() const { return m_standard; }
    int         standard_count() const { return 1 + int(hole_standards().size()); }
    std::string standard_name(int idx) const;
    double      get_diameter() const { return m_diameter; }
    double      get_tolerance() const { return m_tolerance; }
    double      get_depth() const { return m_depth; }
    bool        get_through() const { return m_through; }
    bool        get_boss() const { return m_boss; }
    double      get_boss_d() const { return m_boss_d; }
    double      get_boss_h() const { return m_boss_h; }
    double      get_thickness() const { return m_thickness; }
    double      get_bite() const { return m_bite; }
    double      get_angle() const { return m_angle; }
    double      get_root_w() const { return m_root_w; }
    double      get_root_len() const { return m_root_len; }
    double      get_tip_len() const { return m_tip_len; }
    double      get_taper() const { return m_taper; }
    double      get_corner_r() const { return m_corner_r; }
    double      get_hole_offset() const { return m_hole_offset; }
    double      get_head_fit() const { return m_head_fit; }

    // The clearance bore the current standard, fit and tolerance resolve to, in world mm - the number
    // the True diameter row shows and the one the cutter is built from.
    double true_diameter() const;

    // --- actions ---
    bool anchor_at(const Vec2d& screen_pos); // a click: put the passage under the cursor
    void clear_anchor();
    bool commit();          // Add tab
    bool remove_hovered();  // right-click a tab already added
    bool remove_at(const Vec2d& screen_pos); // the same, for a caller that names the pixel (MCP)
    void clear_all();
    void refresh();

    // --- readouts, for the panel and for the MCP tools ---
    bool          anchor_valid() const { return m_pick.valid; }
    int           tab_count() const;
    double        coverage() const { return m_probe.coverage; }
    double        fall_away() const { return m_probe.fall_away; }
    double        effective_bite() const { return m_effective_bite; }
    // Where the tab goes: a wall click gives a plate parallel to the bed (its own +z is the bed's up),
    // a face within 25 degrees of horizontal keeps the plate in that face's plane.
    bool          frame_is_wall() const { return frame(nullptr).wall; }
    Vec3d         frame_axis() const { return frame(nullptr).axis; }
    const std::string& status_why() const { return m_why; }
    // The last tab's frame and resolved sizes, so a caller can see where a scripted tab landed.
    std::vector<std::array<double, 3>> list_tabs() const;

protected:
    bool        on_init() override;
    std::string on_get_name() const override;
    bool        on_is_activable() const override;
    void        on_set_state() override;
    void        on_render() override;
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    CommonGizmosDataID on_get_requirements() const override;
    void        on_register_raycasters_for_picking() override;
    void        on_unregister_raycasters_for_picking() override;

private:
    // The face under the cursor.
    struct Hover
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        Vec3d              hit_obj{ Vec3d::Zero() }; // object space
    };

    // The anchored placement: what the frame is built from, plus the object units the panel's mm are
    // converted into.
    struct Pick
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        Vec3d              click_obj{ Vec3d::Zero() };  // the point on the face that was clicked
        Vec3d              normal_obj{ Vec3d::UnitZ() };// the face's outward normal
    };

    ModelObject* model_object() const;
    int          object_idx() const;
    Transform3d  instance_matrix() const;
    double       object_scale() const;                            // world mm per object unit
    double       volume_scale(const ModelVolume* mv) const;
    Vec3d        object_centre() const;                           // the object's bounding-box centre

    void        update_hover(const Vec2d& screen_pos);
    void        clear_hover();
    // Where the tab goes, from the picked face, the click and the angle (libslic3r/TabShapes). The
    // frame's +z is the plate's normal, so `wall` says whether the plate is parallel to the bed.
    // `out_bite` receives the bite the plate itself carries (see the note in the implementation).
    TabFrame    frame(double* out_bite = nullptr) const;
    Transform3d frame_to_volume(const ModelVolume* mv) const;     // ... and -> that volume's mesh

    // The spec in object units, with the bite the surface needs and the depth resolved. `why` is set
    // when the tab cannot be built at all.
    TabSpec  build_spec(std::string* why) const;
    TabScrew build_screw(const TabSpec& spec) const;
    // The seat the current head and sink ask for, in mm: 0 when the head has no dimensions.
    double   seat_diameter() const;
    double   seat_depth() const;
    double   seat_angle() const;
    double   head_sink() const; // how far the head is sunk below the surface, >= 0

    void probe_surface(bool force);
    void rebuild_preview();
    void rebuild_merged();      // the object's part volumes, for the through march

    // Volumes. `snapshot` is what makes one undo remove the whole tab.
    bool add_tab_volumes(const TabSpec& spec, const TabScrew& screw, const std::string& action);
    int  next_tab_id() const;
    void remove_tab(int id);

    // The screw's dimensions whenever the standard, fit or head changes: the boss is re-sized to the
    // head and, for a tap, given the height the thread needs.
    void suggest_boss();
    void reseat_defaults();     // the M3 socket-head defaults on_init()
    const HoleStandard* screw_standard() const; // nullptr for the Custom entry

    Hover m_hover;
    Pick  m_pick;

    TabShape m_shape{ TabShape::TaperedU };
    double   m_root_w{ 14. }, m_root_len{ 6. }, m_tip_len{ 10. }, m_taper{ 12. }, m_corner_r{ 2. };
    TabPassage m_passage{ TabPassage::Hole };
    double     m_hole_offset{ 0. }; // sliders only: sliding the passage between the two lengths

    int       m_standard{ -1 };
    double    m_diameter{ 3.4 }, m_tolerance{ 0. };
    ScrewFit  m_screw_fit{ ScrewFit::Free };
    BoreHead  m_head{ BoreHead::SocketHead };
    double    m_head_fit{ 0. };
    double    m_depth{ 10. };
    bool      m_through{ true };

    bool   m_boss{ false };
    double m_boss_d{ 6.6 }, m_boss_h{ 4. };
    double m_thickness{ 3. }, m_bite{ 0.6 }, m_angle{ 0. };

    // The last surface measurement, and the bite the geometry actually uses (the surface's own
    // fall-away can raise it above the panel's value).
    TabSurfaceProbe m_probe;
    double          m_effective_bite{ 0. };
    std::string     m_why;
    double          m_probe_time{ 0. }; // throttles the probe while a slider is dragged

    indexed_triangle_set m_merged_its; // object space, all MODEL_PART volumes
    bool                 m_merged_dirty{ true };

    // Per-facet normals and neighbours of the hovered volume, for the face highlight. Shared with the
    // Cut and Holes gizmos' face picking (GLGizmosCommon).
    FaceRegionCache m_face_cache;

    // Preview overlays, in the fork's colours: teal surface, orange proposal, green added, red cut.
    PickingModel m_preview_face;
    PickingModel m_preview_tab;
    PickingModel m_preview_added;
    PickingModel m_preview_cut;
    bool         m_preview_dirty{ true };

    // Rebuilt when the object's volumes or the instance change, like the other gizmos.
    const ModelObject* m_old_object{ nullptr };
    int                m_old_volume_count{ -1 };
    Transform3d        m_old_matrix{ Transform3d::Identity() };

    // The hover is rebuilt only when the cursor or the camera moved.
    bool        m_hover_computed{ false };
    Vec2d       m_last_mouse{ -1., -1. };
    Transform3d m_last_view{ Transform3d::Identity() };

    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoTabs_hpp_
