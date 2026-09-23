
#include "CutUtils.hpp"
#include "Geometry.hpp"
#include "libslic3r.h"
#include "MeshBoolean.hpp"
#include "Model.hpp"
#include "TriangleMesh.hpp"
#include "TriangleMeshSlicer.hpp"
#include "TriangleSelector.hpp"
#include "ObjectID.hpp"

#include <boost/log/trivial.hpp>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <utility>

namespace Slic3r {

using namespace Geometry;

Vec3d facet_normal_in_world(const indexed_triangle_set& its, int facet_idx, const Transform3d& trafo)
{
    if (facet_idx < 0 || facet_idx >= int(its.indices.size()))
        return Vec3d::UnitZ();

    const Vec3d    local         = its_face_normal(its, facet_idx).cast<double>();
    const Matrix3d normal_matrix = trafo.linear().inverse().transpose();
    Vec3d          world         = normal_matrix * local;
    return world.norm() > 1e-12 ? world.normalized() : Vec3d::UnitZ();
}

void face_plane_axes(const Vec3d& normal, Vec3d& x_axis, Vec3d& y_axis)
{
    const Vec3d  z     = normal.normalized();
    const double z_dot = z.dot(Vec3d::UnitZ());
    if (std::abs(z_dot) > 0.9) {
        x_axis = Vec3d::UnitX();
        y_axis = z.cross(x_axis).normalized();
        if (z_dot < 0) y_axis = -y_axis;
        x_axis = y_axis.cross(z).normalized();
    } else {
        x_axis = Vec3d::UnitZ().cross(z).normalized();
        y_axis = z.cross(x_axis).normalized();
        if (y_axis.z() < 0) { y_axis = -y_axis; x_axis = -x_axis; }
    }
}

indexed_triangle_set fill_from_above_mesh(const indexed_triangle_set &its, const Vec3d &plane_point,
                                          const Vec3d &plane_normal, double offset)
{
    if (its.indices.empty() || !plane_point.allFinite() || !plane_normal.allFinite() || offset <= 0.)
        return {};

    const Vec3d n = plane_normal.norm() > 1e-12 ? plane_normal.normalized() : Vec3d::UnitZ();

    Vec3d x_axis, y_axis;
    face_plane_axes(n, x_axis, y_axis);

    // Frame with z on the plane normal and the origin on the plane.
    Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign> world_to_local = Eigen::Transform<double, 3, Eigen::Affine, Eigen::DontAlign>::Identity();
    world_to_local.linear().col(0) = x_axis;
    world_to_local.linear().col(1) = y_axis;
    world_to_local.linear().col(2) = n;
    world_to_local.translation()    = -world_to_local.linear() * plane_point;

    indexed_triangle_set local = its;
    its_transform(local, world_to_local);

    indexed_triangle_set upper;
    cut_mesh(local, 0.f, &upper, nullptr);
    if (upper.indices.empty())
        return {};

    indexed_triangle_set band;
    cut_mesh(upper, float(offset), nullptr, &band);
    if (band.indices.empty())
        return {};

    const auto local_to_world = world_to_local.inverse();
    its_transform(band, local_to_world);
    its_translate(band, (-n * offset).cast<float>());
    return band;
}

std::vector<FaceSnapPoint> face_snap_points(const indexed_triangle_set& its, const std::vector<int>& region)
{
    std::vector<FaceSnapPoint> out;
    if (region.empty())
        return out;

    const int n_facets = int(its.indices.size());
    const int n_verts  = int(its.vertices.size());
    if (n_facets == 0 || n_verts == 0)
        return out;

    // A region edge used by one region facet only lies on the region's boundary; the loop it forms
    // is walked in the direction the facets store it.
    const auto ekey = [](int a, int b) {
        if (a > b) std::swap(a, b);
        return (uint64_t(uint32_t(a)) << 32) | uint32_t(b);
    };
    std::unordered_map<uint64_t, int> edge_count;
    std::unordered_multimap<int, int> dir_edges;
    for (int f : region) {
        if (f < 0 || f >= n_facets)
            continue;
        const Vec3i32& t = its.indices[f];
        for (int e = 0; e < 3; ++e)
            ++edge_count[ekey(t[e], t[(e + 1) % 3])];
    }
    for (int f : region) {
        if (f < 0 || f >= n_facets)
            continue;
        const Vec3i32& t = its.indices[f];
        for (int e = 0; e < 3; ++e) {
            const int v0 = t[e], v1 = t[(e + 1) % 3];
            if (edge_count[ekey(v0, v1)] == 1)
                dir_edges.emplace(v0, v1);
        }
    }
    if (dir_edges.empty())
        return out; // closed patch: nothing to snap to

    std::vector<std::vector<int>> loops;
    while (!dir_edges.empty()) {
        const int start = dir_edges.begin()->first;
        std::vector<int> loop;
        int              cur = start;
        while (true) {
            auto it = dir_edges.find(cur);
            if (it == dir_edges.end())
                break;
            const int nxt = it->second;
            dir_edges.erase(it);
            loop.push_back(cur);
            if (nxt == start)
                break;
            cur = nxt;
            if (int(loop.size()) > n_verts)
                break;
        }
        if (loop.size() >= 3)
            loops.push_back(std::move(loop));
    }
    if (loops.empty())
        return out;

    const int seed = region.front();
    if (seed < 0 || seed >= n_facets)
        return out;
    const Vec3i32& st = its.indices[seed];
    Vec3d          n  = (its.vertices[st[1]] - its.vertices[st[0]]).cast<double>()
                            .cross((its.vertices[st[2]] - its.vertices[st[0]]).cast<double>());
    const double nl = n.norm();
    if (nl < 1e-12)
        return out;
    n /= nl;
    Vec3d px, py;
    face_plane_axes(n, px, py);

    const auto vertex    = [&its](int vi) { return its.vertices[vi].cast<double>(); };
    const auto loop_area = [&](const std::vector<int>& loop) {
        const Vec3d o = vertex(loop[0]);
        double      a = 0.;
        for (size_t j = 0; j < loop.size(); ++j) {
            const Vec3d p0 = vertex(loop[j]) - o;
            const Vec3d p1 = vertex(loop[(j + 1) % loop.size()]) - o;
            a += p0.dot(px) * p1.dot(py) - p1.dot(px) * p0.dot(py);
        }
        return a * 0.5;
    };

    size_t outer      = 0;
    double outer_area = 0.;
    for (size_t i = 0; i < loops.size(); ++i) {
        const double a = std::abs(loop_area(loops[i]));
        if (a > outer_area) {
            outer_area = a;
            outer      = i;
        }
    }
    if (outer_area < 1e-9)
        return out;
    const std::vector<int>& loop = loops[outer];

    // The facets are coplanar by normal, but a staircase of parallel facets can still carry the loop
    // off the plane. Placing holes on such a loop would be wrong, so reject it and let the caller
    // fall back to the raw hit.
    const Vec3d origin = vertex(loop[0]);
    double      diag   = 0.;
    for (int vi : loop)
        diag = std::max(diag, (vertex(vi) - origin).norm());
    const double plane_tol = std::max(1e-3, 0.005 * diag);
    for (int vi : loop)
        if (std::abs((vertex(vi) - origin).dot(n)) > plane_tol)
            return out;

    const double signed_area = loop_area(loop);
    Vec3d        centroid    = origin;
    if (std::abs(signed_area) > 1e-12) {
        double cx = 0., cy = 0.;
        for (size_t j = 0; j < loop.size(); ++j) {
            const Vec3d  p0 = vertex(loop[j]) - origin;
            const Vec3d  p1 = vertex(loop[(j + 1) % loop.size()]) - origin;
            const double cr = p0.dot(px) * p1.dot(py) - p1.dot(px) * p0.dot(py);
            cx += (p0.dot(px) + p1.dot(px)) * cr;
            cy += (p0.dot(py) + p1.dot(py)) * cr;
        }
        // ponytail: area centroid, not the vertex average, so the point stays central on a face whose
        // triangles are unevenly sized. On a non-convex loop it can fall outside the material, which
        // only matters if a hole is placed there.
        centroid = origin + px * (cx / (6. * signed_area)) + py * (cy / (6. * signed_area));
    }

    out.reserve(loop.size() * 5 + 1);
    for (int vi : loop)
        out.push_back({ FaceSnapKind::Corner, vertex(vi) });
    for (size_t j = 0; j < loop.size(); ++j)
        out.push_back({ FaceSnapKind::EdgeMid, 0.5 * (vertex(loop[j]) + vertex(loop[(j + 1) % loop.size()])) });
    out.push_back({ FaceSnapKind::FaceCenter, centroid });
    for (size_t j = 0; j < loop.size(); ++j) {
        const Vec3d a = vertex(loop[j]), b = vertex(loop[(j + 1) % loop.size()]);
        out.push_back({ FaceSnapKind::EdgeQuarter, (0.75 * a + 0.25 * b) });
        out.push_back({ FaceSnapKind::EdgeQuarter, (0.25 * a + 0.75 * b) });
    }
    for (int vi : loop)
        out.push_back({ FaceSnapKind::FaceQuarter, 0.5 * (centroid + vertex(vi)) });
    return out;
}

std::vector<FaceSnapPoint> snap_points_distinct_cuts(const std::vector<FaceSnapPoint>& pts, const Transform3d& to_world,
                                                     const Vec3d& plane_normal, const Vec3d& plane_point, double tol)
{
    std::vector<FaceSnapPoint> out;
    std::vector<double>        offsets; // one per kept point, same order
    out.reserve(pts.size());
    for (const FaceSnapPoint& p : pts) {
        const double d = plane_normal.dot(to_world * p.pos - plane_point);
        bool         same_cut = false;
        for (const double kept : offsets)
            if (std::abs(kept - d) <= tol) {
                same_cut = true;
                break;
            }
        if (same_cut)
            continue;
        out.push_back(p);
        offsets.push_back(d);
    }
    return out;
}

bool nearest_face_snap(const std::vector<FaceSnapPoint>& pts, const std::function<Vec2d(const Vec3d&)>& project,
                       const Vec2d& screen_pos, FaceSnapPoint& out, double min_px, double max_px,
                       double* tolerance_px)
{
    // Screen spacing scales with the face's on-screen size and with the zoom, so half the gap to the
    // next candidate is the largest stick distance that can never be ambiguous: the stick region
    // ends where the next candidate's begins.
    std::vector<Vec2d> screen;
    screen.reserve(pts.size());
    for (const FaceSnapPoint& p : pts)
        screen.push_back(project(p.pos));

    int    hit      = -1;
    double hit_dist = 0.0;
    for (size_t i = 0; i < screen.size(); ++i) {
        if (!screen[i].allFinite())
            continue;
        const double d = (screen[i] - screen_pos).squaredNorm();
        // Strictly closer only: candidates are ordered by priority, so an earlier kind keeps a tie.
        if (hit < 0 || d < hit_dist) {
            hit      = int(i);
            hit_dist = d;
        }
    }
    if (hit < 0)
        return false;

    double second_dist = -1.0;
    for (size_t i = 0; i < screen.size(); ++i) {
        if (int(i) == hit || !screen[i].allFinite())
            continue;
        const double d = (screen[i] - screen_pos).squaredNorm();
        if (second_dist < 0.0 || d < second_dist)
            second_dist = d;
    }

    // A lone candidate has no spacing to scale from, so it gets the full stick distance.
    const double tolerance = second_dist < 0.0 ? max_px : std::clamp(0.5 * std::sqrt(second_dist), min_px, max_px);
    if (hit_dist > tolerance * tolerance)
        return false;

    out = pts[size_t(hit)];
    if (tolerance_px != nullptr)
        *tolerance_px = tolerance;
    return true;
}

indexed_triangle_set make_cookie_cutter(CutShapeKind kind, double size, double z_min, double z_max)
{
    const double h = z_max - z_min;

    indexed_triangle_set its;
    Vec3f                offset = Vec3f::Zero();
    switch (kind) {
    case CutShapeKind::Square:
        its    = its_make_cube(size, size, h);
        offset = Vec3f(float(-0.5 * size), float(-0.5 * size), float(z_min));
        break;
    case CutShapeKind::Hexagon:
        // its_make_cylinder with 6 segments is a regular hexagon perpendicular to its circumradius.
        its    = its_make_cylinder(size / std::sqrt(3.0), h, 2. * PI / 6.);
        offset = Vec3f(0.f, 0.f, float(z_min));
        break;
    case CutShapeKind::Circle:
    default:
        its    = its_make_cylinder(size * 0.5, h, 2. * PI / 64.);
        offset = Vec3f(0.f, 0.f, float(z_min));
        break;
    }

    // its_make_cube/its_make_cylinder are anchored at z = 0; move them onto the requested range.
    for (Vec3f& v : its.vertices)
        v += offset;

    return its;
}

indexed_triangle_set make_cookie_cutter(CutShapeKind kind, double size, double half_height)
{
    return make_cookie_cutter(kind, size, -half_height, half_height);
}

static void apply_tolerance(ModelVolume* vol)
{
    ModelVolume::CutInfo& cut_info = vol->cut_info;

    assert(cut_info.is_connector);
    if (!cut_info.is_processed)
        return;

    Vec3d sf = vol->get_scaling_factor();

    // make a "hole" wider
    sf[X] += double(cut_info.radius_tolerance);
    sf[Y] += double(cut_info.radius_tolerance);

    // make a "hole" dipper
    sf[Z] += double(cut_info.height_tolerance);

    vol->set_scaling_factor(sf);

    // correct offset in respect to the new depth
    Vec3d rot_norm = rotation_transform(vol->get_rotation()) * Vec3d::UnitZ();
    if (rot_norm.norm() != 0.0)
        rot_norm.normalize();

    double z_offset = 0.5 * static_cast<double>(cut_info.height_tolerance);
    if (cut_info.connector_type == CutConnectorType::Plug || 
        cut_info.connector_type == CutConnectorType::Snap)
        z_offset -= 0.05; // add small Z offset to better preview

    vol->set_offset(vol->get_offset() + rot_norm * z_offset);
}

static void add_cut_volume(TriangleMesh& mesh, ModelObject* object, const ModelVolume* src_volume, const Transform3d& cut_matrix, const std::string& suffix = {}, ModelVolumeType type = ModelVolumeType::MODEL_PART)
{
    if (mesh.empty())
        return;

    mesh.transform(cut_matrix);
    ModelVolume* vol = object->add_volume(mesh);
    vol->set_type(type);

    vol->name = src_volume->name + suffix;
    // Don't copy the config's ID.
    vol->config.assign_config(src_volume->config);
    assert(vol->config.id().valid());
    assert(vol->config.id() != src_volume->config.id());
    vol->set_material(src_volume->material_id(), *src_volume->material());
    vol->cut_info = src_volume->cut_info;
}

static void process_volume_cut( const ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                                ModelObjectCutAttributes attributes, TriangleMesh& upper_mesh, TriangleMesh& lower_mesh)
{
    const auto volume_matrix = volume->get_matrix();

    const Transformation cut_transformation = Transformation(cut_matrix);
    const Transform3d invert_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1 * cut_transformation.get_offset());

