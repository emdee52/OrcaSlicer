#ifndef slic3r_GLGizmoPerforation_hpp_
#define slic3r_GLGizmoPerforation_hpp_

// [ORCAPORT:PP-1] Perforations tool. Stamps a 2D pattern (lines, grid, honeycomb, circles) into
// a picked planar face and adds it as a single negative volume, so the slicer subtracts it per
// layer. Depth is detected by the first surface the face normal crosses, so a hole goes through
// the local wall and stops at a cavity; a solid part is cut right through.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "libslic3r/PerforationPattern.hpp"

#include <map>
#include <string>
#include <vector>

namespace Slic3r {
class ModelObject;
class ModelVolume;
enum class ModelVolumeType : int;

namespace GUI {

class GLGizmoPerforation : public GLGizmoBase
{
public:
    GLGizmoPerforation(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    bool on_mouse(const wxMouseEvent& mouse_event) override;
    // Hover changes the highlight, so the frame must not be reused from the scene cache.
    bool render_follows_cursor() const override { return get_state() == On; }

    // Control surface for the embedded MCP tools (see OrcaMCPGizmoTools.cpp).
    void                     set_params(const PerforationParams& p);
    const PerforationParams& params() const { return m_params; }
    // Perforates the face under `screen_pos`. Same path as a left click. False when the ray
    // misses or the pattern is empty.
    bool gizmo_apply_at(const Vec2d& screen_pos);
    // Faces under `screen_pos`: facet index, coplanar-region size and world normal.
    bool gizmo_face_info_at(const Vec2d& screen_pos, int& facet, int& region_facets, Vec3d& normal);

protected:
    bool on_init() override;
    std::string on_get_name() const override;
    bool on_is_activable() const override;
    void on_set_state() override;
    void data_changed(bool is_serializing) override;
    void on_render() override;
    void on_render_input_window(float x, float y, float bottom_limit) override;
    CommonGizmosDataID on_get_requirements() const override;

private:
    ModelObject* model_object() const;
    Transform3d  instance_matrix() const;

    void rebuild_merged_mesh();
    void clear_hover();
    void update_hover();
    ExPolygons face_domain(const ModelVolume* mv, size_t facet, const Vec3d& normal_obj, const Vec3d& origin_obj);
    bool build_pattern_at(const Vec3d& hit_obj, const ModelVolume* mv, size_t facet,
                          ExPolygons& pattern, indexed_triangle_set& prism);
    bool apply_at(const Vec2d& screen_pos);
    void add_pattern_volume(const indexed_triangle_set& prism);

    PerforationParams    m_params;
    GLModel              m_face_highlight;
    GLModel              m_face_ghost;
    const ModelVolume*   m_hover_mv{ nullptr };
    int                  m_hover_facet{ -1 };
    bool                 m_hover_horizontal{ false };
    Vec3d                m_last_hit{ Vec3d::Constant(1e30) };
    std::vector<int>     m_hover_region;
    FaceRegionCache      m_face_cache;
    indexed_triangle_set m_merged_its;
    bool                 m_merged_dirty{ true };
    // True while a slider is being dragged: the ghost is not rebuilt until release, so the
    // preview cannot flicker and the params-driven notice settles after the drag.
    bool                 m_editing{ false };
    int                  m_old_volume_count{ -1 };
    const ModelObject*   m_old_model_object{ nullptr };
    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoPerforation_hpp_
