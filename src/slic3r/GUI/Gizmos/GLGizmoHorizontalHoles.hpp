#ifndef slic3r_GLGizmoHorizontalHoles_hpp_
#define slic3r_GLGizmoHorizontalHoles_hpp_

// [ORCAPORT:ME-1] Horizontal-hole picker. Detects cylindrical holes on the selected object
// (HoleDetector) and, per hole, either cuts a teardrop negative (HoleShapes) or paints the
// hole's upper arc into the volume's counterbore-bridge annotation, so the slicer bridges over
// it. Works on the mesh directly - no CAD reconstruction.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/HoleDetector.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
class ModelVolume;

namespace GUI {

class GLGizmoHorizontalHoles : public GLGizmoBase
{
public:
    GLGizmoHorizontalHoles(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    // --- control surface for the embedded MCP tools (see OrcaMCPGizmoTools.cpp) ---
    int           hole_count();                 // runs detection if needed
    DetectedHole  hole(int idx) const;          // object-space hole
    bool          hole_has_teardrop(int idx) const;
    bool          hole_has_bridge(int idx) const;
    bool          bridge_mode() const { return m_bridge_mode; }
    void          set_bridge_mode(bool on);
    float         get_angle() const { return m_angle_deg; }
    void          set_angle(float deg);
    void          gizmo_toggle_hole(int idx);   // applies/removes the current mode
    void          gizmo_apply_all();
    void          gizmo_clear_all();
    void          gizmo_refresh();

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
        DetectedHole     hole;          // object coordinates (axis, center, radius, depth)
        int              volume_idx{ -1 };
        std::vector<int> upper_facets;  // facet indices in the volume's local mesh (upper arc)
    };

    ModelObject* model_object() const;
    int          object_idx() const;
    Transform3d  instance_matrix() const;
    Vec3d        object_up() const;
    double       teardrop_depth(const DetectedHole& hole) const;

    void detect();
    void refresh_applied();
    void rebuild_previews();
    void register_pickers();

    indexed_triangle_set teardrop_mesh(int idx) const;
    indexed_triangle_set upper_arc_mesh(int idx) const;

    void toggle_teardrop(int idx);
    void toggle_bridge(int idx);
    void clear_all();
    void reapply_teardrops();

    void add_teardrop_volume(int idx, const std::string& snapshot_name);
    void remove_teardrop_volume(int idx, const std::string& snapshot_name);
    void paint_bridge(int idx, bool on, const std::string& snapshot_name);

    std::vector<HoleView> m_holes;
    std::vector<char>     m_teardrop;  // a teardrop negative matches this hole
    std::vector<char>     m_bridge;    // the hole's upper arc is painted

    std::vector<indexed_triangle_set> m_pick_its;

    bool  m_dirty{ true };
    bool  m_preview_dirty{ true };
    bool  m_angle_changed{ false };
    bool  m_bridge_mode{ false };
    float m_angle_deg{ 45.f };

    PickingModel m_preview_all;
    PickingModel m_preview_teardrop;
    PickingModel m_preview_bridge;
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

#endif // slic3r_GLGizmoHorizontalHoles_hpp_