    // Transform the mesh by the combined transformation matrix.
    // Flip the triangles in case the composite transformation is left handed.
    TriangleMesh mesh(volume->mesh());
    mesh.transform(invert_cut_matrix * instance_matrix * volume_matrix, true);

    indexed_triangle_set upper_its, lower_its;
    cut_mesh(mesh.its, 0.0f, &upper_its, &lower_its);
    if (attributes.has(ModelObjectCutAttribute::KeepUpper))
        upper_mesh = TriangleMesh(upper_its);
    if (attributes.has(ModelObjectCutAttribute::KeepLower))
        lower_mesh = TriangleMesh(lower_its);
}

static void process_connector_cut(  ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                                    ModelObjectCutAttributes attributes, ModelObject* upper, ModelObject* lower,
                                    std::vector<ModelObject*>& dowels)
{
    assert(volume->cut_info.is_connector);
    volume->cut_info.set_processed();

    const auto volume_matrix = volume->get_matrix();

    // ! Don't apply instance transformation for the conntectors.
    // This transformation is already there
    if (volume->cut_info.connector_type != CutConnectorType::Dowel) {
        if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
            ModelVolume* vol = nullptr;
            if (volume->cut_info.connector_type == CutConnectorType::Snap) {
                TriangleMesh mesh = TriangleMesh(its_make_cylinder(1.0, 1.0, PI / 180.));

                vol = upper->add_volume(std::move(mesh));
                vol->set_transformation(volume->get_transformation());
                vol->set_type(ModelVolumeType::NEGATIVE_VOLUME);

                vol->cut_info = volume->cut_info;
                vol->name = volume->name;
            }
            else
                vol = upper->add_volume(*volume);

            vol->set_transformation(volume_matrix);
            apply_tolerance(vol);
        }
        if (attributes.has(ModelObjectCutAttribute::KeepLower)) {
            ModelVolume* vol = lower->add_volume(*volume);
            vol->set_transformation(volume_matrix);
            // for lower part change type of connector from NEGATIVE_VOLUME to MODEL_PART if this connector is a plug
            vol->set_type(ModelVolumeType::MODEL_PART);
        }
    }
    else {
        if (attributes.has(ModelObjectCutAttribute::CreateDowels)) {
            ModelObject* dowel{ nullptr };
            // Clone the object to duplicate instances, materials etc.
            volume->get_object()->clone_for_cut(&dowel);

            // add one more solid part same as connector if this connector is a dowel
            ModelVolume* vol = dowel->add_volume(*volume);
            vol->set_type(ModelVolumeType::MODEL_PART);

            // But discard rotation and Z-offset for this volume
            vol->set_rotation(Vec3d::Zero());
            vol->set_offset(Z, 0.0);

            dowels.push_back(dowel);
        }

        // Cut the dowel
        apply_tolerance(volume);

        // Perform cut
        TriangleMesh upper_mesh, lower_mesh;
        process_volume_cut(volume, Transform3d::Identity(), cut_matrix, attributes, upper_mesh, lower_mesh);

        // add small Z offset to better preview
        upper_mesh.translate((-0.05 * Vec3d::UnitZ()).cast<float>());
        lower_mesh.translate((0.05 * Vec3d::UnitZ()).cast<float>());

        // Add cut parts to the related objects
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A", volume->type());
        add_cut_volume(lower_mesh, lower, volume, cut_matrix, "_B", volume->type());
    }
}

