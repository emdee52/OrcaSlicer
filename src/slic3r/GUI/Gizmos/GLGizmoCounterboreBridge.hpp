#ifndef slic3r_GLGizmoCounterboreBridge_hpp_
#define slic3r_GLGizmoCounterboreBridge_hpp_

// [ORCAPORT:PF-2b] Paint-on counterbore bridge gizmo (ported from preFlight's
// GLGizmoCounterboreBridge, adapted to Orca's painter base). Painted regions drive the smart
// counterbore bridging (PF-2a) or a single partial bridge layer.

#include "GLGizmoPainterBase.hpp"

#include "slic3r/GUI/I18N.hpp"

namespace Slic3r::GUI {

class GLGizmoCounterboreBridge : public GLGizmoPainterBase
{
public:
    GLGizmoCounterboreBridge(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void render_painter_gizmo() override;

protected:
    void        on_render_input_window(float x, float y, float bottom_limit) override;
    std::string on_get_name() const override;

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

    // false = smart stepped bridging, true = partial (single-layer) bridging.
    bool m_partial = false;

    std::map<std::string, wxString> m_desc;
};

} // namespace Slic3r::GUI

#endif // slic3r_GLGizmoCounterboreBridge_hpp_
