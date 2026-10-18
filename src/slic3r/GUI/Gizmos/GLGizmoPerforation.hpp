#ifndef slic3r_GLGizmoPerforation_hpp_
#define slic3r_GLGizmoPerforation_hpp_

// [ORCAPORT:PP-1] Perforations tool. Stamps a 2D pattern (lines, grid, honeycomb, circles) into
// a picked planar face and adds it as a single negative volume, so the slicer subtracts it per
// layer. Depth is detected by the first surface the face normal crosses, so a hole goes through
// the local wall and stops at a cavity; a solid part is cut right through.
//
// [ORCAPORT:PP-2] It works like Extrude: a left click PICKS the face and cuts nothing, the panel's
// values reshape a live preview on that face, and the panel's Perforate button commits the cut. The
// old behaviour - every left click stamped the pattern where it landed - is gone; `gizmo_apply_at`
// is kept as pick + commit in one call so existing MCP scripts keep working.

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
    // [ORCAPORT:PP-2] Pick a face under `screen_pos` (a left click's job) and commit the current
    // pattern as a negative volume. `gizmo_apply_at` is pick + commit in one call, the shape the old
    // auto-apply behaviour had, so a script written for PP-1 still works.
    bool gizmo_pick_at(const Vec2d& screen_pos);
    bool gizmo_create();
    void gizmo_clear_pick();
    bool gizmo_pick_valid() const { return m_pick.valid; }
    int  gizmo_applied_count() const;
    bool gizmo_apply_at(const Vec2d& screen_pos);
    // Faces under `screen_pos`: facet index, coplanar-region size and world normal.
    bool gizmo_face_info_at(const Vec2d& screen_pos, int& facet, int& region_facets, Vec3d& normal);
    // [ORCAPORT:PP-6] The optional second face the pattern stops on ("To").
    bool gizmo_pick_target_at(const Vec2d& screen_pos);
    void gizmo_clear_target();
    bool gizmo_target_valid() const { return m_target.valid; }
    // Distance from the picked face to the target face, along the picked face's normal, in mm.
    double gizmo_target_distance() const { return m_target.distance; }
    // Empty when the last target pick was accepted; otherwise why it was refused.
    const char* gizmo_target_refusal() const;

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
    // [ORCAPORT:PP-2] The panel body, in its own function so the style push/pop in
    // on_render_input_window() cannot be crossed by an early return (docs/handoff.md section 28).
    void render_panel_body();
    ExPolygons face_domain(const ModelVolume* mv, size_t facet, const Vec3d& normal_obj, const Vec3d& origin_obj);
    bool build_pattern_at(const Vec3d& hit_obj, const ModelVolume* mv, size_t facet,
                          ExPolygons& pattern, indexed_triangle_set& prism);
    // [ORCAPORT:PP-2] The overlays. The first two are patches on the surface; the last two are
    // object-space prisms: the proposed pattern (what the values would cut) and every committed one.
    // [ORCAPORT:PP-6] The To face is a third patch, in the picked colour.
    void rebuild_hover_highlight();
    void rebuild_picked_highlight();
    void rebuild_proposed_pattern();
    void rebuild_applied_patterns();
    void pick_at(const Vec2d& screen_pos);
    void clear_pick();
    bool create_from_pick();
    bool apply_at(const Vec2d& screen_pos);   // kept for the MCP alias: pick, then commit
    void add_pattern_volume(const indexed_triangle_set& prism);
    // [ORCAPORT:PP-6] The "To" target: a second face, parallel to the picked one, that decides how
    // deep the pattern goes instead of the depth probe's first surface.
    void pick_target_at(const Vec2d& screen_pos);
    void clear_target();
    void rebuild_target_highlight();

    // [ORCAPORT:PP-2] The picked face. The pattern is anchored where the user clicked and only the
    // panel's values move it, so the anchor is cached here instead of being recomputed from the
    // cursor every frame (which is what the old hover ghost did).
    struct Pick
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        Vec3d              hit_obj{ Vec3d::Zero() };
        bool               horizontal{ false };
    };

    // [ORCAPORT:PP-6] The target face. Gizmo state, not a PerforationParams value: it is a face
    // reference, not a number, so the MCP tool reaches it with its own pick action rather than through
    // set_params, the same way the picked face does.
    struct Target
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        double             distance{ 0. };  // picked face -> this face, along the picked normal, in mm
    };

    // Why the last target pick was refused. The panel turns it into a translated warning, the MCP tool
    // into plain text, so neither has to parse the other's strings.
    enum class TargetWhy { None, NoFace, NotParallel, NotBehind };

    PerforationParams    m_params;
    GLModel              m_face_highlight;     // the surface under the cursor (teal)
    GLModel              m_face_picked;        // the picked surface (green)
    GLModel              m_pattern_proposed;   // the pattern the current values would cut (yellow)
    GLModel              m_patterns_applied;   // every committed perforation (red)
    GLModel              m_face_target;        // the "To" face, in the picked colour
    Pick                 m_pick;
    Target               m_target;
    TargetWhy            m_target_why{ TargetWhy::None };
    // True while the panel's Pick face button is armed: the next left click sets the target face
    // instead of re-picking the one the pattern is anchored to. The panel button (or a right click,
    // or closing the tool) disarms it.
    bool                 m_pick_target{ false };
    // The proposed pattern is rebuilt whenever this is set: a new pick, or a value change. [ORCAPORT:PP-6]
    // A slider sets it on every frame it changes, so the preview follows a drag live. One mesh per hole
    // is not free, so a tiny spacing on a large face rebuilds slowly - the builder's own MAX_FEATURES cap
    // bounds it. A frame-time guard is the upgrade path if that ever bites.
    bool                 m_pick_dirty{ true };
    // False while the proposal IS what was just cut: the red applied overlay already marks that area,
    // and yellow over red reads as neither colour. Any value change (or a new pick) brings it back.
    bool                 m_show_proposal{ true };
    const ModelVolume*   m_hover_mv{ nullptr };
    int                  m_hover_facet{ -1 };
    bool                 m_hover_horizontal{ false };
    Vec3d                m_last_hit{ Vec3d::Constant(1e30) };
    std::vector<int>     m_hover_region;
    FaceRegionCache      m_face_cache;
    indexed_triangle_set m_merged_its;
    bool                 m_merged_dirty{ true };
    int                  m_old_volume_count{ -1 };
    const ModelObject*   m_old_model_object{ nullptr };
    std::map<std::string, wxString> m_desc;
};

} // namespace GUI
} // namespace Slic3r

#endif // slic3r_GLGizmoPerforation_hpp_
