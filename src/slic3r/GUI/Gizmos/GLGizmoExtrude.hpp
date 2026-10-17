#ifndef slic3r_GLGizmoExtrude_hpp_
#define slic3r_GLGizmoExtrude_hpp_

// [ORCAPORT:HF-1] [ORCAPORT:HF-3] Extrude tool. Two jobs in one, chosen by the mode selector:
//
//   Extrude face - pick a flat face and turn it into a solid: the face is grown into a coplanar
//   region, projected to a polygon outline, and closed into a prism of a given height, grown or
//   shrunk by an offset. Optionally only the face's holes are extruded (each one filled), and
//   optionally the prism is split half out of the face and half into the model.
//
//   Cavity fill - fill a depression cut into the mesh (an engraving, a watermark, a pocket) by
//   adding a positive MODEL_PART volume over it. Paint the recessed faces: every facet is measured
//   against the surface fitted to its own neighbourhood, so the plug is those facets projected up to
//   that local surface, sits flush, and follows a curved or coarsely facetted wall without any
//   reference to set. A facet level with its neighbourhood (a plain wall) is left alone.
//
// Both work on the mesh directly - no boolean and no CAD reconstruction. Both add an ordinary
// positive MODEL_PART volume, so one undo removes the result.
//
// Renamed from GLGizmoHoleFill when the extrude mode was added. Volumes are written with the
// "Extrude#" prefix; the legacy "HoleFill#" prefix is still recognised on read so fills made before
// the rename stay editable.

#include "GLGizmoBase.hpp"
#include "GLGizmosCommon.hpp"
#include "GizmoStyle.hpp"   // [ORCAPORT:HF-3] the shared panel widget layer

#include "libslic3r/HoleShapes.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include <array>
#include <map>
#include <string>
#include <tuple>
#include <vector>

namespace Slic3r {

class ModelObject;
class ModelVolume;

namespace GUI {

class GLGizmoExtrude : public GLGizmoBase
{
public:
    GLGizmoExtrude(GLCanvas3D& parent, const std::string& icon_filename, unsigned int sprite_id);

    void data_changed(bool is_serializing) override;
    bool render_follows_cursor() const override { return get_state() == On; }

    // --- control surface for the embedded MCP tool (see OrcaMCPGizmoTools.cpp) ---
    // [ORCAPORT:HF-3] The tool has two modes; the MCP surface drives both.
    enum class Mode { ExtrudeFace = 0, CavityFill = 1 };
    Mode   get_mode() const { return m_mode; }
    void   set_mode(Mode m);
    double get_radius() const { return m_radius; }
    void   set_radius(double radius);
    // [ORCAPORT:HF-3] Click a face to PICK it; Extrude commits. `apply_hovered` and `apply_at` are
    // kept as aliases so MCP scripts written before the rework still work: they now pick, then
    // commit, in one call.
    bool   pick_at(const Vec2d& screen_pos);
    void   clear_pick();
    bool   pick_valid() const { return m_pick.valid; }
    bool   create_from_pick();
    double get_extrude_height() const { return m_extrude_height; }
    void   set_extrude_height(double h);
    double get_extrude_offset() const { return m_extrude_offset; }
    void   set_extrude_offset(double off);
    bool   get_extrude_holes() const { return m_extrude_holes; }
    void   set_extrude_holes(bool on);
    bool   get_extrude_both() const { return m_extrude_both; }
    void   set_extrude_both(bool on);
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
    // what cavity_fill_local expects.
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

