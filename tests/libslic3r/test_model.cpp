#include <catch2/catch_all.hpp>
#include <algorithm>
#include <string>
#include <cstddef>
#include <utility>
#include <vector>
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/Polygon.hpp"
#include "libslic3r/BoundingBox.hpp"
#include "libslic3r/Point.hpp"

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_message.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include "libslic3r/MeshRepair.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/ObjectID.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/libslic3r.h"
// [ORCAPORT:MB-3]
#include "libslic3r/OrcaExt/MergeParts.hpp"
// [ORCAPORT:MB-4]
#include "libslic3r/OrcaExt/SplitParts.hpp"

using namespace Slic3r;

// convex_hull_2d does not clip geometry below the bed, so these cases avoid
// sinking transforms.
TEST_CASE("A part's 2D convex hull is its footprint projected onto the bed", "[Model]")
{
    Model model;
    ModelObject* object = model.add_object();
    // Keep the cube's raw coordinates ([0,20] on every axis): the default
    // add_volume re-centers the geometry, which would move the footprint.
    object->add_volume(make_cube(20, 20, 20), ModelVolumeType::MODEL_PART, false);

    SECTION("identity transform yields the 20 mm square") {
        const Polygon hull   = object->convex_hull_2d(Geometry::Transformation{}.get_matrix());
        const BoundingBox bb = hull.bounding_box();
        CHECK(hull.size() == 4);
        CHECK(bb.min.x() == scaled(0.));
        CHECK(bb.min.y() == scaled(0.));
        CHECK(bb.max.x() == scaled(20.));
        CHECK(bb.max.y() == scaled(20.));
    }

    SECTION("scaling and offset move and grow the footprint") {
        Geometry::Transformation t;
        t.set_scaling_factor({2, 2, 2}); // cube now spans [0,40]
        t.set_offset({10, 5, 0});        // then shift +10 in X, +5 in Y

        const Polygon hull   = object->convex_hull_2d(t.get_matrix());
        const BoundingBox bb = hull.bounding_box();
        CHECK(hull.size() == 4);
        CHECK(bb.min.x() == scaled(10.));
        CHECK(bb.min.y() == scaled(5.));
        CHECK(bb.max.x() == scaled(50.));
        CHECK(bb.max.y() == scaled(45.));
    }
}

TEST_CASE("An object's raw mesh keeps the triangles of each part on its own vertices", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    TriangleMesh second = make_cube(10, 10, 10);
    second.translate(30, 0, 0);
    object->add_volume(std::move(second), ModelVolumeType::MODEL_PART, false);

    // Two separate cubes stay two closed components, one around each cube.
    const std::vector<indexed_triangle_set> parts = its_split(object->raw_indexed_triangle_set());
    REQUIRE(parts.size() == 2);
    std::vector<double> min_x;
    for (const indexed_triangle_set &part : parts) {
        CHECK(part.indices.size() == 12);
        min_x.push_back(bounding_box(part).min.x());
    }
    std::sort(min_x.begin(), min_x.end());
    CHECK_THAT(min_x.front(), Catch::Matchers::WithinAbs(0., 1e-2));
    CHECK_THAT(min_x.back(), Catch::Matchers::WithinAbs(30., 1e-2));
}

TEST_CASE("An instance added from another's scale, rotation and mirror matches it", "[Model]")
{
    // Add instance and Fill bed copy an instance this way.
    const int case_idx = GENERATE(0, 1);
    Transform3d trafo = Transform3d::Identity();
    if (case_idx == 0)
        trafo.linear() = Eigen::AngleAxisd(0.5 * PI, Vec3d::UnitZ()).toRotationMatrix() * Vec3d(-1., 1., 1.).asDiagonal();
    else
        trafo.linear() << 4.4408921e-16,   0.819152044,    0.573576436,
                          1.0,            -4.4408921e-16, -1.11022302e-16,
                         -5.55111512e-17, -0.573576436,    0.819152044;
    CAPTURE(case_idx);

    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 20, 30));
    ModelInstance *original = object->add_instance();
    original->set_transformation(Geometry::Transformation(trafo));
    const ModelInstance *copy = object->add_instance(original->get_offset(), original->get_scaling_factor(),
                                                     original->get_rotation(), original->get_mirror());
    CHECK(copy->get_matrix().linear().isApprox(original->get_matrix().linear(), 1e-9));
}

namespace {

// A flat triangle first, then a cube for each offset, all in one mesh.
TriangleMesh flat_triangle_then_cubes(const std::vector<double> &cube_offsets)
{
    indexed_triangle_set its;
    its.vertices = {{-20.f, 0.f, 0.f}, {-10.f, 0.f, 0.f}, {-15.f, 5.f, 0.f}};
    its.indices  = {{0, 1, 2}};
    for (double offset : cube_offsets) {
        TriangleMesh cube = make_cube(10, 10, 10);
        cube.translate(float(offset), 0.f, 0.f);
        its_merge(its, cube.its);
    }
    return TriangleMesh(its);
}

} // namespace

TEST_CASE("Splitting a part drops pieces enclosing no volume and keeps the part itself", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(flat_triangle_then_cubes({0., 30.}), ModelVolumeType::MODEL_PART, false);
    REQUIRE(volume->is_splittable());

    CHECK(volume->split(1, false) == 2);
    REQUIRE(object->volumes.size() == 2);
    CHECK(std::find(object->volumes.begin(), object->volumes.end(), volume) != object->volumes.end());
    for (const ModelVolume *part : object->volumes) {
        CHECK(part->mesh().facets_count() == 12);
        CHECK_FALSE(part->is_splittable());
    }
}

TEST_CASE("Splitting a part with a single solid piece keeps just that piece", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(flat_triangle_then_cubes({0.}), ModelVolumeType::MODEL_PART, false);

    CHECK(volume->split(1, false) == 1);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == volume);
    CHECK(volume->mesh().facets_count() == 12);
}