static void process_modifier_cut(ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& inverse_cut_matrix,
                                 ModelObjectCutAttributes attributes, ModelObject* upper, ModelObject* lower)
{
    const auto volume_matrix = instance_matrix * volume->get_matrix();

    // Modifiers are not cut, but we still need to add the instance transformation
    // to the modifier volume transformation to preserve their shape properly.
    volume->set_transformation(Transformation(volume_matrix));

    if (attributes.has(ModelObjectCutAttribute::KeepAsParts)) {
        upper->add_volume(*volume);
        return;
    }

    // Some logic for the negative volumes/connectors. Add only needed modifiers
    auto bb = volume->mesh().transformed_bounding_box(inverse_cut_matrix * volume_matrix);
    bool is_crossed_by_cut = bb.min[Z] <= 0 && bb.max[Z] >= 0;
    if (attributes.has(ModelObjectCutAttribute::KeepUpper) && (bb.min[Z] >= 0 || is_crossed_by_cut))
        upper->add_volume(*volume);
    if (attributes.has(ModelObjectCutAttribute::KeepLower) && (bb.max[Z] <= 0 || is_crossed_by_cut))
        lower->add_volume(*volume);
}

static void process_solid_part_cut(const ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                            ModelObjectCutAttributes attributes, ModelObject* upper, ModelObject* lower)
{
    // Perform cut
    TriangleMesh upper_mesh, lower_mesh;
    process_volume_cut(volume, instance_matrix, cut_matrix, attributes, upper_mesh, lower_mesh);

    // Add required cut parts to the objects

    if (attributes.has(ModelObjectCutAttribute::KeepAsParts)) {
        add_cut_volume(upper_mesh, upper, volume, cut_matrix, "_A");
        if (!lower_mesh.empty()) {
            add_cut_volume(lower_mesh, upper, volume, cut_matrix, "_B");
            upper->volumes.back()->cut_info.is_from_upper = false;
        }
        return;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepUpper))
        add_cut_volume(upper_mesh, upper, volume, cut_matrix);

    if (attributes.has(ModelObjectCutAttribute::KeepLower) && !lower_mesh.empty())
        add_cut_volume(lower_mesh, lower, volume, cut_matrix);
}

