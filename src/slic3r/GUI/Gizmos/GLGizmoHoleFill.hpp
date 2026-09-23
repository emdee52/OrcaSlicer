#ifndef slic3r_GLGizmoHoleFill_hpp_
#define slic3r_GLGizmoHoleFill_hpp_

// [ORCAPORT:HF-1] Cavity fill tool. Fills a depression cut into the mesh (an engraving, a watermark,
// a pocket) by adding a positive MODEL_PART volume over it. Set a wall reference (drag on the wall
// around the mark), then paint the recessed faces: the plug is those facets projected up to the
// reference plane, so it sits flush and cannot bridge onto the surrounding surface. Painting a
// surface that is not behind the reference plane does nothing. Works on the mesh directly - no
// boolean and no CAD reconstruction. Handling full through-holes is deliberately deferred.

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
    double get_depth() const { return m_depth; }
    void   set_depth(double depth);
    bool   has_reference() const { return m_has_reference; }
    void   clear_reference();
    bool   gizmo_set_reference_at(const Vec2d& screen_pos);
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
    // The plug for the hovered face, or for a whole brush stroke, in object space; empty when there
    // is nothing to fill.
    indexed_triangle_set fill_mesh(const Hover& hover) const;
    indexed_triangle_set fill_mesh(const std::vector<Hover>& seeds) const;
    void                 record_stroke_sample();
    void                 commit_stroke();
    // Wall-reference picking: collect wall points onto the plane, then fit it.
    void sample_reference();
    void finish_reference();
    // Volume index of an applied plug that already covers `hit_world`, or -1.
    int          find_covering_fill(const Vec3d& hit_world) const;
    void         rebuild_preview();
    void         add_named_fill(const std::string& name, const indexed_triangle_set& its);
    int          next_feature_id() const;

    double m_radius{ 5.0 }; // in world mm, converted to mesh units per volume
    double m_depth{ 0.15 }; // fill depth behind the reference plane, in world mm
    std::map<std::string, wxString> m_desc;

    Hover        m_hover;
    int          m_hover_applied{ -1 };
    bool         m_painting{ false };
    std::vector<Hover> m_stroke;

    // Wall reference, in world space. While set, the brush fills only facets behind it, so a curved
    // or finely tessellated wall cannot leak the selection past the mark.
    bool         m_setting_reference{ false };
    bool         m_ref_painting{ false };
    bool         m_has_reference{ false };
    Vec3d        m_ref_point{ Vec3d::Zero() };
    Vec3d        m_ref_normal{ Vec3d::UnitZ() };
    std::vector<Vec3d> m_ref_points;
    std::vector<Vec3d> m_ref_normals;
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