TEST_CASE("Splitting a part made only of pieces enclosing no volume leaves it whole", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    indexed_triangle_set its;
    its.vertices = {{0.f, 0.f, 0.f}, {10.f, 0.f, 0.f}, {5.f, 5.f, 0.f}, {20.f, 0.f, 0.f}, {30.f, 0.f, 0.f}, {25.f, 5.f, 0.f}};
    its.indices  = {{0, 1, 2}, {3, 4, 5}};
    ModelVolume *volume = object->add_volume(TriangleMesh(its), ModelVolumeType::MODEL_PART, false);

    CHECK(volume->split(1, false) == 1);
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front() == volume);
    CHECK(volume->mesh().facets_count() == 2);
}

TEST_CASE("Replacing a part's mesh updates whether it can be split", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    TriangleMesh two_cubes = make_cube(10, 10, 10);
    TriangleMesh second    = make_cube(10, 10, 10);
    second.translate(30.f, 0.f, 0.f);
    two_cubes.merge(second);
    ModelVolume *volume = object->add_volume(std::move(two_cubes), ModelVolumeType::MODEL_PART, false);
    REQUIRE(volume->is_splittable());

    volume->set_mesh(make_cube(10, 10, 10));
    CHECK_FALSE(volume->is_splittable());
}

TEST_CASE("Repairing a part replaces its mesh only when the repair changes it", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    TriangleMesh flipped = make_cube(10, 10, 10);
    std::swap(flipped.its.indices.front()[1], flipped.its.indices.front()[2]);
    ModelVolume *broken = object->add_volume(std::move(flipped), ModelVolumeType::MODEL_PART, false);
    TriangleMesh cube = make_cube(10, 10, 10);
    cube.translate(30.f, 0.f, 0.f);
    ModelVolume *valid = object->add_volume(std::move(cube), ModelVolumeType::MODEL_PART, false);
    const ObjectID broken_id = broken->id();
    const ObjectID valid_id  = valid->id();

    MeshRepairStats stats;
    REQUIRE(broken->repair(false, &stats));
    CHECK(stats.modified);
    CHECK(broken->id() != broken_id);
    CHECK_THAT(its_volume(broken->mesh().its), Catch::Matchers::WithinRel(1000., 1e-6));
    CHECK_FALSE(broken->get_convex_hull().empty());

    REQUIRE(valid->repair(false, &stats));
    CHECK_FALSE(stats.modified);
    CHECK(valid->id() == valid_id);
}