static void process_shape_cut(const ModelVolume* volume, const Transform3d& instance_matrix, const Transform3d& cut_matrix,
                              const TriangleMesh& cutter, ModelObjectCutAttributes attributes,
                              ModelObject* upper, ModelObject* lower, bool& hit)
{
    const auto volume_matrix = volume->get_matrix();

    const Transformation cut_transformation = Transformation(cut_matrix);
    const Transform3d    invert_cut_matrix  = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1. * cut_transformation.get_offset());

    TriangleMesh mesh(volume->mesh());
    mesh.transform(invert_cut_matrix * instance_matrix * volume_matrix, true);

    // The keep-upper piece is inside the cutter, the keep-lower piece is the rest of the part.
    auto merge_boolean = [&mesh, &cutter](const char* op) {
        std::vector<TriangleMesh> parts;
        MeshBoolean::mcut::make_boolean(mesh, cutter, parts, op);
        TriangleMesh merged;
        for (const TriangleMesh& part : parts)
            merged.merge(part);
        return merged;
    };

    TriangleMesh inside = merge_boolean("INTERSECTION");
    if (inside.empty()) {
        // The shape does not touch this part: carry it over whole instead of failing the cut.
        if (attributes.has(ModelObjectCutAttribute::KeepAsParts)) {
            add_cut_volume(mesh, upper, volume, cut_matrix);
            upper->volumes.back()->cut_info.is_from_upper = false;
        } else if (attributes.has(ModelObjectCutAttribute::KeepLower)) {
            add_cut_volume(mesh, lower, volume, cut_matrix);
        }
        return;
    }

    // The shape intersects at least one part, so the cut as a whole is valid.
    hit = true;

    TriangleMesh outside = merge_boolean("A_NOT_B");
    if (outside.empty()) {
        // The part lies entirely inside the shape.
        if (attributes.has(ModelObjectCutAttribute::KeepAsParts)) {
            add_cut_volume(mesh, upper, volume, cut_matrix);
        } else if (attributes.has(ModelObjectCutAttribute::KeepUpper)) {
            add_cut_volume(mesh, upper, volume, cut_matrix);
        }
        return;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepAsParts)) {
        add_cut_volume(inside, upper, volume, cut_matrix, "_A");
        add_cut_volume(outside, upper, volume, cut_matrix, "_B");
        upper->volumes.back()->cut_info.is_from_upper = false;
        return;
    }

    if (attributes.has(ModelObjectCutAttribute::KeepUpper))
        add_cut_volume(inside, upper, volume, cut_matrix);

    if (attributes.has(ModelObjectCutAttribute::KeepLower))
        add_cut_volume(outside, lower, volume, cut_matrix);
}

// Carries a model part that is NOT being cut into the single result object, baked into cut space
// exactly like process_volume_cut and then re-added through the cut matrix, so it lands at its
// original world transform once the result object's instance transformation is reset.
static void process_untouched_volume(const ModelVolume* volume, const Transform3d& instance_matrix,
                                     const Transform3d& inverse_cut_matrix, const Transform3d& cut_matrix,
                                     ModelObject* object)
{
    TriangleMesh mesh(volume->mesh());
    mesh.transform(inverse_cut_matrix * instance_matrix * volume->get_matrix(), true);
    add_cut_volume(mesh, object, volume, cut_matrix);
}

