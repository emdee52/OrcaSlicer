#ifndef slic3r_GLGizmoCounterboreBridge_hpp_
#define slic3r_GLGizmoCounterboreBridge_hpp_

// [ORCAPORT:PF-2b] Paint-on counterbore bridge gizmo (ported from preFlight's
// GLGizmoCounterboreBridge, adapted to Orca's painter base). Painted regions drive the smart
// counterbore bridging (PF-2a) or a single partial bridge layer.
// [ORCAPORT:RF-1] Also hosts "Strengthen hole": a per-hole localized PARAMETER_MODIFIER that
// raises the local wall count so a screw bites into solid plastic instead of splitting the part.

#include "GLGizmoPainterBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/HoleDetector.hpp"

#include "slic3r/GUI/I18N.hpp"

#include <map>
#include <memory>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
enum class ModelVolumeType : int;

namespace GUI {

class GLGizmoCounterboreBridge : public GLGizmoPainterBase
{
public:
    GLGizmoCounterboreBridge(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void render_painter_gizmo() override;

    // Called by the manager on every state change so the pick raycasters follow the mode.
    void on_set_state() override;
    bool on_mouse(const wxMouseEvent& mouse_event) override;

    // --- "Strengthen hole" mode ---------------------------------------------------------------
    // The strengthening sleeve is a PARAMETER_MODIFIER, not a painted region.
    bool strengthen_mode() const { return m_strengthen; }
    void set_strengthen_mode(bool on);

    int          hole_count();        // runs detection if needed, 0 when not in strengthen mode
    DetectedHole hole(int idx) const; // object-space hole
    bool         hole_vertical(int idx) const;
    bool         hole_has_reinforce(int idx) const;

    double get_reinforce_thickness() const { return m_reinforce_thickness; }
    void   set_reinforce_thickness(double mm);
    int    get_reinforce_loops() const { return m_reinforce_loops; }
    void   set_reinforce_loops(int n);

    void gizmo_toggle_hole(int idx);
    void gizmo_reinforce_all();
    void gizmo_clear_reinforce();
    void gizmo_refresh();

protected:
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

    void on_set_hover_id() override;
    void on_register_raycasters_for_picking() override;
    void on_unregister_raycasters_for_picking() override;

    wxString handle_snapshot_action_name(bool shift_down, Button button_down) const override;

    std::string get_gizmo_entering_text() const override { return _u8L("Entering counterbore bridge painting"); }
    std::string get_gizmo_leaving_text() const override { return _u8L("Leaving counterbore bridge painting"); }
    std::string get_action_snapshot_name() const override { return _u8L("Counterbore bridge editing"); }

    // Smart = ENFORCER state, Partial = BLOCKER state (in the counterbore annotation only).
    EnforcerBlockerType get_left_button_state_type() const override {
        return m_partial ? EnforcerBlockerType::BLOCKER : EnforcerBlockerType::ENFORCER;
    }
    EnforcerBlockerType get_right_button_state_type() const override { return EnforcerBlockerType::NONE; }

private:
    bool on_init() override;
    bool on_is_selectable() const override { return true; }
    void on_opening() override {}
    void on_shutdown() override;
    void update_model_object() override;
    void update_from_model_object(bool first_update) override;
    PainterGizmoType get_painter_type() const override;

    // --- strengthen-mode helpers (object space, like the holes gizmo) ---
    struct HoleView {
        DetectedHole hole;
        bool         vertical = false;
    };

    ModelObject* model_object() const;
    int          object_idx() const;
    Transform3d  instance_matrix() const;
    Vec3d        object_up() const;
    double       object_scale() const;
    int          effective_wall_loops() const;

    indexed_triangle_set reinforce_mesh(const DetectedHole& hole) const;
    indexed_triangle_set make_pick_cylinder(const DetectedHole& hole, const Vec3d& up) const;

    void detect();
    void refresh_applied();
    void register_pickers();
    ModelVolume* add_named_volume(const indexed_triangle_set& its, const std::string& name, bool snapshot);
    void remove_named_volumes(const std::string& prefix, const std::string& snapshot_name);
    void toggle_reinforce(int idx);
    void clear_all_reinforce();

    // false = smart stepped bridging, true = partial (single-layer) bridging.
    bool m_partial = false;

    // Strengthen-hole mode state.
    bool                    m_strengthen = false;
    std::vector<HoleView>   m_holes;
    std::vector<char>       m_reinforce;
    std::vector<indexed_triangle_set> m_pick_its;
    std::vector<std::unique_ptr<MeshRaycaster>>         m_pick_raycasters;
    std::vector<std::shared_ptr<SceneRaycasterItem>>    m_pick_items;
    bool                    m_dirty = false;
    double                  m_reinforce_thickness = 1.5;
    int                     m_reinforce_loops     = 1;
    ModelObject*            m_old_model_object = nullptr;
    int                     m_old_volume_count  = -1;
    Transform3d             m_old_instance_matrix;

    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoCounterboreBridge_hpp_