// [ORCAPORT:MB-3] BEGIN - merge only the selected parts of one object into one part
TEST_CASE("Merging selected parts leaves the object's other parts and instances alone", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();

    auto cube_at = [](float x) {
        TriangleMesh mesh = make_cube(10, 10, 10);
        mesh.translate(x, 0.f, 0.f);
        return mesh;
    };
    object->add_volume(cube_at(0.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(cube_at(5.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(cube_at(100.f), ModelVolumeType::MODEL_PART, false);
    object->volumes[0]->name = "A";
    object->volumes[1]->name = "B";
    object->volumes[2]->name = "C";

    bool        welded = false;
    std::string error;
    REQUIRE(OrcaExt::merge_parts_into_one(*object, {0, 1}, false, welded, error));
    CHECK(error.empty());
    CHECK(welded);
    REQUIRE(object->volumes.size() == 2);
    // The merged part took the lowest selected index and its name; the third part is untouched.
    CHECK(object->volumes[0]->name == "A");
    CHECK(object->volumes[1]->name == "C");
    CHECK(object->instances.size() == 1);

    // The merged geometry spans both source cubes ([0,15] on X); a join that dropped the parts'
    // transforms would leave it inside the first cube.
    const BoundingBoxf3 merged_bb = object->volumes[0]->mesh().bounding_box();
    CHECK_THAT(merged_bb.min.x(), Catch::Matchers::WithinAbs(0.0, 1e-2));
    CHECK_THAT(merged_bb.max.x(), Catch::Matchers::WithinAbs(15.0, 1e-2));
    CHECK(object->volumes[0]->get_matrix().isApprox(Transform3d::Identity()));

    // The part that was not selected stayed where it was.
    const BoundingBoxf3 kept_bb = object->volumes[1]->mesh().bounding_box();
    CHECK_THAT(kept_bb.min.x(), Catch::Matchers::WithinAbs(100.0, 1e-2));
    CHECK_THAT(kept_bb.max.x(), Catch::Matchers::WithinAbs(110.0, 1e-2));
}

TEST_CASE("Merging the only two parts of an object leaves one part covering both", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    TriangleMesh second = make_cube(10, 10, 10);
    second.translate(50.f, 0.f, 0.f);
    object->add_volume(std::move(second), ModelVolumeType::MODEL_PART, false);

    bool        welded = false;
    std::string error;
    REQUIRE(OrcaExt::merge_parts_into_one(*object, {0, 1}, false, welded, error));
    REQUIRE(object->volumes.size() == 1);

    // Parts that do not touch are either welded by the kernel or joined as separate shells; both
    // outcomes must keep the geometry of both parts.
    const BoundingBoxf3 bb = object->volumes[0]->mesh().bounding_box();
    CHECK_THAT(bb.min.x(), Catch::Matchers::WithinAbs(0.0, 1e-2));
    CHECK_THAT(bb.max.x(), Catch::Matchers::WithinAbs(60.0, 1e-2));
}

TEST_CASE("Merging two parts that share a whole face leaves one part", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    TriangleMesh upper = make_cube(10, 10, 10);
    upper.translate(0.f, 0.f, 10.f);
    object->add_volume(std::move(upper), ModelVolumeType::MODEL_PART, false);

    bool        welded = false;
    std::string error;
    REQUIRE(OrcaExt::merge_parts_into_one(*object, {0, 1}, false, welded, error));
    REQUIRE(object->volumes.size() == 1);

    // The face the two parts shared is not a surface any more: the merged part spans both.
    const BoundingBoxf3 bb = object->volumes[0]->mesh().bounding_box();
    CHECK_THAT(bb.min.z(), Catch::Matchers::WithinAbs(0.0, 1e-2));
    CHECK_THAT(bb.max.z(), Catch::Matchers::WithinAbs(20.0, 1e-2));
}

TEST_CASE("Merging refuses a selection that has nothing to fuse", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    object->add_volume(make_cube(5, 5, 5), ModelVolumeType::NEGATIVE_VOLUME, false);
    object->add_volume(make_cube(4, 4, 4), ModelVolumeType::PARAMETER_MODIFIER, false);

    bool        welded = false;
    std::string error;
    // One positive on its own, and negatives without a positive to cut.
    CHECK_FALSE(OrcaExt::merge_parts_into_one(*object, {0}, false, welded, error));
    CHECK_FALSE(error.empty());
    CHECK_FALSE(OrcaExt::merge_parts_into_one(*object, {1}, false, welded, error));
    // A modifier has no CSG meaning, so a selection that contains one is refused whole.
    CHECK_FALSE(OrcaExt::merge_parts_into_one(*object, {0, 2}, false, welded, error));
    CHECK(object->volumes.size() == 3);
}
// [ORCAPORT:MB-3] END

// [ORCAPORT:MB-4] BEGIN - move selected parts of one object into their own objects
namespace {

// Where a part really sits: the instance transform composed with the part's own transform.
BoundingBoxf3 world_bounding_box(const ModelObject &object, const ModelVolume &volume, size_t instance_idx)
{
    REQUIRE(instance_idx < object.instances.size());
    const Transform3d placement = object.instances[instance_idx]->get_matrix() * volume.get_matrix();
    return volume.mesh().bounding_box().transformed(placement);
}

} // namespace

TEST_CASE("Splitting one part off leaves the object's other parts where they were", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();

    auto cube_at = [](float x) {
        TriangleMesh mesh = make_cube(10, 10, 10);
        mesh.translate(x, 0.f, 0.f);
        return mesh;
    };
    object->add_volume(cube_at(0.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(cube_at(20.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(cube_at(40.f), ModelVolumeType::MODEL_PART, false);
    object->volumes[0]->name = "A";
    object->volumes[1]->name = "B";
    object->volumes[2]->name = "C";

    const BoundingBoxf3 part_b_world = world_bounding_box(*object, *object->volumes[1], 0);
    const BoundingBoxf3 part_c_world = world_bounding_box(*object, *object->volumes[2], 0);

    std::vector<size_t> new_idxs;
    std::string         error;
    REQUIRE(OrcaExt::split_parts_to_objects(*object, {2}, new_idxs, error));
    CHECK(error.empty());
    REQUIRE(new_idxs.size() == 1);
    REQUIRE(model.objects.size() == 2);

    // The source object keeps the two parts it was not asked for, in their places.
    REQUIRE(object->volumes.size() == 2);
    CHECK(object->volumes[0]->name == "A");
    CHECK(object->volumes[1]->name == "B");
    const BoundingBoxf3 kept_b_world = world_bounding_box(*object, *object->volumes[1], 0);
    CHECK_THAT(kept_b_world.min.x(), Catch::Matchers::WithinAbs(part_b_world.min.x(), 1e-2));
    CHECK_THAT(kept_b_world.max.x(), Catch::Matchers::WithinAbs(part_b_world.max.x(), 1e-2));

    // The extracted part became its own object, at the place it had, named after the part.
    const ModelObject *extracted = model.objects[new_idxs.front()];
    REQUIRE(extracted->volumes.size() == 1);
    CHECK(extracted->name == "C");
    CHECK(extracted->instances.size() == 1);
    const BoundingBoxf3 extracted_world = world_bounding_box(*extracted, *extracted->volumes.front(), 0);
    CHECK_THAT(extracted_world.min.x(), Catch::Matchers::WithinAbs(part_c_world.min.x(), 1e-2));
    CHECK_THAT(extracted_world.max.x(), Catch::Matchers::WithinAbs(part_c_world.max.x(), 1e-2));
    CHECK_THAT(extracted_world.min.z(), Catch::Matchers::WithinAbs(part_c_world.min.z(), 1e-2));

    // The part that was left in the source object did not move either.
    const BoundingBoxf3 source_a_world = world_bounding_box(*object, *object->volumes[0], 0);
    CHECK_THAT(source_a_world.min.x(), Catch::Matchers::WithinAbs(0.0, 1e-2));
    CHECK_THAT(source_a_world.max.x(), Catch::Matchers::WithinAbs(10.0, 1e-2));
}

TEST_CASE("A split-off part keeps a rotated and translated transform", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();
    TriangleMesh other = make_cube(10, 10, 10);
    other.translate(-30.f, 0.f, 0.f);
    object->add_volume(std::move(other), ModelVolumeType::MODEL_PART, false);
    ModelVolume *volume = object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    Geometry::Transformation placement;
    placement.set_rotation(Vec3d(0., 0., 1.5707963267948966)); // 90 deg about Z
    placement.set_offset(Vec3d(15., 5., 0.));
    volume->set_transformation(placement);

    const BoundingBoxf3 part_world = world_bounding_box(*object, *volume, 0);

    std::vector<size_t> new_idxs;
    std::string         error;
    REQUIRE(OrcaExt::split_parts_to_objects(*object, {1}, new_idxs, error));
    REQUIRE(new_idxs.size() == 1);
    REQUIRE(object->volumes.size() == 1);

    const ModelObject *extracted = model.objects[new_idxs.front()];
    REQUIRE(extracted->volumes.size() == 1);
    const BoundingBoxf3 extracted_world = world_bounding_box(*extracted, *extracted->volumes.front(), 0);
    CHECK_THAT(extracted_world.min.x(), Catch::Matchers::WithinAbs(part_world.min.x(), 1e-2));
    CHECK_THAT(extracted_world.min.y(), Catch::Matchers::WithinAbs(part_world.min.y(), 1e-2));
    CHECK_THAT(extracted_world.max.x(), Catch::Matchers::WithinAbs(part_world.max.x(), 1e-2));
    CHECK_THAT(extracted_world.max.y(), Catch::Matchers::WithinAbs(part_world.max.y(), 1e-2));
}

TEST_CASE("Splitting a part off when the source has two copies keeps both copies", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    TriangleMesh second = make_cube(10, 10, 10);
    second.translate(40.f, 0.f, 0.f);
    object->add_volume(std::move(second), ModelVolumeType::MODEL_PART, false);
    object->add_instance(*object->instances.front());
    object->instances.back()->set_offset(Vec3d(0., 30., 0.));

    std::vector<size_t> new_idxs;
    std::string         error;
    REQUIRE(OrcaExt::split_parts_to_objects(*object, {1}, new_idxs, error));
    REQUIRE(new_idxs.size() == 1);

    const ModelObject *extracted = model.objects[new_idxs.front()];
    REQUIRE(extracted->instances.size() == 2);
    const BoundingBoxf3 copy0 = world_bounding_box(*extracted, *extracted->volumes.front(), 0);
    const BoundingBoxf3 copy1 = world_bounding_box(*extracted, *extracted->volumes.front(), 1);
    CHECK_THAT(copy0.min.x(), Catch::Matchers::WithinAbs(40.0, 1e-2));
    CHECK_THAT(copy1.min.x(), Catch::Matchers::WithinAbs(40.0, 1e-2));
    CHECK_THAT(copy1.min.y() - copy0.min.y(), Catch::Matchers::WithinAbs(30.0, 1e-2));
}

TEST_CASE("Splitting refuses a selection that would leave no model part", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    object->add_volume(make_cube(5, 5, 5), ModelVolumeType::NEGATIVE_VOLUME, false);

    std::vector<size_t> new_idxs;
    std::string         error;
    CHECK_FALSE(OrcaExt::split_parts_to_objects(*object, {0, 1}, new_idxs, error));
    CHECK_FALSE(error.empty());
    CHECK(new_idxs.empty());
    CHECK_FALSE(OrcaExt::split_parts_to_objects(*object, {2}, new_idxs, error));
    CHECK(model.objects.size() == 1);
    CHECK(object->volumes.size() == 3);
}
// [ORCAPORT:MB-4] END

// [ORCAPORT:MB-6] BEGIN - a merge that also subtracts the selected negative volumes
TEST_CASE("Merging a part with inside and outside negatives cuts them out and consumes them", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();
    object->add_volume(make_cube(20, 20, 20), ModelVolumeType::MODEL_PART, false);
    TriangleMesh inside = make_cube(4, 4, 4); // centred on the part
    object->add_volume(std::move(inside), ModelVolumeType::NEGATIVE_VOLUME, false);
    TriangleMesh outside = make_cube(4, 4, 4);
    outside.translate(100.f, 100.f, 100.f); // nowhere near the part
    object->add_volume(std::move(outside), ModelVolumeType::NEGATIVE_VOLUME, false);

    bool        welded = false;
    std::string error;
    REQUIRE(OrcaExt::merge_parts_into_one(*object, {0, 1, 2}, false, welded, error));
    CHECK(error.empty());
    CHECK(welded);
    // Every selected negative is consumed, inside or not; the part keeps the cut of the inside one.
    REQUIRE(object->volumes.size() == 1);
    CHECK(object->volumes.front()->is_model_part());
    CHECK_THAT(its_volume(object->volumes.front()->mesh().its), Catch::Matchers::WithinRel(7936., 1e-3));
}
// [ORCAPORT:MB-6] END

// [ORCAPORT:MB-5] BEGIN - move selected volumes of one object into one new object
TEST_CASE("Moving selected volumes into one object keeps their type and placement", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_instance();

    auto box_at = [](float x, float size) {
        TriangleMesh mesh = make_cube(size, size, size);
        mesh.translate(x, 0.f, 0.f);
        return mesh;
    };
    object->add_volume(box_at(0.f, 10.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(box_at(30.f, 10.f), ModelVolumeType::MODEL_PART, false);
    object->add_volume(box_at(10.f, 4.f), ModelVolumeType::NEGATIVE_VOLUME, false);
    object->add_volume(box_at(20.f, 6.f), ModelVolumeType::PARAMETER_MODIFIER, false);
    object->volumes[0]->name = "moved_part";
    object->volumes[2]->name = "moved_cut";
    object->volumes[3]->name = "moved_modifier";
    object->volumes[0]->config.set("extruder", 2);

    const BoundingBoxf3 part_before = world_bounding_box(*object, *object->volumes[0], 0);
    const BoundingBoxf3 cut_before  = world_bounding_box(*object, *object->volumes[2], 0);
    const BoundingBoxf3 mod_before  = world_bounding_box(*object, *object->volumes[3], 0);
    const ObjectID      moved_id    = object->volumes[0]->id();

    size_t      new_idx = 0;
    std::string error;
    REQUIRE(OrcaExt::split_volumes_to_one_object(*object, {0, 2, 3}, new_idx, error));
    CHECK(error.empty());
    REQUIRE(model.objects.size() == 2);
    REQUIRE(object->volumes.size() == 1);

    const ModelObject *moved = model.objects[new_idx];
    CHECK(moved != object);
    CHECK(moved->name == object->name + "_parts");
    REQUIRE(moved->volumes.size() == 3);
    CHECK(moved->instances.size() == 1);

    // Types, names and per-volume settings travel with the volumes.
    CHECK(moved->volumes[0]->type() == ModelVolumeType::MODEL_PART);
    CHECK(moved->volumes[1]->type() == ModelVolumeType::NEGATIVE_VOLUME);
    CHECK(moved->volumes[2]->type() == ModelVolumeType::PARAMETER_MODIFIER);
    CHECK(moved->volumes[0]->name == "moved_part");
    CHECK(moved->volumes[1]->name == "moved_cut");
    CHECK(moved->volumes[2]->name == "moved_modifier");
    CHECK(moved->volumes[0]->config.extruder() == 2);
    // The volumes themselves moved, so their identity is preserved (nothing was copied).
    CHECK(moved->volumes[0]->id() == moved_id);

    // Nothing moved in world space.
    const BoundingBoxf3 part_after = world_bounding_box(*moved, *moved->volumes[0], 0);
    const BoundingBoxf3 cut_after  = world_bounding_box(*moved, *moved->volumes[1], 0);
    const BoundingBoxf3 mod_after  = world_bounding_box(*moved, *moved->volumes[2], 0);
    CHECK_THAT(part_after.min.x(), Catch::Matchers::WithinAbs(part_before.min.x(), 1e-2));
    CHECK_THAT(part_after.max.x(), Catch::Matchers::WithinAbs(part_before.max.x(), 1e-2));
    CHECK_THAT(cut_after.min.x(), Catch::Matchers::WithinAbs(cut_before.min.x(), 1e-2));
    CHECK_THAT(mod_after.min.x(), Catch::Matchers::WithinAbs(mod_before.min.x(), 1e-2));
    CHECK_THAT(mod_after.max.x(), Catch::Matchers::WithinAbs(mod_before.max.x(), 1e-2));

    // The part left behind stayed where it was.
    const BoundingBoxf3 kept = world_bounding_box(*object, *object->volumes.front(), 0);
    CHECK_THAT(kept.min.x(), Catch::Matchers::WithinAbs(30.0, 1e-2));
    CHECK_THAT(kept.max.x(), Catch::Matchers::WithinAbs(40.0, 1e-2));
}

TEST_CASE("Moving every volume of an object is refused", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    object->add_volume(make_cube(5, 5, 5), ModelVolumeType::NEGATIVE_VOLUME, false);

    size_t      new_idx = 0;
    std::string error;
    CHECK_FALSE(OrcaExt::split_volumes_to_one_object(*object, {0, 1}, new_idx, error));
    CHECK_FALSE(error.empty());
    CHECK(model.objects.size() == 1);
    CHECK(object->volumes.size() == 2);
}
// [ORCAPORT:MB-5] END

// A cube whose raw mesh spans [0, size] on every axis, with one instance at plate_offset on the plate.
static ModelObject *add_cube(Model &model, double size, const Vec3d &plate_offset)
{
    ModelObject *object = model.add_object();
    object->add_volume(make_cube(size, size, size), ModelVolumeType::MODEL_PART, false);
    object->add_instance()->set_offset(plate_offset);
    return object;
}

TEST_CASE("An object placed in the assembly view goes 10 mm to the right of the others", "[Model]")
{
    Model model;
    ModelObject *placed = add_cube(model, 20., Vec3d::Zero());
    placed->instances.front()->set_assemble_transformation(placed->instances.front()->get_transformation());
    const Vec3d plate_offset(128., 128., 5.);
    ModelObject *added = add_cube(model, 10., plate_offset);

    model.set_assembly_pos(added);

    const BoundingBoxf3 placed_box = placed->bounding_box_in_assembly_view();
    const BoundingBoxf3 added_box  = added->bounding_box_in_assembly_view();
    CHECK(added->instances.front()->is_assemble_initialized());
    CHECK_THAT(added_box.min.x(), Catch::Matchers::WithinAbs(placed_box.max.x() + 10., 1e-6));
    CHECK_THAT(added_box.center().y(), Catch::Matchers::WithinAbs(placed_box.center().y(), 1e-6));
    // The height comes from the plate, and the plate itself is left alone.
    CHECK_THAT(added_box.min.z(), Catch::Matchers::WithinAbs(plate_offset.z(), 1e-6));
    CHECK_THAT((added->instances.front()->get_offset() - plate_offset).norm(), Catch::Matchers::WithinAbs(0., 1e-6));
}

TEST_CASE("An object alone in the assembly view is centred on the origin and keeps its instance layout", "[Model]")
{
    Model model;
    ModelObject *object = add_cube(model, 10., {100., 100., 0.});
    object->add_instance()->set_offset({130., 100., 0.});

    model.set_assembly_pos(object);

    const BoundingBoxf3 box = object->bounding_box_in_assembly_view();
    CHECK_THAT(box.center().x(), Catch::Matchers::WithinAbs(0., 1e-6));
    CHECK_THAT(box.center().y(), Catch::Matchers::WithinAbs(0., 1e-6));
    const Vec3d spacing = object->instances[1]->get_assemble_offset() - object->instances[0]->get_assemble_offset();
    CHECK_THAT((spacing - Vec3d(30., 0., 0.)).norm(), Catch::Matchers::WithinAbs(0., 1e-6));
}

TEST_CASE("Ignored objects do not push an object aside in the assembly view", "[Model]")
{
    Model model;
    ModelObject *ignored = add_cube(model, 20., Vec3d::Zero());
    ignored->instances.front()->set_assemble_transformation(ignored->instances.front()->get_transformation());
    ModelObject *object = add_cube(model, 10., {128., 128., 0.});

    model.set_assembly_pos(object, {ignored});

    const BoundingBoxf3 box = object->bounding_box_in_assembly_view();
    CHECK_THAT(box.center().x(), Catch::Matchers::WithinAbs(0., 1e-6));
    CHECK_THAT(box.center().y(), Catch::Matchers::WithinAbs(0., 1e-6));
}
namespace {

// A box of the given size whose lowest corner is at `corner` in the file's coordinates. The mesh keeps them, unless
// `as_reader` centers it and keeps the same place in the volume's offset, as the file readers do.
ModelVolume *add_box(ModelObject *object, const Vec3d &size, const Vec3d &corner, bool as_reader = false)
{
    TriangleMesh box = make_cube(size.x(), size.y(), size.z());
    box.translate(corner.cast<float>());
    return object->add_volume(std::move(box), ModelVolumeType::MODEL_PART, as_reader);
}

// The vertices of every volume (or only of the model parts) of every instance of every object, in world coordinates,
// in a stable order.
std::vector<Vec3d> world_vertices(const Model &model, bool parts_only = false)
{
    std::vector<Vec3d> points;
    for (const ModelObject *object : model.objects)
        for (const ModelInstance *instance : object->instances)
            for (const ModelVolume *volume : object->volumes)
                if (!parts_only || volume->is_model_part())
                    for (const Vec3f &p : volume->mesh().its.vertices)
                        points.push_back(instance->get_matrix() * volume->get_matrix() * p.cast<double>());
    return points;
}

// The box around the given points.
BoundingBoxf3 points_box(const std::vector<Vec3d> &points)
{
    BoundingBoxf3 bb;
    for (const Vec3d &p : points)
        bb.merge(p);
    return bb;
}

// The larger distance between the matching corners of two boxes.
double box_distance(const BoundingBoxf3 &a, const BoundingBoxf3 &b) { return std::max((a.min - b.min).norm(), (a.max - b.max).norm()); }

// Largest distance between two lists of points of equal length.
double max_distance(const std::vector<Vec3d> &a, const std::vector<Vec3d> &b)
{
    REQUIRE(a.size() == b.size());
    double distance = 0.;
    for (size_t i = 0; i < a.size(); ++i)
        distance = std::max(distance, (a[i] - b[i]).norm());
    return distance;
}

// The points turned by `angle` around the vertical axis through `center`.
std::vector<Vec3d> turned(const std::vector<Vec3d> &points, double angle, const Vec2d &center)
{
    const Vec3d       center_3d(center.x(), center.y(), 0.);
    const Transform3d around = Geometry::translation_transform(center_3d) * Geometry::rotation_transform(angle * Vec3d::UnitZ()) *
                               Geometry::translation_transform(-center_3d);
    std::vector<Vec3d> out;
    for (const Vec3d &p : points)
        out.push_back(around * p);
    return out;
}

// The XY center of the box around the given points.
Vec2d layout_center(const std::vector<Vec3d> &points)
{
    const BoundingBoxf3 bb = points_box(points);
    return {bb.center().x(), bb.center().y()};
}

// The linear parts (rotation, scale, mirror) of the volumes and of the instances of an object.
std::vector<Matrix3d> linear_parts(const ModelObject &object)
{
    std::vector<Matrix3d> parts;
    for (const ModelVolume *volume : object.volumes)
        parts.push_back(volume->get_matrix().linear());
    for (const ModelInstance *instance : object.instances)
        parts.push_back(instance->get_matrix().linear());
    return parts;
}

} // namespace

TEST_CASE("Centering a placed object keeps it where it stands", "[Model]")
{
    const bool as_reader = GENERATE(false, true);
    CAPTURE(as_reader);

    Model model;
    ModelObject *object = model.add_object();
    // A part far from its object's origin, placed by its instance, as an AMF constellation leaves it, with a
    // modifier reaching beyond it.
    add_box(object, {20., 10., 5.}, {100., 50., 0.}, as_reader);
    TriangleMesh modifier = make_cube(40., 10., 10.);
    modifier.translate(90.f, 50.f, 0.f);
    object->add_volume(std::move(modifier), ModelVolumeType::PARAMETER_MODIFIER, false);
    object->add_instance()->set_offset({5., 7., 0.});
    const std::vector<Vec3d> before = world_vertices(model);

    model.rotate_and_center_objects(0.);

    CHECK_THAT(max_distance(world_vertices(model), before), Catch::Matchers::WithinAbs(0., 1e-4));
    CHECK_THAT(object->full_raw_mesh_bounding_box().center().norm(), Catch::Matchers::WithinAbs(0., 1e-4));
    // The instance compensates the centering, so there is nothing for merging or export to undo.
    CHECK_THAT(object->origin_translation.norm(), Catch::Matchers::WithinAbs(0., 1e-9));
}

TEST_CASE("Turning placed objects of different heights keeps them on the bed", "[Model]")
{
    const double angle  = GENERATE(0., 0.25 * PI);
    const double raised = GENERATE(0., 1.);
    CAPTURE(angle, raised);

    Model model;
    ModelObject *low  = model.add_object();
    ModelObject *flat = model.add_object();
    add_box(low, {20., 10., 5.}, {0., 0., 0.});
    add_box(flat, {30., 30., 2.}, {0., 0., raised});
    low->add_instance()->set_offset({110., 110., 0.});
    flat->add_instance()->set_offset({150., 125., 0.});
    // Fill the bounding box caches first, so a stale cache would show.
    REQUIRE_THAT(low->min_z(), Catch::Matchers::WithinAbs(0., 1e-6));
    REQUIRE_THAT(flat->min_z(), Catch::Matchers::WithinAbs(raised, 1e-6));

    model.rotate_and_center_objects(angle);

    CHECK_THAT(low->min_z(), Catch::Matchers::WithinAbs(0., 1e-4));
    CHECK_THAT(flat->min_z(), Catch::Matchers::WithinAbs(raised, 1e-4));
    CHECK(model.looks_like_multipart_object() == (raised != 0.));
}

TEST_CASE("Turning placed objects turns their whole layout around its center", "[Model]")
{
    Model model;
    ModelObject *bracket = model.add_object();
    ModelObject *plate   = model.add_object();
    add_box(bracket, {20., 10., 5.}, {0., 0., 0.});
    // A part at an offset inside its object turns with it.
    add_box(bracket, {4., 4., 8.}, {25., 0., 0.});
    add_box(plate, {30., 30., 2.}, {40., 40., 0.});
    bracket->add_instance()->set_offset({110., 110., 0.});
    bracket->add_instance()->set_offset({110., 140., 0.});
    // A turned and scaled copy still takes the rotation in its volumes.
    ModelInstance *plate_instance = plate->add_instance();
    plate_instance->set_offset({150., 125., 0.});
    plate_instance->set_rotation({0., 0., 0.3});
    plate_instance->set_scaling_factor({1.5, 1.5, 1.5});
    // A modifier reaching far to one side turns with its object but does not move the layout's center.
    TriangleMesh modifier = make_cube(60., 5., 5.);
    modifier.translate(200.f, 0.f, 0.f);
    bracket->add_volume(std::move(modifier), ModelVolumeType::PARAMETER_MODIFIER, false);
    // An object with nothing in it takes no part in the layout.
    model.add_object()->add_instance();
    // Fill the bounding box caches first, so a stale cache would show.
    for (const ModelObject *object : model.objects) {
        object->bounding_box_exact();
        object->raw_mesh_bounding_box();
    }

    const double                angle        = 0.5 * PI;
    const std::vector<Vec3d>    before       = world_vertices(model);
    const Vec2d                 center       = layout_center(world_vertices(model, true));
    const std::vector<Matrix3d> plate_before = linear_parts(*plate);
    // The modifier must reach outside the parts, or counting it would not move the center.
    REQUIRE((layout_center(before) - center).norm() > 1.);

    model.rotate_and_center_objects(angle);

    CHECK_THAT(max_distance(world_vertices(model), turned(before, angle, center)), Catch::Matchers::WithinAbs(0., 1e-3));
    // Instances that commute with the turn keep their own rotation and scale; the volumes take the turn.
    const Matrix3d              turn        = Geometry::rotation_transform(angle * Vec3d::UnitZ()).linear();
    const std::vector<Matrix3d> plate_after = linear_parts(*plate);
    CHECK(plate_after[0].isApprox(turn * plate_before[0], 1e-9));
    CHECK(plate_after[1].isApprox(plate_before[1], 1e-9));
    // The caches follow the change.
    for (const ModelObject *object : model.objects) {
        if (object->volumes.empty())
            continue;
        BoundingBoxf3 raw;
        for (const ModelVolume *volume : object->volumes)
            if (volume->is_model_part())
                raw.merge(volume->mesh().transformed_bounding_box(volume->get_matrix()));
        CHECK_THAT(box_distance(object->raw_mesh_bounding_box(), raw), Catch::Matchers::WithinAbs(0., 1e-4));
        BoundingBoxf3 exact;
        for (size_t i = 0; i < object->instances.size(); ++i)
            exact.merge(object->instance_bounding_box(i));
        CHECK_THAT(box_distance(object->bounding_box_exact(), exact), Catch::Matchers::WithinAbs(0., 1e-4));
    }
}

TEST_CASE("Turning keeps mirrored, tilted and skewed instances in the turned layout", "[Model]")
{
    const int case_idx = GENERATE(0, 1, 2, 3, 4);
    CAPTURE(case_idx);
    Transform3d linear = Transform3d::Identity();
    switch (case_idx) {
    case 0: linear.linear() = Vec3d(-1., 1., 1.).asDiagonal(); break;                            // mirror X
    case 1: linear.linear() = Vec3d(-1., -1., 1.).asDiagonal(); break;                           // both horizontal mirrors
    case 2: linear.linear() = Eigen::AngleAxisd(0.5, Vec3d::UnitX()).toRotationMatrix(); break; // tilt about X
    case 3: linear.linear() << 1., 0.3, 0., 0., 1., 0., 0., 0., 1.; break;                      // skew in XY
    default: linear.linear() = Vec3d(2., 1., 1.).asDiagonal(); break;                            // uneven XY scale
    }

    Model model;
    ModelObject *object = model.add_object();
    add_box(object, {20., 10., 5.}, {100., 0., 0.});
    add_box(object, {4., 4., 8.}, {125., 0., 0.});
    // One plain copy and one transformed copy of the same object: both must stay right.
    object->add_instance()->set_offset({60., 60., 0.});
    ModelInstance *transformed = object->add_instance();
    transformed->set_transformation(Geometry::Transformation(Geometry::translation_transform({160., 90., 0.}) * linear));

    const double                angle         = -0.25 * PI;
    const std::vector<Vec3d>    before        = world_vertices(model);
    const Vec2d                 center        = layout_center(before);
    const std::vector<Matrix3d> linear_before = linear_parts(*object);

    model.rotate_and_center_objects(angle);

    CHECK_THAT(max_distance(world_vertices(model), turned(before, angle, center)), Catch::Matchers::WithinAbs(0., 1e-3));
    // Both horizontal mirrors commute with the turn, so the volumes take it; otherwise every instance of the object
    // takes it and the shared volumes keep theirs.
    const Matrix3d              turn         = Geometry::rotation_transform(angle * Vec3d::UnitZ()).linear();
    const std::vector<Matrix3d> linear_after = linear_parts(*object);
    const size_t                volumes      = object->volumes.size();
    for (size_t i = 0; i < linear_after.size(); ++i) {
        CAPTURE(i);
        const bool turned_here = (i < volumes) == (case_idx == 1);
        CHECK(linear_after[i].isApprox(turned_here ? Matrix3d(turn * linear_before[i]) : linear_before[i], 1e-9));
    }
}

TEST_CASE("Merging turned objects into one keeps every part where it stands", "[Model]")
{
    Model model;
    ModelObject *bracket = model.add_object();
    ModelObject *plate   = model.add_object();
    add_box(bracket, {20., 10., 5.}, {100., 50., 0.});
    add_box(plate, {30., 30., 2.}, {-40., 10., 3.});
    bracket->add_instance()->set_offset({10., 20., 0.});
    plate->add_instance()->set_offset({60., 20., 0.});

    model.rotate_and_center_objects(0.25 * PI);
    const std::vector<Vec3d> standing = world_vertices(model);
    model.convert_multipart_object(1);

    // The merged object has no instance yet: its parts are in world coordinates.
    REQUIRE(model.objects.size() == 1);
    std::vector<Vec3d> merged;
    for (const ModelVolume *volume : model.objects.front()->volumes)
        for (const Vec3f &p : volume->mesh().its.vertices)
            merged.push_back(volume->get_matrix() * p.cast<double>());
    CHECK_THAT(max_distance(merged, standing), Catch::Matchers::WithinAbs(0., 1e-3));
}

TEST_CASE("Unplaced objects turn around the file origin and keep their height", "[Model]")
{
    Model model;
    ModelObject *bracket = model.add_object();
    ModelObject *plate   = model.add_object();
    add_box(bracket, {20., 10., 5.}, {100., 50., 0.});
    add_box(plate, {30., 30., 2.}, {-40., 10., 3.});
    // Unplaced objects lie in the file's coordinates; origin_translation records how far they were centered.
    auto file_points = [&model]() {
        std::vector<Vec3d> points;
        for (const ModelObject *object : model.objects)
            for (const ModelVolume *volume : object->volumes)
                for (const Vec3f &p : volume->mesh().its.vertices)
                    points.push_back(volume->get_matrix() * p.cast<double>() - object->origin_translation);
        return points;
    };
    const double             angle  = 0.5 * PI;
    const std::vector<Vec3d> before = file_points();

    model.rotate_and_center_objects(angle);

    CHECK_THAT(max_distance(file_points(), turned(before, angle, Vec2d::Zero())), Catch::Matchers::WithinAbs(0., 1e-3));
    for (const ModelObject *object : model.objects) {
        const BoundingBoxf3 bb = object->full_raw_mesh_bounding_box();
        CHECK_THAT(bb.center().x(), Catch::Matchers::WithinAbs(0., 1e-4));
        CHECK_THAT(bb.center().y(), Catch::Matchers::WithinAbs(0., 1e-4));
        CHECK_THAT(object->origin_translation.z(), Catch::Matchers::WithinAbs(0., 1e-9));
    }
    CHECK_THAT(bracket->full_raw_mesh_bounding_box().min.z(), Catch::Matchers::WithinAbs(0., 1e-4));
    CHECK_THAT(plate->full_raw_mesh_bounding_box().min.z(), Catch::Matchers::WithinAbs(3., 1e-4));
}

TEST_CASE("Unplaced objects scaled to millimetres still merge on a common base", "[Model]")
{
    Model model;
    ModelObject *tall = model.add_object();
    ModelObject *flat = model.add_object();
    add_box(tall, {2., 1., 0.5}, {0., 0., 0.});
    add_box(flat, {3., 3., 0.2}, {5., 0., 0.});

    model.rotate_and_center_objects(0.);
    // The rest of the import as it runs for several files loaded as one object.
    model.convert_from_imperial_units(false);
    for (ModelObject *object : model.objects)
        object->center_around_origin(false);
    model.convert_multipart_object(1);

    REQUIRE(model.objects.size() == 1);
    REQUIRE(model.objects.front()->volumes.size() == 2);
    std::vector<double> bases;
    for (const ModelVolume *volume : model.objects.front()->volumes)
        bases.push_back(volume->mesh().transformed_bounding_box(volume->get_matrix()).min.z());
    CHECK_THAT(bases.front(), Catch::Matchers::WithinAbs(bases.back(), 1e-3));
}

TEST_CASE("Converting a part's units keeps its material", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    volume->set_material_id("mat_a");

    ModelObjectPtrs converted;
    object->convert_units(converted, ConversionType::CONV_FROM_INCH, {});

    REQUIRE(converted.size() == 1);
    REQUIRE(converted.front()->volumes.size() == 1);
    CHECK(converted.front()->volumes.front()->material_id() == "mat_a");

    // convert_units() hands its objects to the caller unowned; give them to a model so they are freed.
    Model owner;
    owner.objects = std::move(converted);
}

TEST_CASE("Converting a part's units keeps an id whose material is missing", "[Model]")
{
    Model model;
    ModelObject *object = model.add_object();
    ModelVolume *volume = object->add_volume(make_cube(10, 10, 10), ModelVolumeType::MODEL_PART, false);
    volume->set_material_id("mat_a");
    model.delete_material("mat_a");

    ModelObjectPtrs converted;
    object->convert_units(converted, ConversionType::CONV_FROM_INCH, {});

    REQUIRE(converted.size() == 1);
    REQUIRE(converted.front()->volumes.size() == 1);
    CHECK(converted.front()->volumes.front()->material_id() == "mat_a");

    // convert_units() hands its objects to the caller unowned; give them to a model so they are freed.
    Model owner;
    owner.objects = std::move(converted);
}
