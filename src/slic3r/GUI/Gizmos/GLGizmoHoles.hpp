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
enum class BoreHead { None, Counterbore, Countersink };

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
    BoreHead    get_head() const { return m_head; }
    void        set_head(BoreHead h);
    HoleFit     get_fit() const { return HoleFit(m_fit); }
    void        set_fit(HoleFit f);
    double      get_diameter() const { return m_custom_d; }
    void        set_diameter(double d);
    bool        get_through() const { return m_through; }
    void        set_through(bool t);
    bool        get_flip() const { return m_flip; }
    void        set_flip(bool f);

    void gizmo_toggle_hole(int idx);
    void gizmo_apply_all();
    void gizmo_clear_all();
    void gizmo_refresh();

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
    void feature_frame(int idx, Vec3d& dir, Vec3d& entry) const;
    double bore_diameter() const; // resolved from the standard / custom diameter + fit

    indexed_triangle_set teardrop_mesh(int idx) const;
    indexed_triangle_set bore_negative_mesh(int idx) const; // empty when nothing to cut
    indexed_triangle_set bore_tube_mesh(int idx) const;     // empty unless shrinking
    indexed_triangle_set shape_mesh(int idx) const;         // the preview shape for the operation

    void detect();
    void refresh_applied();
    void rebuild_previews();
    void register_pickers();

    void toggle_teardrop(int idx);
    void toggle_bore(int idx);
    void clear_all();
    void reapply_teardrops();
    void reapply_bores();

    void add_named_volume(int idx, const indexed_triangle_set& its, ModelVolumeType type, const std::string& name, bool snapshot);
    void remove_named_volumes(int idx, const char* name, const std::string& snapshot_name);

    std::vector<HoleView>             m_holes;
    std::vector<char>                 m_teardrop; // a teardrop negative matches this hole
    std::vector<char>                 m_bore;     // a pocket/bore volume matches this hole
    std::vector<indexed_triangle_set> m_pick_its;

    bool  m_dirty{ true };
    bool  m_preview_dirty{ true };
    bool  m_angle_changed{ false };
    bool  m_bore_changed{ false };
    float m_angle_deg{ 45.f };

    HoleOperation m_operation{ HoleOperation::Teardrop };
    int           m_standard{ -1 };          // -1 = Custom, else index into hole_standards()
    BoreHead      m_head{ BoreHead::None };
    int           m_fit{ 1 };                // 0 tight, 1 slip, 2 epoxy
    double        m_custom_d{ 5.0 };
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

    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHoles_hpp_
