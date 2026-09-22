#ifndef slic3r_GLGizmoEdgeDress_hpp_
#define slic3r_GLGizmoEdgeDress_hpp_

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"

#include "libslic3r/TriangleMesh.hpp"

#include <array>
#include <map>
#include <string>
#include <vector>

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace GUI {

// [ORCAPORT:EF-1] Chamfer or fillet a straight mesh edge with a negative prism.
enum class EdgeDressMode { Chamfer, Fillet };

class GLGizmoEdgeDress : public GLGizmoBase
{
public:
    GLGizmoEdgeDress(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    // MCP control surface.
    EdgeDressMode get_mode() const { return m_mode; }
    void          set_mode(EdgeDressMode mode);
    double        get_size() const { return m_size; }
    void          set_size(double size);
    bool          hover_edge_valid() const { return m_hover.valid; }
    double        object_scale() const;
    int           applied_count() const;
    bool          gizmo_hover_at(const Vec2d& screen_pos);
    bool          gizmo_apply_hovered();
    bool          gizmo_apply_at(const Vec2d& screen_pos);
    void          gizmo_clear_all();
    void          gizmo_refresh();

    // Deterministic, screen-coordinate-free access used by tests and MCP: the edges of the first
    // model part, indexed in mesh order. Each entry is {p0.x, p0.y, p0.z, p1.x, p1.y, p1.z}.
    int                                 edge_count() const;
    std::vector<std::array<double, 6>>  gizmo_list_edges(int max_count) const;
    bool                                gizmo_apply_edge(int index);

    void data_changed(bool is_serializing) override;
    bool render_follows_cursor() const override { return get_state() == On; }

protected:
    bool                on_init() override;
    std::string         on_get_name() const override;
    bool                on_is_activable() const override;
    void                on_set_state() override;
    void                on_render() override;
    void                on_render_input_window(float x, float y, float bottom_limit) override;
    bool                on_mouse(const wxMouseEvent& mouse_event) override;
    CommonGizmosDataID  on_get_requirements() const override;
    void                on_register_raycasters_for_picking() override;
    void                on_unregister_raycasters_for_picking() override;

private:
    // The picked edge in object space: endpoints, edge direction and the two adjacent face normals.
    struct HoverEdge
    {
        bool  valid{false};
        Vec3d p0{Vec3d::Zero()}, p1{Vec3d::Zero()}, n_a{Vec3d::Zero()}, n_b{Vec3d::Zero()};
    };

    const ModelObject* model_object() const;
    Transform3d        instance_matrix() const;
    void               update_hover(const Vec2d& screen_pos);
    void               clear_hover();
    indexed_triangle_set hover_mesh() const;
    indexed_triangle_set mesh_for_edge(const HoverEdge& edge) const;
    std::vector<HoverEdge> collect_edges() const;
    // The merged geometric edges of the selected part, cached until the object changes.
    const std::vector<HoverEdge>& cached_edges() const;
    void               rebuild_preview();
    void               add_named_negative(const std::string& name, const indexed_triangle_set& its);
    int                next_feature_id() const;

    EdgeDressMode m_mode{EdgeDressMode::Chamfer};
    double        m_size{1.0};
    std::map<std::string, wxString> m_desc;

    HoverEdge    m_hover;
    PickingModel m_preview;
    bool         m_preview_dirty{true};

    // Cached merged edges, in object space, rebuilt when the object or its volume list changes.
    mutable std::vector<HoverEdge> m_edges;
    mutable bool                   m_edges_dirty{true};

    const ModelObject* m_old_object{nullptr};
    int                m_old_volume_count{-1};
    Transform3d        m_old_matrix{Transform3d::Identity()};
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoEdgeDress_hpp_