static void reset_instance_transformation(ModelObject* object, size_t src_instance_idx, 
                                          const Transform3d& cut_matrix = Transform3d::Identity(),
                                          bool place_on_cut = false, bool flip = false)
{
    // Reset instance transformation except offset and Z-rotation

    for (size_t i = 0; i < object->instances.size(); ++i) {
        auto& obj_instance = object->instances[i];
        const double rot_z = obj_instance->get_rotation().z();
        
        Transformation inst_trafo = Transformation(obj_instance->get_transformation().get_matrix_no_scaling_factor());
        // add respect to mirroring
        if (obj_instance->is_left_handed())
            inst_trafo = inst_trafo * Transformation(scale_transform(Vec3d(-1, 1, 1)));

        obj_instance->set_transformation(inst_trafo);

        Vec3d rotation = Vec3d::Zero();
        if (!flip && !place_on_cut) {
            if ( i != src_instance_idx)
            rotation[Z] = rot_z;
        }
        else {
            Transform3d rotation_matrix = Transform3d::Identity();
            if (flip)
                rotation_matrix = rotation_transform(PI * Vec3d::UnitX());

            if (place_on_cut)
                rotation_matrix = rotation_matrix * Transformation(cut_matrix).get_rotation_matrix().inverse();

            if (i != src_instance_idx)
                rotation_matrix = rotation_transform(rot_z * Vec3d::UnitZ()) * rotation_matrix;

            rotation = Transformation(rotation_matrix).get_rotation();
        }

        obj_instance->set_rotation(rotation);
    }
}


Cut::Cut(const ModelObject* object, int instance, const Transform3d& cut_matrix,
         ModelObjectCutAttributes attributes/*= ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts*/,
         const std::vector<int>& cut_volume_idxs)
    : m_instance(instance), m_cut_matrix(cut_matrix), m_attributes(attributes), m_cut_volume_idxs(cut_volume_idxs)
{
    m_model = Model();
    if (object)
        m_model.add_object(*object);
}

void Cut::post_process(ModelObject* object, ModelObjectPtrs& cut_object_ptrs, bool keep, bool place_on_cut, bool flip)
{
    if (!object) return;

    if (keep && !object->volumes.empty()) {
        reset_instance_transformation(object, m_instance, m_cut_matrix, place_on_cut, flip);
        cut_object_ptrs.push_back(object);
    }
    else
        m_model.objects.push_back(object); // will be deleted in m_model.clear_objects();
}

void Cut::post_process(ModelObject* upper, ModelObject* lower, ModelObjectPtrs& cut_object_ptrs)
{
    post_process(upper, cut_object_ptrs,
        m_attributes.has(ModelObjectCutAttribute::KeepUpper),
        m_attributes.has(ModelObjectCutAttribute::PlaceOnCutUpper),
        m_attributes.has(ModelObjectCutAttribute::FlipUpper));

    post_process(lower, cut_object_ptrs,
        m_attributes.has(ModelObjectCutAttribute::KeepLower),
        m_attributes.has(ModelObjectCutAttribute::PlaceOnCutLower),
        m_attributes.has(ModelObjectCutAttribute::PlaceOnCutLower) || m_attributes.has(ModelObjectCutAttribute::FlipLower));
}


void Cut::finalize(const ModelObjectPtrs& objects, const std::vector<std::optional<TriangleSelector::SavedPainting>>& saved_paintings)
{
    // Paint volumes
    for (const auto& saved_painting : saved_paintings) {
        if (saved_painting) {
            for (const auto object : objects) {
                for (const auto volume : object->volumes) {
                    if (volume->is_model_part() && !volume->is_cut_connector()) {
                    volume->restore_painting(saved_painting, true);
                    }
                }
            }
        }
    }

    //clear model from temporary objects
    m_model.clear_objects();

    // add to model result objects
    m_model.objects = objects;
}


const ModelObjectPtrs& Cut::perform_split(
    const std::function<void(const ModelVolume*, const Transform3d& instance_matrix, ModelObject* upper, ModelObject* lower, bool& ok)>& split_solid_volume)
{
    if (!m_attributes.has(ModelObjectCutAttribute::KeepUpper) && !m_attributes.has(ModelObjectCutAttribute::KeepLower)) {
        m_model.clear_objects();
        return m_model.objects;
    }

    ModelObject* mo = m_model.objects.front();

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - start";

    // Clone the object to duplicate instances, materials etc.
    ModelObject* upper{ nullptr };
    if (m_attributes.has(ModelObjectCutAttribute::KeepUpper))
        mo->clone_for_cut(&upper);

    ModelObject* lower{ nullptr };
    if (m_attributes.has(ModelObjectCutAttribute::KeepLower) && !m_attributes.has(ModelObjectCutAttribute::KeepAsParts))
        mo->clone_for_cut(&lower);

    std::vector<ModelObject*> dowels;

    // Because transformations are going to be applied to meshes directly,
    // we reset transformation of all instances and volumes,
    // except for translation and Z-rotation on instances, which are preserved
    // in the transformation matrix and not applied to the mesh transform.

    const auto              instance_matrix = mo->instances[m_instance]->get_transformation().get_matrix_no_offset();
    const Transformation    cut_transformation = Transformation(m_cut_matrix);
    const Transform3d       inverse_cut_matrix = cut_transformation.get_rotation_matrix().inverse() * translation_transform(-1. * cut_transformation.get_offset());

    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    const bool cut_selected_only = !m_cut_volume_idxs.empty();
    // A volume-filtered cut keeps everything in one object, so KeepAsParts is required.
    assert(!cut_selected_only || m_attributes.has(ModelObjectCutAttribute::KeepAsParts));
    bool ok = true;
    for (size_t vol_idx = 0; vol_idx < mo->volumes.size(); ++vol_idx) {
        ModelVolume* volume = mo->volumes[vol_idx];
        // Save painting data before reset_extra_facets() discards it.
        if (m_attributes.has(ModelObjectCutAttribute::KeepPaint)) {
            saved_paintings.emplace_back(volume->save_painting());
            if (saved_paintings.back()) {
                // Transform mesh to cut space (same transform as process_volume_cut applies)
                saved_paintings.back()->mesh.transform(instance_matrix * volume->get_matrix(), true);
            }
        }

        volume->reset_extra_facets();

        if (!volume->is_model_part()) {
            if (volume->cut_info.is_processed)
                process_modifier_cut(volume, instance_matrix, inverse_cut_matrix, m_attributes, upper, lower);
            else
                process_connector_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower, dowels);
        }
        else if (!volume->mesh().empty()) {
            if (cut_selected_only &&
                std::find(m_cut_volume_idxs.begin(), m_cut_volume_idxs.end(), int(vol_idx)) == m_cut_volume_idxs.end())
                process_untouched_volume(volume, instance_matrix, inverse_cut_matrix, m_cut_matrix, upper);
            else
                split_solid_volume(volume, instance_matrix, upper, lower, ok);
        }
    }

    // A failed split (e.g. a mesh boolean that could not be computed) aborts the cut and leaves the
    // source object untouched.
    if (!ok) {
        m_model.clear_objects();
        return m_model.objects;
    }

    // Post-process cut parts

    if (m_attributes.has(ModelObjectCutAttribute::KeepAsParts) && upper->volumes.empty()) {
        m_model = Model();
        m_model.objects.push_back(upper);
        return m_model.objects;
    }

    ModelObjectPtrs cut_object_ptrs;

    if (m_attributes.has(ModelObjectCutAttribute::KeepAsParts) && !upper->volumes.empty()) {
        reset_instance_transformation(upper, m_instance, m_cut_matrix);
        cut_object_ptrs.push_back(upper);
    }
    else {
        // Delete all modifiers which are not intersecting with solid parts bounding box
        auto delete_extra_modifiers = [this](ModelObject* mo) {
            if (!mo) return;
            const BoundingBoxf3 obj_bb = mo->instance_bounding_box(m_instance);
            const Transform3d inst_matrix = mo->instances[m_instance]->get_transformation().get_matrix();

            for (int i = int(mo->volumes.size()) - 1; i >= 0; --i)
                if (const ModelVolume* vol = mo->volumes[i];
                    !vol->is_model_part() && !vol->is_cut_connector()) {
                    auto bb = vol->mesh().transformed_bounding_box(inst_matrix * vol->get_matrix());
                    if (!obj_bb.intersects(bb))
                        mo->delete_volume(i);
                }
        };

        post_process(upper, lower, cut_object_ptrs);
        delete_extra_modifiers(upper);
        delete_extra_modifiers(lower);

        if (m_attributes.has(ModelObjectCutAttribute::CreateDowels) && !dowels.empty()) {
            for (auto dowel : dowels) {
                reset_instance_transformation(dowel, m_instance);
                dowel->name += "-Dowel-" + dowel->volumes[0]->name;
                cut_object_ptrs.push_back(dowel);
            }
        }
    }

    finalize(cut_object_ptrs, saved_paintings);

    BOOST_LOG_TRIVIAL(trace) << "ModelObject::cut - end";

    return m_model.objects;
}