    // [ORCAPORT:HF-3] The PICK. Clicking a face selects it; Height/Offset then reshape the preview
    // and nothing is committed until the panel's Extrude button.
    //
    // Everything that does NOT depend on Height/Offset is computed once, here: the face's flat frame
    // (a pure rotation bringing its normal to +Z), the region's outline and its holes. Dragging a
    // slider then only re-runs the (cheap) prism build, which is what keeps the live preview smooth
    // on a dense mesh - computing normals + neighbours + region + outline per frame would not be.
    struct Pick
    {
        bool               valid{ false };
        const ModelVolume* mv{ nullptr };
        int                facet{ -1 };
        Transform3d        to_flat{ Transform3d::Identity() };  // object space -> face-up frame
        ExPolygons         outline;   // the whole coplanar face
        ExPolygons         holes;     // only its holes, each one filled
        double             face_z{ 0. };
    };
    Pick m_pick;
    // The same derivation, into a caller-owned Pick: the hover highlight needs the geometry too, and
    // must not disturb what the user has already picked.
    bool make_pick(const Hover& hover, Pick* out) const;
    bool build_pick(const Hover& hover);
    void invalidate_pick();
    // The prism for the current pick + parameters, in object space. Empty with `why` set when the
    // pick has no usable outline or the offset eats it.
    indexed_triangle_set picked_prism(std::string* why = nullptr) const;
    // Why the current pick cannot be extruded, recomputed with the preview (not per frame: the panel
    // reads it, and building the prism twice a frame on a dense mesh is exactly what we avoid).
    std::string m_pick_why;
    // The flat slab that renders the outline as a highlighted surface, in object space.
    indexed_triangle_set face_slab(const ExPolygons& outline, const Transform3d& to_flat, double face_z) const;

    // [ORCAPORT:HF-3] Ghost: dim the object while a face is picked, so the highlight reads. Same
    // discipline as Support Zones: ALWAYS restore first, or a second call saves an already-tinted
    // colour as "the original" and the scene fades a little more on every refresh.
    void apply_ghost();
    void restore_ghost();
    std::map<std::tuple<int, int, int>, ColorRGBA> m_saved_colors;

    // Facet normals and neighbours, cached per volume: they are O(mesh) and the hover needs them on
    // every facet change. Invalidated when the volume changes or its mesh size changes.
    const std::vector<Vec3f>*   cached_normals(const ModelVolume* mv) const;
    const std::vector<Vec3i32>* cached_neighbors(const ModelVolume* mv) const;
    mutable const ModelVolume*  m_cache_mv{ nullptr };
    mutable size_t              m_cache_verts{ 0 };
    mutable size_t              m_cache_tris{ 0 };
    mutable std::vector<Vec3f>   m_cache_normals;
    mutable std::vector<Vec3i32> m_cache_neighbors;

    // The plug for the hovered face, or for a whole brush stroke, in object space; empty when there
    // is nothing to fill.
    indexed_triangle_set fill_mesh(const Hover& hover) const;
    indexed_triangle_set fill_mesh(const std::vector<Hover>& seeds) const;
    void                 record_stroke_sample();
    void                 commit_stroke();
    // Volume index of an applied plug that already covers `hit_world`, or -1.
    int          find_covering_fill(const Vec3d& hit_world) const;
    void         rebuild_preview();
    void         add_named_fill(const std::string& name, const indexed_triangle_set& its, const std::string& action);
    int          next_feature_id() const;

    double m_radius{ 2.5 }; // brush reach in world mm, converted to mesh units per volume
    std::map<std::string, wxString> m_desc;

    // [ORCAPORT:HF-3] Mode and the extrude-face parameters. Height and offset are in mm.
    Mode   m_mode{ Mode::ExtrudeFace };
    double m_extrude_height{ 5.0 };
    double m_extrude_offset{ 0.0 };
    bool   m_extrude_holes{ false }; // extrude only the holes of the face, each one filled
    bool   m_extrude_both{ false };  // half out of the face, half into the model

    Hover        m_hover;
    int          m_hover_applied{ -1 };
    bool         m_painting{ false };
    std::vector<Hover> m_stroke;

    PickingModel m_preview_hover;    // [ORCAPORT:HF-3] the face under the cursor (teal)
    PickingModel m_preview_pick;     // the picked face (green)
    PickingModel m_preview_prism;    // the extrusion for the pick + parameters (orange)
    PickingModel m_preview_applied;  // the fills already committed (red)
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

#endif // slic3r_GLGizmoExtrude_hpp_
