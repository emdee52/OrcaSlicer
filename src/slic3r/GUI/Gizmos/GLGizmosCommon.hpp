#ifndef slic3r_GUI_GLGizmosCommon_hpp_
#define slic3r_GUI_GLGizmosCommon_hpp_

#include <functional>
#include <memory>
#include <map>
#include <vector>

#include "slic3r/GUI/3DScene.hpp"
#include "slic3r/GUI/MeshUtils.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/CutUtils.hpp" // FaceSnapPoint

namespace Slic3r {

class ModelObject;
class ModelInstance;
class SLAPrintObject;
class ModelVolume;

namespace GUI {

class GLCanvas3D;
class Selection;
class ClippingPlane;
class Camera;

enum class SLAGizmoEventType : unsigned char {
    LeftDown = 1,
    LeftUp,
    RightDown,
    RightUp,
    Dragging,
    Delete,
    SelectAll,
    CtrlDown,
    CtrlUp,
    ShiftDown,
    ShiftUp,
    AltUp,
    Escape,
    ApplyChanges,
    DiscardChanges,
    AutomaticGeneration,
    ManualEditing,
    MouseWheelUp,
    MouseWheelDown,
    ResetClippingPlane,
    Moving
};



class CommonGizmosDataBase;
class AssembleViewDataBase;
namespace CommonGizmosDataObjects {
    class SelectionInfo;
    class InstancesHider;
    class HollowedMesh;
    class Raycaster;
    class ObjectClipper;
    class SupportsClipper;
}

namespace AssembleViewDataObjects {
    class ModelObjectsInfo;
    class ModelObjectsClipper;
}

// Some of the gizmos use the same data that need to be updated ocassionally.
// It is also desirable that the data are not recalculated when the gizmos
// are just switched, but on the other hand, they should be released when
// they are not in use by any gizmo anymore.

// Enumeration of various data types that the data pool can contain.
// Each gizmo can tell which of the data it wants to use through
// on_get_requirements() method.
enum class CommonGizmosDataID {
    None                 = 0,
    SelectionInfo        = 1 << 0,
    InstancesHider       = 1 << 1,
    Raycaster            = 1 << 3,
    ObjectClipper        = 1 << 4,

};


// Following class holds pointers to the common data objects and triggers
// their updating/releasing. There is just one object of this type (managed
// by GLGizmoManager, the gizmos keep a pointer to it.
class CommonGizmosDataPool {
public:
    explicit CommonGizmosDataPool(GLCanvas3D* canvas);

    // Update all resources and release what is not used.
    // Accepts a bitmask of currently required resources.
    void update(CommonGizmosDataID required);

    // Getters for the data that need to be accessed from the gizmos directly.
    CommonGizmosDataObjects::SelectionInfo* selection_info() const;
    CommonGizmosDataObjects::InstancesHider* instances_hider() const;
//    CommonGizmosDataObjects::HollowedMesh* hollowed_mesh() const;
    CommonGizmosDataObjects::Raycaster *  raycaster_ptr();
    CommonGizmosDataObjects::Raycaster* raycaster() const;
    CommonGizmosDataObjects::ObjectClipper* object_clipper() const;
    // CommonGizmosDataObjects::SupportsClipper* supports_clipper() const;


    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<CommonGizmosDataID, std::unique_ptr<CommonGizmosDataBase>> m_data;
    GLCanvas3D* m_canvas;

#ifndef NDEBUG
    bool check_dependencies(CommonGizmosDataID required) const;
#endif
};





// Base class for a wrapper object managing a single resource.
// Each of the enum values above (safe None) will have an object of this kind.
class CommonGizmosDataBase {
public:
    // Pass a backpointer to the pool, so the individual
    // objects can communicate with one another.
    explicit CommonGizmosDataBase(CommonGizmosDataPool* cgdp)
        : m_common{cgdp} {}
    virtual ~CommonGizmosDataBase() {}

    // Update the resource.
    void update() { on_update(); m_is_valid = true; }

    // Release any data that are stored internally.
    void release() { on_release(); m_is_valid = false; }

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    // Return a bitmask of all resources that this one relies on.
    // The dependent resource must have higher ID than the one
    // it depends on.
    virtual CommonGizmosDataID get_dependencies() const { return CommonGizmosDataID::None; }
#endif // NDEBUG

protected:
    virtual void on_release() = 0;
    virtual void on_update() = 0;
    CommonGizmosDataPool* get_pool() const { return m_common; }

private:
    bool m_is_valid = false;
    CommonGizmosDataPool* m_common = nullptr;
};



// The specializations of the CommonGizmosDataBase class live in this
// namespace to avoid clashes in GUI namespace.
namespace CommonGizmosDataObjects
{

class SelectionInfo : public CommonGizmosDataBase
{
public:
    explicit SelectionInfo(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}