const ModelObjectPtrs& Cut::perform_with_plane()
{
    return perform_split([this](const ModelVolume* volume, const Transform3d& instance_matrix, ModelObject* upper, ModelObject* lower, bool& ok) {
        ok = true;
        process_solid_part_cut(volume, instance_matrix, m_cut_matrix, m_attributes, upper, lower);
    });
}

const ModelObjectPtrs& Cut::perform_with_shape(const TriangleMesh& cutter)
{
    bool hit = false;
    perform_split([this, &cutter, &hit](const ModelVolume* volume, const Transform3d& instance_matrix, ModelObject* upper, ModelObject* lower, bool&) {
        process_shape_cut(volume, instance_matrix, m_cut_matrix, cutter, m_attributes, upper, lower, hit);
    });
    // If the shape missed every part there is nothing to split, so report the empty result to the caller.
    if (!hit)
        m_model.clear_objects();
    return m_model.objects;
}

static void distribute_modifiers_from_object(ModelObject* from_obj, const int instance_idx, ModelObject* to_obj1, ModelObject* to_obj2)
{
    auto              obj1_bb = to_obj1 ? to_obj1->instance_bounding_box(instance_idx) : BoundingBoxf3();
    auto              obj2_bb = to_obj2 ? to_obj2->instance_bounding_box(instance_idx) : BoundingBoxf3();
    const Transform3d inst_matrix = from_obj->instances[instance_idx]->get_transformation().get_matrix();

    for (ModelVolume* vol : from_obj->volumes)
        if (!vol->is_model_part()) {
            // Don't add modifiers which are processed connectors
            if (vol->cut_info.is_connector && !vol->cut_info.is_processed)
                continue;
            auto bb = vol->mesh().transformed_bounding_box(inst_matrix * vol->get_matrix());
            // Don't add modifiers which are not intersecting with solid parts
            if (obj1_bb.intersects(bb))
                to_obj1->add_volume(*vol);
            if (obj2_bb.intersects(bb))
                to_obj2->add_volume(*vol);
        }
}

static void merge_solid_parts_inside_object(ModelObjectPtrs& objects)
{
    for (ModelObject* mo : objects) {
        TriangleMesh mesh;
        // Merge all SolidPart but not Connectors
        for (const ModelVolume* mv : mo->volumes) {
            if (mv->is_model_part() && !mv->is_cut_connector()) {
                TriangleMesh m = mv->mesh();
                m.transform(mv->get_matrix());
                mesh.merge(m);
            }
        }
        if (!mesh.empty()) {
            ModelVolume* new_volume = mo->add_volume(mesh);
            new_volume->name = mo->name;
            // Delete all merged SolidPart but not Connectors
            for (int i = int(mo->volumes.size()) - 2; i >= 0; --i) {
                const ModelVolume* mv = mo->volumes[i];
                if (mv->is_model_part() && !mv->is_cut_connector())
                    mo->delete_volume(i);
            }
            // Ensuring that volumes start with solid parts for proper slicing
            mo->sort_volumes(true);
        }
    }
}


