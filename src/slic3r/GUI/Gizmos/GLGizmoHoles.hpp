#ifndef slic3r_GLGizmoHoles_hpp_
#define slic3r_GLGizmoHoles_hpp_

// [ORCAPORT:ME-2] Holes tool. Detects cylindrical holes on the selected object and applies, per
// picked hole, either a teardrop cut (horizontal holes, ME-1) or a parametric bore/pocket on any
// axis: screw clearance with an optional counterbore / countersink, a heat-set insert pocket, a
// magnet pocket, or a custom diameter. Shrinking a hole adds a positive tube. Works on the mesh
// directly - no CAD reconstruction. Partial bridging is left to the global counterbore option.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/HoleDetector.hpp"
#include "libslic3r/HoleStandards.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
enum class ModelVolumeType : int;

namespace GUI {

enum class HoleOperation { Teardrop, Bore };
enum class BoreHead { None, SocketHead, ButtonHead, Countersink };
enum class ScrewFit { Free, Tap };
enum class HoleCategory { Screw, Nut, Magnet, Insert, Custom };

class GLGizmoHoles : public GLGizmoBase
{
public:
    GLGizmoHoles(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;
    // Hover changes the highlight, so the frame must not be reused from the scene cache.
    bool render_follows_cursor() const override { return get_state() == On; }

    // --- control surface for the embedded MCP tools (see OrcaMCPGizmoTools.cpp) ---
    int          hole_count();        // runs detection if needed
    DetectedHole hole(int idx) const; // object-space hole
    bool         hole_horizontal(int idx) const;
    bool         hole_has_teardrop(int idx) const;
    bool         hole_has_bore(int idx) const;

    HoleOperation get_operation() const { return m_operation; }
    void          set_operation(HoleOperation op);
    float         get_angle() const { return m_angle_deg; }
    void          set_angle(float deg);

    int         standard_count() const;              // includes the leading "Custom" entry
    std::string standard_name(int idx) const;        // idx 0 = Custom
    int         get_standard() const { return m_standard; }
    void        set_standard(int idx);
    HoleCategory get_category() const { return m_category; }
    void         set_category(HoleCategory c);
    BoreHead    get_head() const { return m_head; }
    void        set_head(BoreHead h);
    ScrewFit    get_screw_fit() const { return m_screw_fit; }
    void        set_screw_fit(ScrewFit f);
    HoleFit     get_fit() const { return HoleFit(m_fit); }
    void        set_fit(HoleFit f);
    double      get_diameter() const { return m_diameter; }
    void        set_diameter(double d);
    double      get_tolerance() const { return m_tolerance; }
    void        set_tolerance(double t);
    double      get_head_fit() const { return m_head_fit; }
    void        set_head_fit(double f);
    double      true_diameter() const { return bore_diameter(); }
    bool        get_through() const { return m_through; }
    void        set_through(bool t);
    double      get_depth() const { return m_depth; }
    void        set_depth(double d);
    bool        get_flip() const { return m_flip; }
    void        set_flip(bool f);

    void gizmo_toggle_hole(int idx);
    void gizmo_apply_all();
    void gizmo_clear_all();
    void gizmo_refresh();

    // --- authoring a new pocket on a picked flat face (ME-2c) ---
    bool place_face_mode() const { return m_place_face_mode; }
    void set_place_face_mode(bool on);
    // Places a feature on the face under `screen_pos` using the current settings; stays in face
    // mode so several can be placed. Returns false when the ray misses.
    bool gizmo_place_face_at(const Vec2d& screen_pos);
    // Info about the face under `screen_pos`: facet index, size of its coplanar region, world normal.
    bool gizmo_face_info_at(const Vec2d& screen_pos, int& facet, int& region_facets, Vec3d& normal);
    int  placed_face_count() const { return int(m_placed.size()); }
    int  placed_face_id(int idx) const { return idx >= 0 && idx < int(m_placed.size()) ? m_placed[idx].id : -1; }
    DetectedHole placed_face(int idx) const { return idx >= 0 && idx < int(m_placed.size()) ? m_placed[idx].hole : DetectedHole(); }
    // Uniform object-to-world scale of the instance, so callers can report world-space sizes.
    double object_scale() const;

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_set_state() override;
    void on_render() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    CommonGizmosDataID on_get_requirements() const override;
    void on_set_hover_id() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;

private:
    struct HoleView
    {
        DetectedHole hole; // object coordinates (axis, center, radius, depth)
        bool         horizontal{ false };
    };

    ModelObject* model_object() const;
    int          object_idx() const;
    Transform3d  instance_matrix() const;
    Vec3d        object_up() const;
    double       teardrop_depth(const DetectedHole& hole) const;
    // Feature geometry for one hole, oriented from the chosen entry end. `dir` grows into the
    // material, `entry` is a point on the axis at the near end.
    void feature_frame(const DetectedHole& hole, Vec3d& dir, Vec3d& entry) const;
    double bore_diameter() const; // resolved from the standard / custom diameter + fit

    indexed_triangle_set teardrop_mesh(const DetectedHole& hole) const;
    indexed_triangle_set bore_negative_mesh(const DetectedHole& hole) const; // empty when nothing to cut
    indexed_triangle_set bore_tube_mesh(const DetectedHole& hole) const;     // empty unless shrinking
    indexed_triangle_set shape_mesh(const DetectedHole& hole) const;         // the preview shape for the operation

    void detect();
    void refresh_applied();
    void rebuild_previews();
    void register_pickers();

    void toggle_teardrop(int idx);
    void toggle_bore(int idx);
    void clear_all();

    // Face authoring: builds a synthetic hole from a picked face and adds it as a FacePocket volume.
    bool         place_face_at(const Vec2d& screen_pos);
    DetectedHole face_hole(const Vec3d& hit_world, const Vec3d& outward_normal) const;
    indexed_triangle_set face_shape_mesh(const DetectedHole& hole);
    void         update_face_highlight();
    void         build_face_highlight(const ModelVolume* mv, size_t facet);
    void         render_face_highlight();
    void         exit_place_face_mode();

    // Alt-held coordinate snap on the hovered face: corners, edge midpoints and the face centre.
    const std::vector<FaceSnapPoint>& face_snap_points(const ModelVolume* mv, size_t facet);
    Vec3d snap_face_hit(const Vec3d& hit_obj, const Vec2d& screen_pos, const ModelVolume* mv, size_t facet,
                        FaceSnapKind& kind);
    // Markers for the snap coordinates, shown while Alt is held. The three kind models plus the
    // bigger marker of the active one.
    void build_snap_markers();
    void render_snap_markers();
    void set_active_snap_marker(const FaceSnapPoint& p);

    void add_named_volume(int idx, const indexed_triangle_set& its, ModelVolumeType type, const std::string& name, bool snapshot);
    void remove_named_volumes(int idx, const char* name, const std::string& snapshot_name);

    std::vector<HoleView>             m_holes;
    std::vector<char>                 m_teardrop; // a teardrop negative matches this hole
    std::vector<char>                 m_bore;     // a pocket/bore volume matches this hole
    std::vector<indexed_triangle_set> m_pick_its;

    bool  m_dirty{ true };
    bool  m_preview_dirty{ true };
    float m_angle_deg{ 45.f };

    HoleOperation m_operation{ HoleOperation::Teardrop };
    HoleCategory  m_category{ HoleCategory::Screw };
    int           m_standard{ -1 };   // -1 = Custom, else index into hole_standards()
    BoreHead      m_head{ BoreHead::None };
    ScrewFit      m_screw_fit{ ScrewFit::Free };
    int           m_fit{ 1 };         // 0 tight, 1 slip, 2 epoxy
    double        m_diameter{ 5.0 };  // nominal diameter (editable)
    double        m_tolerance{ 0.0 }; // extra diameter; fit-derived and read-only for insert/magnet
    double        m_head_fit{ 0.0 };  // 0 flush, -0.08, -0.16: sink the head/pocket below the surface
    bool          m_through{ true };
    double        m_depth{ 10.0 };
    bool          m_flip{ false };

    // Candidate holes (blue), applied features (red), hovered hole (green).
    PickingModel m_preview_all;
    PickingModel m_preview_applied;
    PickingModel m_preview_hover;

    std::vector<std::unique_ptr<MeshRaycaster>>      m_pick_raycasters;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_pick_items;

    const ModelObject* m_old_model_object{ nullptr };
    int                m_old_volume_count{ -1 };
    Transform3d        m_old_instance_matrix{ Transform3d::Identity() };

    // Placed features authored on a picked face, tracked by their own monotonic id so they survive
    // re-detection and keep a stable volume name.
    struct PlacedFace
    {
        int          id{ 0 };
        DetectedHole hole;
    };

    bool                 m_place_face_mode{ false };
    GLModel              m_face_highlight;
    GLModel              m_face_ghost;
    const ModelVolume*   m_hover_face_mv{ nullptr };
    int                  m_hover_face_facet{ -1 };
    Vec3d                m_last_face_hit{ Vec3d::Constant(1e30) }; // object space, to detect movement
    FaceRegionCache      m_face_cache;
    const ModelVolume*   m_snap_mv{ nullptr };
    int                  m_snap_facet{ -1 };
    std::vector<FaceSnapPoint> m_snap_points;
    FaceSnapKind         m_hover_snap{ FaceSnapKind::None };
    // Hysteresis: hold the snapped coordinate while the cursor stays near it.
    FaceSnapPoint        m_snap_lock;
    double               m_snap_lock_tol{ 0.0 };
    bool                 m_snap_locked{ false };
    const ModelVolume*   m_snap_lock_mv{ nullptr };
    int                  m_snap_lock_facet{ -1 };
    double               m_snap_radius{ 0.0 };
    GLModel              m_snap_markers[3]; // indexed by int(FaceSnapKind) - 1
    GLModel              m_snap_marker_active;
    std::vector<PlacedFace> m_placed;
    int                  m_next_face_id{ 1 };
    indexed_triangle_set m_merged_its; // merged model-part mesh, used for through-depth marching

    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHoles_hpp_
