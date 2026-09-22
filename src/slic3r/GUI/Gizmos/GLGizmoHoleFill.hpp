#ifndef slic3r_GLGizmoHoleFill_hpp_
#define slic3r_GLGizmoHoleFill_hpp_

// [ORCAPORT:HF-1] Cavity fill tool. Fills a depression cut into the mesh (an engraving, a watermark,
// a pocket) by adding a positive MODEL_PART volume over it. The plug is the convex hull of the mesh
// facets under the cursor, so it works both on a flat face and on a curved wall. Works on the mesh
// directly - no boolean and no CAD reconstruction. Handling strongly concave outer surfaces, and
// full through-holes, is deliberately deferred: the hull can bridge a valley or a hole.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace GUI {

class GLGizmoHoleFill : public GLGizmoBase
{
public:
    GLGizmoHoleFill(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool render_follows_cursor() const override { return get_state() == On; }

    // --- control surface for the embedded MCP tool (see OrcaMCPGizmoTools.cpp) ---
    double get_radius() const { return m_radius; }
    void   set_radius(double radius);
    double object_scale() const;
    int    applied_count() const;
    bool   hover_valid() const { return m_hover.valid; }
    bool   gizmo_hover_at(const Vec2d& screen_pos);
    bool   gizmo_apply_hovered();
    bool   gizmo_apply_at(const Vec2d& screen_pos);
    bool   gizmo_remove_hovered();
    bool   gizmo_remove_fill(int index);
    void   gizmo_clear_all();
    void   gizmo_refresh();
    // Centre of every applied plug, in object space, so a caller can see what is filled.
    std::vector<std::array<double, 3>> gizmo_list_fills() const;

protected:
    bool               on_init() override;
    std::string        on_get_name() const override;
    bool               on_is_activable() const override;
    void               on_set_state() override;
    void               on_render() override;
    void               on_render_input_window(float x, float y, float bottom_limit) override;
    bool               on_mouse(const wxMouseEvent& mouse_event) override;
    CommonGizmosDataID on_get_requirements() const override;
    void               on_register_raycasters_for_picking() override;
    void               on_unregister_raycasters_for_picking() override;

private:
    // The picked face in object space, plus the seed point in the volume's own mesh space, which is
    // what cavity_fill_hull expects.
    struct Hover
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        Vec3d              hit_world{ Vec3d::Zero() };
        Vec3d              seed_local{ Vec3d::Zero() };
    };

    ModelObject* model_object() const;
    Transform3d  instance_matrix() const;
    double       volume_scale(const ModelVolume* mv) const;
    void         update_hover(const Vec2d& screen_pos);
    void         clear_hover();
    // The plug for the hovered face, in object space; empty when there is nothing to fill.
    indexed_triangle_set fill_mesh(const Hover& hover) const;
    // Volume index of an applied plug that already covers `hit_world`, or -1.
    int          find_covering_fill(const Vec3d& hit_world) const;
    void         rebuild_preview();
    void         add_named_fill(const std::string& name, const indexed_triangle_set& its);
    int          next_feature_id() const;

    double m_radius{ 5.0 }; // in world mm, converted to mesh units per volume
    std::map<std::string, wxString> m_desc;

    Hover        m_hover;
    int          m_hover_applied{ -1 };
    PickingModel m_preview;
    PickingModel m_preview_applied;
    bool         m_preview_dirty{ true };

    const ModelObject* m_old_object{ nullptr };
    int                m_old_volume_count{ -1 };
    Transform3d        m_old_matrix{ Transform3d::Identity() };

    // The hover is rebuilt only when the cursor or the camera moved, so a still cursor costs nothing.
    bool        m_hover_computed{ false };
    Vec2d       m_last_mouse{ -1., -1. };
    Transform3d m_last_view{ Transform3d::Identity() };
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoHoleFill_hpp_
