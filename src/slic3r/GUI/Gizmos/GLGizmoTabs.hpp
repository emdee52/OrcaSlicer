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

    // --- the geometry rows. The panel and the embedded MCP tools share these setters. ---
    void set_tip(TabTip t);
    void set_root_w(double mm);
    void set_length(double mm);      // the tab's reach out from its root
    void set_hole_offset(double mm); // where the screw sits along that length
    void set_taper(double deg);
    void set_passage(TabPassage p);
    void set_standard(int idx); // 0 = Custom, else 1 + the index into hole_standards()
    void set_diameter(double mm);
    void set_tolerance(double mm);
    void set_screw_fit(ScrewFit f);
    void set_head(BoreHead h);
    void set_head_fit(double f); // 0 flush, negative sinks the head
    void set_boss(bool on);
    void set_boss_d(double mm);
    void set_boss_h(double mm);
    void set_thickness(double mm);
    void set_angle(double deg);

    TabTip     get_tip() const { return m_tip; }
    TabPassage get_passage() const { return m_passage; }
    BoreHead   get_head() const { return m_head; }
    ScrewFit   get_screw_fit() const { return m_screw_fit; }
    int        get_standard() const { return m_standard; }
    int        standard_count() const { return 1 + int(hole_standards().size()); }
    std::string standard_name(int idx) const;
    double     get_diameter() const { return m_diameter; }
    double     get_tolerance() const { return m_tolerance; }
    bool       get_boss() const { return m_boss; }
    double     get_boss_d() const { return m_boss_d; }
    double     get_boss_h() const { return m_boss_h; }
    double     get_thickness() const { return m_thickness; }
    double     get_angle() const { return m_angle; }
    double     get_root_w() const { return m_root_w; }
    double     get_length() const { return m_length; }
    double     get_hole_offset() const { return m_hole_offset; }
    double     get_taper() const { return m_taper; }
    double     get_head_fit() const { return m_head_fit; }
    // The hole offset the current length allows, and the split it makes: the panel's slider bounds and
    // the hint line both read these.
    double hole_offset_min() const;
    double hole_offset_max() const;
    // The head styles this screw's standard actually has, prefixed with None. Empty when it has none
    // (Custom), which is when the panel hides the row rather than offering a seat that would cut
    // nothing. `out` receives up to 4 entries and returns the count.
    int    available_heads(BoreHead *out) const;
    // The weld the geometry is using: the constant the tool always enters the material by, raised by
    // the surface's own fall-away on a curved pick.
    double effective_weld() const { return m_effective_weld; }

    // The clearance bore the current standard, fit and tolerance resolve to, in world mm - the number
    // the True diameter row shows and the one the cutter is built from.
    double true_diameter() const;

    // --- actions ---
    // A click: the first one PICKS the face under the cursor (green highlight, nothing placed), and the
    // next one on that same face PLACES the tab's passage where it landed. A click on another face
    // moves the pick instead of placing, so an anchor can only ever be a point on the picked face.
    bool click_at(const Vec2d& screen_pos);
    bool pick_face_at(const Vec2d& screen_pos); // just the pick, for a caller that wants it alone
    bool anchor_at(const Vec2d& screen_pos);    // place on the picked face (false when not on it)
    void clear_anchor();
    bool commit();          // Add tab
    bool remove_hovered();  // right-click a tab already added
    bool remove_at(const Vec2d& screen_pos); // the same, for a caller that names the pixel (MCP)
    void clear_all();
    void refresh();

    // --- readouts, for the panel and for the MCP tools ---
    bool          anchor_valid() const { return m_pick.valid; }
    bool          face_picked() const { return m_face.valid; }
    int           face_region_facets() const { return int(m_face.region.size()); }
    int           tab_count() const;
    double        coverage() const { return m_probe.coverage; }
    double        fall_away() const { return m_probe.fall_away; }
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

    // The face the user PICKED, which is what a tab may be placed on. It is highlighted green, and the
    // tab's anchor is only ever a point on it.
    struct Face
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        std::vector<int>   region; // its coplanar facets, which is what the highlight and the test use
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
    // The face pick: `pick_face` takes it from a hover, `on_picked_face` is the test a placement has to
    // pass, and `clear_face` drops it.
    void        pick_face(const Hover& h);
    void        clear_face();
    bool        on_picked_face(const Hover& h) const;
    // Where the tab goes, from the picked face, the click and the angle (libslic3r/TabShapes). The
    // frame's +z is the plate's normal, so `wall` says whether the plate is parallel to the bed.
    // `out_bite` receives the bite the plate itself carries (see the note in the implementation).
    TabFrame    frame(double* out_bite = nullptr) const;
    // The same frame for an arbitrary point, which is how the ghost follows the cursor across the
    // picked face before it is placed.
    TabFrame    frame_at(const ModelVolume* mv, int facet, const Vec3d& click_obj, double* out_bite) const;
    Transform3d frame_to_volume(const ModelVolume* mv) const;     // ... and -> that volume's mesh

    // The spec in object units, with the weld the surface needs and the bore's depth resolved. `why`
    // is set when the tab cannot be built at all.
    TabSpec  build_spec(std::string* why) const;
    TabScrew build_screw(const TabSpec& spec) const;
    // The seat the current head and sink ask for, in mm: 0 when the head has no dimensions.
    double   seat_diameter() const;
    double   seat_depth() const;
    double   seat_angle() const;
    double   head_sink() const; // how far the head is sunk below the surface, >= 0

    void probe_surface(bool force);
    void rebuild_preview();

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
    Face  m_face;
    Pick  m_pick;

    TabTip     m_tip{ TabTip::Round };
    double     m_root_w{ 14. }, m_length{ 16. }, m_hole_offset{ 6. }, m_taper{ 12. };
    TabPassage m_passage{ TabPassage::Hole };

    int       m_standard{ -1 };
    double    m_diameter{ 3.4 }, m_tolerance{ 0. };
    ScrewFit  m_screw_fit{ ScrewFit::Free };
    BoreHead  m_head{ BoreHead::SocketHead };
    double    m_head_fit{ 0. };

    bool   m_boss{ false };
    double m_boss_d{ 6.6 }, m_boss_h{ 4. };
    double m_thickness{ 3. }, m_angle{ 0. };

    // The last surface measurement, and the weld the geometry actually uses: the tool's constant,
    // raised by the surface's own fall-away on a curved pick.
    TabSurfaceProbe m_probe;
    double          m_effective_weld{ 0. };
    std::string     m_why;
    double          m_probe_time{ 0. }; // throttles the probe while a slider is dragged

    // Per-facet normals and neighbours of the hovered volume, for the face highlight. Shared with the
    // Cut and Holes gizmos' face picking (GLGizmosCommon). Two caches: the hover and the picked face can
    // be on different volumes, and one cache would then rebuild on every frame.
    FaceRegionCache m_face_cache;
    FaceRegionCache m_pick_cache;

    // Preview overlays, in the fork's colours: teal surface, green picked face, orange proposal, green
    // added, red cut.
    PickingModel m_preview_face;
    PickingModel m_preview_picked;
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
