#ifndef slic3r_GLGizmoMeshBoolean_hpp_
#define slic3r_GLGizmoMeshBoolean_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/Point.hpp"
#include <optional>
#include <string>
#include <wx/event.h>
#include "libslic3r/TriangleMesh.hpp"
#include <vector>

namespace Slic3r {

namespace GUI {

enum class MeshBooleanSelectingState {
    Undef,
    SelectSource,
    SelectTool,

};
enum class MeshBooleanOperation{
    Undef,
    Union,
    Difference,
    Intersection,
};
struct VolumeInfo {
    ModelVolume* mv{ nullptr };
    int volume_idx{-1};
    Transform3d trafo;
    void reset() {
        mv = nullptr;
        volume_idx = -1;
        trafo = Transform3d::Identity();
    };
    template<class Archive>
    void serialize(Archive& ar) {
        ar(volume_idx, trafo);
    }

    std::optional<TriangleSelector::SavedPainting> save_painting() const;
};
class GLGizmoMeshBoolean : public GLGizmoBase
{
public:
    GLGizmoMeshBoolean(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);
    ~GLGizmoMeshBoolean();

    void set_enable(bool enable) { m_enable = enable; }
    bool get_enable() { return m_enable; }
    MeshBooleanSelectingState get_selecting_state() { return m_selecting_state; }

    // [ORCAPORT:MCP-3] Machine-drivable surface for the mesh_boolean_gizmo MCP tool. The picks run the
    // same raycast an interactive click does in gizmo_event(); pick_volume_index() sets a volume
    // directly, which is the only way to pick one that no camera ray reaches (a cutter buried inside
    // the part). perform_operation() is the same code the Difference/Union/Intersection buttons run.
    bool pick_at(const Vec2d &mouse_position, bool as_tool);
    bool pick_volume_index(int volume_idx, bool as_tool);
    bool perform_operation();
    // The closest volume under a screen point, by the same raycast gizmo_event() uses. Bounded by
    // both arrays, and the count of raycasters is exposed: the raycasters and mo->volumes disagreeing
    // after a volume-list change is the suspected cause of a pick that misses (docs/handoff.md 37).
    int raycast_volume(const Vec2d &mouse_position) const;
    int get_raycaster_count() const;
    void set_operation(MeshBooleanOperation op) { m_operation_mode = op; }
    MeshBooleanOperation get_operation() const { return m_operation_mode; }
    void set_delete_input(bool v) { m_diff_delete_input = v; m_inter_delete_input = v; }
    bool get_delete_input() const { return m_diff_delete_input; }
    const ModelVolume *get_src_volume() const { return m_src.mv; }
    const ModelVolume *get_tool_volume() const { return m_tool.mv; }
    int get_src_index() const { return m_src.volume_idx; }
    int get_tool_index() const { return m_tool.volume_idx; }
    void set_src_volume(ModelVolume* mv) { 
        m_src.mv = mv;
        if (m_src.mv == m_tool.mv)
            m_tool.reset();
    }
    void set_tool_volume(ModelVolume* mv) { 
        m_tool.mv = mv;
        if (m_tool.mv == m_src.mv)
            m_src.reset();
    }

    bool gizmo_event(SLAGizmoEventType action, const Vec2d &mouse_position, bool shift_down, bool alt_down, bool control_down);

    /// <summary>
    /// Implement when want to process mouse events in gizmo
    /// Click, Right click, move, drag, ...
    /// </summary>
    /// <param name="mouse_event">Keep information about mouse click</param>
    /// <returns>Return True when use the information and don't want to
    /// propagate it otherwise False.</returns>
    bool on_mouse(const wxMouseEvent &mouse_event) override;

protected:
    virtual bool on_init() override;
    virtual std::string on_get_name() const override;
    virtual bool on_is_activable() const override;
    virtual void on_render() override;
    virtual void on_set_state() override;
    virtual CommonGizmosDataID on_get_requirements() const override;
    virtual void on_render_input_window(float x, float y, float bottom_limit) override;

    void on_load(cereal::BinaryInputArchive &ar) override;
    void on_save(cereal::BinaryOutputArchive &ar) const override;

private:
    bool m_enable{ false };
    MeshBooleanOperation m_operation_mode;
    MeshBooleanSelectingState m_selecting_state;
    bool m_diff_delete_input = false;
    bool m_inter_delete_input = false;
    VolumeInfo m_src;
    VolumeInfo m_tool;

    void generate_new_volume(bool delete_input, TriangleMesh& mesh_result, const std::vector<std::optional<TriangleSelector::SavedPainting>>& saved_paintings);
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoMeshBoolean_hpp_