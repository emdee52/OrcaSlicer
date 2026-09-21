#include <catch2/catch_all.hpp>

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <cmath>

using namespace Slic3r;
using namespace Slic3r::Geometry;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

namespace {

indexed_triangle_set single_triangle(const Vec3d &a, const Vec3d &b, const Vec3d &c)
{
    indexed_triangle_set its;
    its.vertices = { a.cast<float>(), b.cast<float>(), c.cast<float>() };
    its.indices  = { Vec3i32(0, 1, 2) };
    return its;
}

Transform3d scaling(const Vec3d &s)
{
    Transform3d t = Transform3d::Identity();
    t.linear()    = s.asDiagonal();
    return t;
}

// A plane normal must be perpendicular to two edge vectors of the (transformed) triangle and
// have unit length. This is the defining property and catches a naive `linear * normal`
// transform, which only stays perpendicular under a similarity transform.
void check_perpendicular_unit(const indexed_triangle_set &its, const Vec3d &n, const Transform3d &trafo)
{
    REQUIRE(std::abs(n.norm() - 1.0) < 1e-9);
    const Vec3d p0 = trafo * its.vertices[0].cast<double>();
    const Vec3d p1 = trafo * its.vertices[1].cast<double>();
    const Vec3d p2 = trafo * its.vertices[2].cast<double>();
    CHECK_THAT(n.dot((p1 - p0).normalized()), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.dot((p2 - p0).normalized()), WithinAbs(0.0, 1e-9));
}

} // namespace

TEST_CASE("Facet normal is returned unchanged for the identity transform", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });

    const Vec3d n = facet_normal_in_world(its, 0, Transform3d::Identity());

    CHECK_THAT(n.x(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.y(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.z(), WithinAbs(1.0, 1e-9));
}

TEST_CASE("Facet normal follows a rotation", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });
    const Transform3d        trafo = rotation_transform(Vec3d(0., PI / 2., 0.)); // +Z -> +X

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    CHECK_THAT(n.x(), WithinAbs(1.0, 1e-9));
    CHECK_THAT(n.y(), WithinAbs(0.0, 1e-9));
    CHECK_THAT(n.z(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Facet normal stays perpendicular under a non-uniform scale", "[CutUtils]")
{
    // A slanted triangle: a naive `linear * normal` would no longer be perpendicular to the
    // transformed edges.
    const indexed_triangle_set its   = single_triangle({ 0., 0., 0. }, { 2., 1., 0. }, { 0.5, 0.5, 1. });
    const Transform3d        trafo   = scaling(Vec3d(1.0, 2.5, 0.4));

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    check_perpendicular_unit(its, n, trafo);

    // Exact expected value: inverse-transpose of the linear part.
    const Vec3d expected = (trafo.linear().inverse().transpose() * its_face_normal(its, 0).cast<double>()).normalized();
    CHECK_THAT(n.x(), WithinRel(expected.x(), 1e-9));
    CHECK_THAT(n.y(), WithinRel(expected.y(), 1e-9));
    CHECK_THAT(n.z(), WithinRel(expected.z(), 1e-9));
}

TEST_CASE("Facet normal stays perpendicular under a mirrored transform", "[CutUtils]")
{
    const indexed_triangle_set its   = single_triangle({ 0., 0., 0. }, { 2., 1., 0. }, { 0.5, 0.5, 1. });
    const Transform3d        trafo   = scaling(Vec3d(-1.0, 1.0, 1.0));

    const Vec3d n = facet_normal_in_world(its, 0, trafo);

    check_perpendicular_unit(its, n, trafo);
}

TEST_CASE("Out-of-range facet returns the fallback normal", "[CutUtils]")
{
    const indexed_triangle_set its = single_triangle({ 0., 0., 0. }, { 2., 0., 0. }, { 0., 3., 0. });

    CHECK(facet_normal_in_world(its, -1, Transform3d::Identity()).isApprox(Vec3d::UnitZ()));
    CHECK(facet_normal_in_world(its, 7, Transform3d::Identity()).isApprox(Vec3d::UnitZ()));
}

namespace {

// Two separate 10mm cubes: volume 0 spans [0,10]^3, volume 1 spans [20,30]x[0,10]x[0,10].
ModelObject *make_two_part_object(Model &model)
{
    ModelObject *obj = model.add_object();
    obj->add_instance();
    obj->add_volume(TriangleMesh(its_make_cube(10., 10., 10.)), false);

    TriangleMesh second(its_make_cube(10., 10., 10.));
    its_translate(second.its, Vec3f(20.f, 0.f, 0.f));
    obj->add_volume(std::move(second), ModelVolumeType::MODEL_PART, false);
    return obj;
}

BoundingBoxf3 volume_world_box(const ModelObject *obj, const ModelVolume *vol)
{
    return bounding_box(vol->mesh().its).transformed(obj->instances.front()->get_matrix() * vol->get_matrix());
}

} // namespace

TEST_CASE("Cutting selected volumes leaves the other parts untouched", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_two_part_object(model);
    REQUIRE(obj->volumes.size() == 2);

    const BoundingBoxf3 untouched_before = volume_world_box(obj, obj->volumes[1]);

    // Cut plane at z = 5; only volume 0 is in the filter.
    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(0., 0., 5.)), attrs, { 0 });
    const ModelObjectPtrs         &results = cut.perform_with_plane();

    REQUIRE(results.size() == 1);
    const ModelObject *result = results.front();
    // volume 0 split into two, volume 1 carried over whole.
    REQUIRE(result->volumes.size() == 3);

    bool found_untouched = false;
    for (const ModelVolume *v : result->volumes) {
        const BoundingBoxf3 bb = volume_world_box(result, v);
        if (bb.min.isApprox(untouched_before.min, 1e-3) && bb.max.isApprox(untouched_before.max, 1e-3))
            found_untouched = true;
    }
    CHECK(found_untouched);
}

TEST_CASE("Cutting without a volume filter still cuts every part", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_two_part_object(model);

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(0., 0., 5.)), attrs);
    const ModelObjectPtrs         &results = cut.perform_with_plane();

    REQUIRE(results.size() == 1);
    // Both cubes straddle z = 5, so each is split -> four pieces.
    CHECK(results.front()->volumes.size() == 4);
}

TEST_CASE("A volume-filtered cut keeps painting on the untouched parts", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_two_part_object(model);

    // Paint a facet of the part that will NOT be cut (volume 1). reset_extra_facets() wipes paint
    // on every volume during the cut, so this guards the KeepPaint remap for carried-over parts.
    {
        TriangleSelector selector(obj->volumes[1]->mesh());
        selector.set_facet(0, EnforcerBlockerType::ENFORCER);
        obj->volumes[1]->supported_facets.set_data(selector.serialize());
    }
    REQUIRE(obj->volumes[1]->is_fdm_support_painted());

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower |
                                           ModelObjectCutAttribute::KeepAsParts | ModelObjectCutAttribute::KeepPaint;
    Cut                    cut(obj, 0, translation_transform(Vec3d(0., 0., 5.)), attrs, { 0 });
    const ModelObjectPtrs &results = cut.perform_with_plane();

    REQUIRE(results.size() == 1);
    const ModelObject *result = results.front();

    int painted_parts = 0;
    for (const ModelVolume *v : result->volumes)
        if (v->is_fdm_support_painted())
            ++painted_parts;

    CHECK(painted_parts == 1);
}