    ModelObject* model_object() const { return m_model_object; }
    int get_active_instance() const;
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObject* m_model_object = nullptr;
    // int m_active_inst = -1;
    float m_z_shift = 0.f;
};



class InstancesHider : public CommonGizmosDataBase
{
public:
    explicit InstancesHider(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    void render_cut() const;

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
};



class Raycaster : public CommonGizmosDataBase
{
public:
    explicit Raycaster(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG

    const MeshRaycaster* raycaster() const { assert(m_raycasters.size() == 1); return m_raycasters.front().get(); }
    std::vector<const MeshRaycaster*> raycasters() const;
    void  set_only_support_model_part_flag(bool);

protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<std::unique_ptr<MeshRaycaster>> m_raycasters;
    std::vector<const TriangleMesh*> m_old_meshes;
    bool  m_only_support_model_part{true};
};



class ObjectClipper : public CommonGizmosDataBase
{
public:
    explicit ObjectClipper(CommonGizmosDataPool* cgdp)
        : CommonGizmosDataBase(cgdp) {}
#ifndef NDEBUG
    CommonGizmosDataID get_dependencies() const override { return CommonGizmosDataID::SelectionInfo; }
#endif // NDEBUG
    double get_position() const { return m_clp_ratio; }
    void set_position_to_init_layer();
    const ClippingPlane* get_clipping_plane(bool ignore_hide_clipped = false) const;
    void render_cut(const std::vector<size_t>* ignore_idxs = nullptr) const;
    void set_position_by_ratio(double pos, bool keep_normal, bool vertical_normal=false);
    void set_range_and_pos(const Vec3d& cpl_normal, double cpl_offset, double pos);
    void set_behavior(bool hide_clipped, bool fill_cut, double contour_width);

    // Restricts clipping and contour queries to a subset of the object's model-volume indices
    // (indices into selection_info()->model_object()->volumes). Empty = all volumes. Used by the
    // Cut tool to preview only the part that is being cut (CUT-2).
    void set_volume_filter(std::vector<int> idxs) { m_volume_idxs = std::move(idxs); }
    
    int get_number_of_contours() const;
    std::vector<Vec3d> point_per_contour() const;

    int is_projection_inside_cut(const Vec3d& point_in) const;
    bool has_valid_contour() const;


protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::pair<std::unique_ptr<MeshClipper>, Geometry::Transformation>> m_clippers;
    std::unique_ptr<ClippingPlane> m_clp;
    double m_clp_ratio = 0.;
    double m_active_inst_bb_radius = 0.;
    bool m_hide_clipped = true;
    // Empty = clip every volume. m_clippers is always built for all volumes (index aligned with
    // the model object's volumes), so this filter is applied when reading them, not when building.
    std::vector<int> m_volume_idxs;

    bool is_volume_included(size_t volume_idx) const {
        if (m_volume_idxs.empty())
            return true;
        for (int i : m_volume_idxs)
            if (i == int(volume_idx))
                return true;
        return false;
    }
};

} // namespace CommonGizmosDataObjects


// Per-facet normals and edge-neighbours of one ModelVolume, rebuilt only when the volume changes.
// Shared by the Cut and Holes gizmos' face-picking code.
struct FaceRegionCache
{
    const ModelVolume*   mv{ nullptr };
    std::vector<Vec3f>   normals;
    std::vector<Vec3i32> neighbors;
};

// Facets coplanar with `facet` (component-wise normal equality, the same rule as the measure tool's
// plane grouping), reached by edge adjacency. Fills `cache` if needed.
std::vector<int> coplanar_region(const ModelVolume* mv, size_t facet, FaceRegionCache& cache);

// Whether `facet` is one of the facets of `region`.
bool region_has_facet(const std::vector<int>& region, int facet);

// Triangle patch covering `region`, each facet lifted `lift` along its own normal to avoid z-fighting.
indexed_triangle_set build_coplanar_patch(const ModelVolume* mv, const std::vector<int>& region,
                                          const FaceRegionCache& cache, float lift);

// Raycasts the selected volumes of `mo` under `mouse_position` and returns the closest unclipped hit.
// Returns false when nothing is hit. When `accept` is set, only volumes it accepts are considered,
// so a caller can pick through parts of the object it must not hit.
bool raycast_object_face(const Vec2d& mouse_position, const Selection& selection, const ModelObject* mo,
                         const ClippingPlane* clipping, const GLVolume*& volume, const ModelVolume*& mv,
                         size_t& facet, Vec3d& hit_world,
                         const std::function<bool(const ModelVolume*)>& accept = {});


// Snap coordinates of the coplanar `region` of `mv`: corners, edge midpoints, edge quarters, face
// quarters and the face centre.
// The geometry lives in libslic3r/CutUtils; this is a thin wrapper over the volume's mesh.
std::vector<FaceSnapPoint> build_face_snap_points(const ModelVolume* mv, const std::vector<int>& region);

// Device-pixel projection of a world point; non-finite when it is behind the camera.
Vec2d world_to_screen(const Camera& camera, const Vec3d& world);


enum class AssembleViewDataID {
    None = 0,
    ModelObjectsInfo = 1 << 0,
    ModelObjectsClipper = 1 << 4,
};

class AssembleViewDataPool {
public:
    AssembleViewDataPool(GLCanvas3D* canvas);

