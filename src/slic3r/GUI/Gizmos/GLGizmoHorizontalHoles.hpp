#ifndef slic3r_GLGizmoHorizontalHoles_hpp_
#define slic3r_GLGizmoHorizontalHoles_hpp_

// [ORCAPORT:ME-1] Horizontal-hole picker. Detects cylindrical holes on the selected object
// (HoleDetector) and cuts a teardrop negative (HoleShapes) into the ones the user picks, so
// the top of a horizontal hole prints as a self-supporting ~45 degree roof instead of an
// unsupported bridge. Works on the mesh directly - no CAD reconstruction.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/HoleDetector.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;

namespace GUI {

class GLGizmoHorizontalHoles : public GLGizmoBase
{
public:
    GLGizmoHorizontalHoles(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;

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
        DetectedHole hole;
    };

    ModelObject* model_object() const;
    int          object_idx() const;
    Transform3d  instance_matrix() const;
    Vec3d        object_up() const;
    double       teardrop_depth(const DetectedHole& hole) const;
    indexed_triangle_set teardrop_mesh(int idx) const;

    void detect();          // (re)detect holes and refresh the derived state
    void refresh_applied(); // mark which holes already have a teardrop volume
    void rebuild_previews();
    void register_pickers();

    void toggle_hole(int idx);
    void apply_all(bool applied);
    void clear_all();
    void reapply_all();

    void add_teardrops(const std::vector<int>& idxs, const std::string& snapshot_name);
    void remove_teardrops(const std::vector<int>& idxs, const std::string& snapshot_name);

    std::vector<HoleView> m_holes;
    std::vector<char>     m_applied;
    std::vector<indexed_triangle_set> m_pick_its;

    bool  m_dirty{ true };
    bool  m_preview_dirty{ true };
    bool  m_angle_changed{ false };
    float m_angle_deg{ 45.f };

    PickingModel m_preview_applied;
    PickingModel m_preview_hover;

    std::vector<std::unique_ptr<MeshRaycaster>>      m_pick_raycasters;
    std::vector<std::shared_ptr<SceneRaycasterItem>> m_pick_items;

    // Signature of the last detection, so data_changed() only marks the gizmo dirty when the
    // geometry it depends on actually changed.
    const ModelObject* m_old_model_object{ nullptr };
    int                m_old_volume_count{ -1 };
    Transform3d        m_old_instance_matrix{ Transform3d::Identity() };

    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHorizontalHoles_hpp_
