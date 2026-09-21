#ifndef slic3r_CutUtils_hpp_
#define slic3r_CutUtils_hpp_

#include "enum_bitmask.hpp"
#include "Point.hpp"
#include "Model.hpp"

#include <functional>
#include <vector>

namespace Slic3r {

using ModelObjectPtrs = std::vector<ModelObject*>;

enum class ModelObjectCutAttribute : int { KeepUpper, KeepLower, KeepAsParts, FlipUpper, FlipLower, PlaceOnCutUpper, PlaceOnCutLower, CreateDowels, InvalidateCutInfo, KeepPaint };
using ModelObjectCutAttributes = enum_bitmask<ModelObjectCutAttribute>;
ENABLE_ENUM_BITMASK_OPERATORS(ModelObjectCutAttribute);

// Outward normal of facet `facet_idx` of `its` in world space, given the object-to-world
// transform `trafo`. The normal is transformed with the inverse-transpose of the linear part,
// so it stays correct under non-uniform scaling and mirrored (left-handed) transforms.
// Returns UnitZ if the facet index is out of range.
Vec3d facet_normal_in_world(const indexed_triangle_set& its, int facet_idx, const Transform3d& trafo);

// Coordinates on a picked flat face that the cursor can snap to. Ordered by pick priority: an
// earlier kind wins a tie at the same screen distance.
enum class FaceSnapKind : unsigned char { None = 0, Corner, EdgeMid, FaceCenter };

struct FaceSnapPoint
{
    FaceSnapKind kind{ FaceSnapKind::None };
    Vec3d        pos{ Vec3d::Zero() }; // mesh space of the mesh the region came from
};

// Deterministic right-handed in-plane basis of `normal`; repeated picks of one face give the same
// axes.
void face_plane_axes(const Vec3d& normal, Vec3d& x_axis, Vec3d& y_axis);

// Corners and edge midpoints of the outer boundary loop of `region`, plus the loop's area centroid.
// The region must be planar enough to trust (see the check in the implementation); returns an empty
// vector otherwise, so the caller can fall back to the raw raycast hit.
std::vector<FaceSnapPoint> face_snap_points(const indexed_triangle_set& its, const std::vector<int>& region);

// Nearest candidate to `screen_pos` within `max_px`, measured through `project` (mesh space to
// device pixels). Candidates further away or projecting behind the camera are ignored.
bool nearest_face_snap(const std::vector<FaceSnapPoint>& pts, const std::function<Vec2d(const Vec3d&)>& project,
                       const Vec2d& screen_pos, double max_px, FaceSnapPoint& out);

// Built-in profiles for the shaped ("cookie cutter") cut.
enum class CutShapeKind : int { Circle, Square, Hexagon };

// Closed prism used as the cutting solid for Cut::perform_with_shape, expressed in cut space:
// centered on the origin (the cut plane) and extruded along Z (the cut normal) from
// -half_height to +half_height. `size` is the diameter (Circle), the side (Square) or the
// across-flats distance (Hexagon).
indexed_triangle_set make_cookie_cutter(CutShapeKind kind, double size, double half_height);

// Same prism, but spanning an explicit range [z_min, z_max] along the cut normal. Used to cut a
// blind pocket (material removed on one side of the plane only) instead of cutting all the way
// through.
indexed_triangle_set make_cookie_cutter(CutShapeKind kind, double size, double z_min, double z_max);


class Cut {

    Model                       m_model;
    int                         m_instance;
    const Transform3d           m_cut_matrix;
    ModelObjectCutAttributes    m_attributes;
    // Indices into the source object's volumes to cut. Empty = cut every model part (default).
    // When non-empty, only these parts are cut; the others are carried into the single result
    // object unchanged. Requires KeepAsParts.
    std::vector<int>            m_cut_volume_idxs;

    void post_process(ModelObject* object, ModelObjectPtrs& objects, bool keep, bool place_on_cut, bool flip);
    void post_process(ModelObject* upper_object, ModelObject* lower_object, ModelObjectPtrs& objects);
    void finalize(const ModelObjectPtrs& objects, const std::vector<std::optional<TriangleSelector::SavedPainting>>& saved_paintings);

    // Shared driver for every solid-splitting cut mode. `split_solid_volume` fills the keep-upper
    // (inside) and keep-lower (outside) objects for one model part; `ok` is set to false when the
    // split could not be performed, which aborts the whole cut and leaves the model untouched.
    const ModelObjectPtrs& perform_split(
        const std::function<void(const ModelVolume*, const Transform3d& instance_matrix, ModelObject* upper, ModelObject* lower, bool& ok)>& split_solid_volume);

public:

    Cut(const ModelObject* object, int instance, const Transform3d& cut_matrix, 
        ModelObjectCutAttributes attributes = ModelObjectCutAttribute::KeepUpper |
                                              ModelObjectCutAttribute::KeepLower |
                                              ModelObjectCutAttribute::KeepAsParts,
        const std::vector<int>& cut_volume_idxs = {});
    ~Cut() { m_model.clear_objects(); }

    struct Groove
    {
        float depth{ 0.f };
        float width{ 0.f };
        float flaps_angle{ 0.f };
        float angle{ 0.f };
        float depth_init{ 0.f };
        float width_init{ 0.f };
        float flaps_angle_init{ 0.f };
        float angle_init{ 0.f };
        float depth_tolerance{ 0.1f };
        float width_tolerance{ 0.1f };
    };

    struct Part
    {
        bool selected;
        bool is_modifier;
    };

    const ModelObjectPtrs& perform_with_plane();
    // Shaped ("cookie cutter") split: `cutter` is a closed solid in cut space (see
    // make_cookie_cutter). The keep-upper piece is the object inside the cutter, the keep-lower
    // piece is the object outside it. Returns an empty list when the boolean fails.
    const ModelObjectPtrs& perform_with_shape(const TriangleMesh& cutter);
    const ModelObjectPtrs& perform_by_contour(const ModelObject* src_object, std::vector<Part> parts, int dowels_count);
    const ModelObjectPtrs& perform_with_groove(const Groove&      groove,
                                               const Transform3d& rotation_m,
                                               const int          groove_count,
                                               const float        groove_gap,
                                               const float        m_radius,
                                               bool               keep_as_parts = false);

    static float calculate_groove_width(const Cut::Groove& groove, const float m_radius);
    }; // namespace Cut

} // namespace Slic3r

#endif /* slic3r_CutUtils_hpp_ */