    // Update all resources and release what is not used.
    // Accepts a bitmask of currently required resources.
    void update(AssembleViewDataID required);

    // Getters for the data that need to be accessed from the gizmos directly.
    AssembleViewDataObjects::ModelObjectsInfo* model_objects_info() const;
    AssembleViewDataObjects::ModelObjectsClipper* model_objects_clipper() const;

    GLCanvas3D* get_canvas() const { return m_canvas; }

private:
    std::map<AssembleViewDataID, std::unique_ptr<AssembleViewDataBase>> m_data;
    GLCanvas3D* m_canvas;

#ifndef NDEBUG
    bool check_dependencies(AssembleViewDataID required) const;
#endif
};

// Base class for a wrapper object managing a single resource.
// Each of the enum values above (safe None) will have an object of this kind.
class AssembleViewDataBase {
public:
    // Pass a backpointer to the pool, so the individual
    // objects can communicate with one another.
    explicit AssembleViewDataBase(AssembleViewDataPool* cgdp)
        : m_common{ cgdp } {}
    virtual ~AssembleViewDataBase() {}

    // Update the resource.
    void update() { on_update(); m_is_valid = true; }

    // Release any data that are stored internally.
    void release() { on_release(); m_is_valid = false; }

    // Returns whether the resource is currently maintained.
    bool is_valid() const { return m_is_valid; }

#ifndef NDEBUG
    // Return a bitmask of all resources that this one relies on.
    // The dependent resource must have higher ID than the one
    // it depends on.
    virtual AssembleViewDataID get_dependencies() const { return AssembleViewDataID::None; }
#endif // NDEBUG

protected:
    virtual void on_release() = 0;
    virtual void on_update() = 0;
    AssembleViewDataPool* get_pool() const { return m_common; }


private:
    bool m_is_valid = false;
    AssembleViewDataPool* m_common = nullptr;
};

namespace AssembleViewDataObjects
{
class ModelObjectsInfo : public AssembleViewDataBase
{
public:
    explicit ModelObjectsInfo(AssembleViewDataPool* cgdp)
        : AssembleViewDataBase(cgdp) {}

    ModelObjectPtrs model_objects() const { return m_model_objects; }
    //int get_active_instance() const;
    float get_sla_shift() const { return m_z_shift; }

protected:
    void on_update() override;
    void on_release() override;

private:
    ModelObjectPtrs m_model_objects;
    float m_z_shift = 0.f;
};

class ModelObjectsClipper : public AssembleViewDataBase
{
public:
    explicit ModelObjectsClipper(AssembleViewDataPool* cgdp)
        : AssembleViewDataBase(cgdp) {}
#ifndef NDEBUG
    AssembleViewDataID get_dependencies() const override { return AssembleViewDataID::ModelObjectsInfo; }
#endif // NDEBUG

    void set_position(double pos, bool keep_normal);
    double get_position() const { return m_clp_ratio; }
    ClippingPlane* get_clipping_plane() const { return m_clp.get(); }
    void render_cut() const;


protected:
    void on_update() override;
    void on_release() override;

private:
    std::vector<const TriangleMesh*> m_old_meshes;
    std::vector<std::unique_ptr<MeshClipper>> m_clippers;
    std::unique_ptr<ClippingPlane> m_clp;
    double m_clp_ratio = 0.;
    double m_active_inst_bb_radius = 0.;
};
}

} // namespace GUI
} // namespace Slic3r


#endif // slic3r_GUI_GLGizmosCommon_hpp_