const ModelObjectPtrs& Cut::perform_by_contour(const ModelObject* src_object, std::vector<Part> parts, int dowels_count)
{
    ModelObject* cut_mo = m_model.objects.front();

    // Clone the object to duplicate instances, materials etc.
    ModelObject* upper{ nullptr };
    if (m_attributes.has(ModelObjectCutAttribute::KeepUpper)) cut_mo->clone_for_cut(&upper);
    ModelObject* lower{ nullptr };
    if (m_attributes.has(ModelObjectCutAttribute::KeepLower)) cut_mo->clone_for_cut(&lower);

    if (upper && lower) {
        upper->name = upper->name + "_A";
        lower->name = lower->name + "_B";
    }

    // Save painting data so we later can remap it.
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    if (m_attributes.has(ModelObjectCutAttribute::KeepPaint)) {
        const auto instance_matrix = src_object->instances[m_instance]->get_transformation().get_matrix_no_offset();
        for (const auto volume : src_object->volumes) {
            saved_paintings.emplace_back(volume->save_painting());
            if (saved_paintings.back()) {
                // Transform mesh to cut space (same transform as process_volume_cut applies)
                saved_paintings.back()->mesh.transform(instance_matrix * volume->get_matrix(), true);
            }
        }
    }

    const size_t cut_parts_cnt = parts.size();
    bool has_modifiers = false;

    // Distribute SolidParts to the Upper/Lower object
    for (size_t id = 0; id < cut_parts_cnt; ++id) {
        if (parts[id].is_modifier)
            has_modifiers = true; // modifiers will be added later to the related parts
        else if (ModelObject* obj = (parts[id].selected ? upper : lower))
            obj->add_volume(*(cut_mo->volumes[id]));
    }

    if (has_modifiers) {
        // Distribute Modifiers to the Upper/Lower object
        distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
    }

    ModelObjectPtrs cut_object_ptrs;

    ModelVolumePtrs& volumes = cut_mo->volumes;
    if (volumes.size() == cut_parts_cnt) {
        // Means that object is cut without connectors

        // Just add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        // replace initial objects in model with cut object 
        finalize(cut_object_ptrs, saved_paintings);
    }
    else if (volumes.size() > cut_parts_cnt) {
        // Means that object is cut with connectors

        // All volumes are distributed to Upper / Lower object,
        // So we don’t need them anymore
        for (size_t id = 0; id < cut_parts_cnt; id++)
            delete* (volumes.begin() + id);
        volumes.erase(volumes.begin(), volumes.begin() + cut_parts_cnt);

        // Perform cut just to get connectors
        Cut cut(cut_mo, m_instance, m_cut_matrix, m_attributes);
        const ModelObjectPtrs& cut_connectors_obj = cut.perform_with_plane();
        assert(dowels_count > 0 ? cut_connectors_obj.size() >= 3 : cut_connectors_obj.size() == 2);

        // Connectors from upper object
        for (const ModelVolume* volume : cut_connectors_obj[0]->volumes)
            upper->add_volume(*volume, volume->type());

        // Connectors from lower object
        for (const ModelVolume* volume : cut_connectors_obj[1]->volumes)
            lower->add_volume(*volume, volume->type());

        // Add Upper and Lower objects to cut_object_ptrs
        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);

        // replace initial objects in model with cut object
        finalize(cut_object_ptrs, saved_paintings);

        // Add Dowel-connectors as separate objects to model
        if (cut_connectors_obj.size() >= 3)
            for (size_t id = 2; id < cut_connectors_obj.size(); id++)
                m_model.add_object(*cut_connectors_obj[id]);
    }

    return m_model.objects;
}


const ModelObjectPtrs& Cut::perform_with_groove(const Groove&       groove,
                                                const Transform3d&  rotation_m,
                                                const int           groove_count,
                                                const float         groove_gap,
                                                const float         m_radius,
                                                bool                keep_as_parts /* = false*/)
{
    ModelObject* cut_mo = m_model.objects.front();

    // Clone the object to duplicate instances, materials etc.
    ModelObject* upper{ nullptr };
    cut_mo->clone_for_cut(&upper);
    ModelObject* lower{ nullptr };
    cut_mo->clone_for_cut(&lower);

    if (upper && lower) {
        upper->name = upper->name + "_A";
        lower->name = lower->name + "_B";
    }

    // Save painting data so we later can remap it.
    std::vector<std::optional<TriangleSelector::SavedPainting>> saved_paintings;
    if (m_attributes.has(ModelObjectCutAttribute::KeepPaint)) {
        const auto instance_matrix = cut_mo->instances[m_instance]->get_transformation().get_matrix_no_offset();
        for (const auto volume : cut_mo->volumes) {
            saved_paintings.emplace_back(volume->save_painting());
            if (saved_paintings.back()) {
                // Transform mesh to cut space (same transform as process_volume_cut applies)
                saved_paintings.back()->mesh.transform(instance_matrix * volume->get_matrix(), true);
            }
        }
    }

    const double groove_half_depth = 0.5 * double(groove.depth);

    Model tmp_model_for_cut = Model();

    Model tmp_model = Model();
    tmp_model.add_object(*cut_mo);
    ModelObject* tmp_object = tmp_model.objects.front();

    auto add_volumes_from_cut = [](ModelObject* object, const ModelObjectCutAttribute attribute, const Model& tmp_model_for_cut) {
        const auto& volumes = tmp_model_for_cut.objects.front()->volumes;
        for (const ModelVolume* volume : volumes)
            if (volume->is_model_part()) {
                if ((attribute == ModelObjectCutAttribute::KeepUpper && volume->is_from_upper()) ||
                    (attribute != ModelObjectCutAttribute::KeepUpper && !volume->is_from_upper())) {
                    ModelVolume* new_vol = object->add_volume(*volume);
                    new_vol->reset_from_upper();
                }
            }
    };

    auto cut = [this, add_volumes_from_cut]
                (ModelObject* object, const Transform3d& cut_matrix, const ModelObjectCutAttribute add_volumes_attribute, Model& tmp_model_for_cut) {
        Cut cut(object, m_instance, cut_matrix);

        tmp_model_for_cut = Model();
        tmp_model_for_cut.add_object(*cut.perform_with_plane().front());
        assert(!tmp_model_for_cut.objects.empty());

        object->clear_volumes();
        add_volumes_from_cut(object, add_volumes_attribute, tmp_model_for_cut);
        reset_instance_transformation(object, m_instance);
    };

    // cut by upper plane (+Z)
    {
        const Transform3d cut_matrix_upper = translation_transform(rotation_m * (groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;

        cut(tmp_object, cut_matrix_upper, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
        add_volumes_from_cut(upper, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
    }

    // cut by lower plane (-Z)
    {
        const Transform3d cut_matrix_lower = translation_transform(rotation_m * (-groove_half_depth * Vec3d::UnitZ())) * m_cut_matrix;

        cut(tmp_object, cut_matrix_lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
    }

    // Compute same slot outer width used in preview plane
    const float  groove_width     = calculate_groove_width(groove, m_radius);

    ModelObject* groove_object{nullptr};

    // multiple cuts
    for (int i = 0; i < groove_count; i++) {
        bool is_first_groove = i == 0; 
        bool is_last_groove = i == groove_count - 1; 

        // Calculate the x-axis offset for this dovetail
        float groove_offset_factor_start = -.5 * ((groove_count - 1));
        float groove_offset_factor       = groove_offset_factor_start + i;

        float offset_x = groove_offset_factor * (groove_gap + groove_width);


        tmp_object->clone_for_cut(&groove_object);
        for (ModelVolume* volume : tmp_object->volumes) {
            ModelVolume* new_vol = groove_object->add_volume(*volume);
            new_vol->reset_from_upper();
        }

        // isolate area of current groove
        if (!is_first_groove) {
            float left_cut_position = (-groove_gap / 2.f) - (groove_width / 2.f) + offset_x;

            const Transform3d cut_matrix_left = translation_transform(rotation_m * (left_cut_position * Vec3d::UnitX())) *
                                                m_cut_matrix * rotation_transform(Vec3d(0, M_PI / 2.0, 0));

            cut(groove_object, cut_matrix_left, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        }
        if (!is_last_groove) {
            float right_cut_position = (groove_gap / 2.f) + (groove_width / 2.f) + offset_x;

            const Transform3d cut_matrix_right = translation_transform(rotation_m * (right_cut_position * Vec3d::UnitX())) *
                                                 m_cut_matrix * rotation_transform(Vec3d(0, M_PI / 2.0, 0));
            cut(groove_object, cut_matrix_right, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
        }

        const Transform3d groove_translation = translation_transform(rotation_m * (offset_x * Vec3d::UnitX()));
        // cut middle part with 2 angles and add parts to related upper/lower objects
        const double h_side_shift = 0.5 * double(groove.width + groove.depth / tan(groove.flaps_angle));

        // cut by angle1 plane
        {
            const Transform3d cut_matrix_angle1 = groove_translation * translation_transform(rotation_m * (-h_side_shift * Vec3d::UnitX())) *
                                                  m_cut_matrix * rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));

            cut(groove_object, cut_matrix_angle1, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
            add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        }

        // cut by angle2 plane
        {
            const Transform3d cut_matrix_angle2 = groove_translation * translation_transform(rotation_m * (h_side_shift * Vec3d::UnitX())) *
                                                  m_cut_matrix * rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));

            cut(groove_object, cut_matrix_angle2, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);
            add_volumes_from_cut(lower, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        }

        // apply tolerance to the middle part
        {
            const double h_groove_shift_tolerance = groove_half_depth - (double)groove.depth_tolerance;

            const Transform3d cut_matrix_lower_tolerance = groove_translation * translation_transform(rotation_m * (-h_groove_shift_tolerance * Vec3d::UnitZ())) *
                                                           m_cut_matrix;
            cut(groove_object, cut_matrix_lower_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);

            const double h_side_shift_tolerance = h_side_shift - 0.5 * double(groove.width_tolerance);

            const Transform3d cut_matrix_angle1_tolerance = groove_translation * translation_transform(rotation_m * (-h_side_shift_tolerance * Vec3d::UnitX())) *
                                                            m_cut_matrix * rotation_transform(Vec3d(0, -groove.flaps_angle, -groove.angle));
            cut(groove_object, cut_matrix_angle1_tolerance, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);

            const Transform3d cut_matrix_angle2_tolerance = groove_translation * translation_transform(rotation_m * (h_side_shift_tolerance * Vec3d::UnitX())) *
                                                            m_cut_matrix * rotation_transform(Vec3d(0, groove.flaps_angle, groove.angle));
            cut(groove_object, cut_matrix_angle2_tolerance, ModelObjectCutAttribute::KeepUpper, tmp_model_for_cut);
        }

        add_volumes_from_cut(upper, ModelObjectCutAttribute::KeepLower, tmp_model_for_cut);

        groove_object->clear_volumes();
    }

    ModelObjectPtrs cut_object_ptrs;

    if (keep_as_parts) {
        // add volumes from lower object to the upper, but mark them as a lower
        const auto& volumes = lower->volumes;
        for (const ModelVolume* volume : volumes) {
            ModelVolume* new_vol = upper->add_volume(*volume);
            new_vol->cut_info.is_from_upper = false;
        }

        // add modifiers
        for (const ModelVolume* volume : cut_mo->volumes)
            if (!volume->is_model_part())
                upper->add_volume(*volume);

        cut_object_ptrs.push_back(upper);

        // add lower object to the cut_object_ptrs just to correct delete it from the Model destructor and avoid memory leaks
        cut_object_ptrs.push_back(lower);
    }
    else {
        // add modifiers if object has any
        for (const ModelVolume* volume : cut_mo->volumes)
            if (!volume->is_model_part()) {
                distribute_modifiers_from_object(cut_mo, m_instance, upper, lower);
                break;
            }

        assert(!upper->volumes.empty() && !lower->volumes.empty());

        // Add Upper and Lower parts to cut_object_ptrs

        post_process(upper, lower, cut_object_ptrs);

        // Now merge all model parts together:
        merge_solid_parts_inside_object(cut_object_ptrs);
    }

    finalize(cut_object_ptrs, saved_paintings);

    return m_model.objects;
}

float Cut::calculate_groove_width (const Cut::Groove& groove, const float m_radius)
{
    // Compute same slot outer width used in preview plane
    const double flap_width             = is_approx(groove.flaps_angle, 0.f) ? groove.depth : groove.depth / sin(groove.flaps_angle);
    const double total_flap_width       = 2.0 * flap_width * cos(groove.flaps_angle);
    const double slot_neck_half_width   = 0.5f * (groove.width);
    const double slot_mouth_half_width  = 0.5 * (groove.width + total_flap_width);
    const double plane_half_height      = 0.5f* (1.5f * (1.5f *m_radius));
    const double flap_taper_offset      = plane_half_height * tan(groove.angle);
    const double slot_outer_x_max       = std::max(slot_mouth_half_width + flap_taper_offset, slot_neck_half_width + flap_taper_offset);

    return float(2.0 * slot_outer_x_max);
}

} // namespace Slic3r

