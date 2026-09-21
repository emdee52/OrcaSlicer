#include <catch2/catch_all.hpp>

#include "libslic3r/CutUtils.hpp"
#include "libslic3r/Geometry.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/TriangleMesh.hpp"
#include "libslic3r/TriangleSelector.hpp"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

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

namespace {

// A cube centered on `center`, spanning +/-10mm, cut by a cylinder of the given diameter.
ModelObject *make_centered_cube(Model &model, double size)
{
    ModelObject *obj = model.add_object();
    obj->add_instance();
    const double half = size / 2.;
    TriangleMesh cube(its_make_cube(size, size, size));
    its_translate(cube.its, Vec3f(float(-half), float(-half), float(-half)));
    obj->add_volume(std::move(cube), ModelVolumeType::MODEL_PART, false);
    return obj;
}

double volume_of(const ModelVolume *v) { return double(its_volume(v->mesh().its)); }

} // namespace

TEST_CASE("Cookie-cutter prisms are closed and centered on the plane", "[CutUtils]")
{
    for (const CutShapeKind kind : { CutShapeKind::Circle, CutShapeKind::Square, CutShapeKind::Hexagon }) {
        TriangleMesh cutter(make_cookie_cutter(kind, 10., 7.5));

        CHECK(its_num_open_edges(cutter.its) == 0);
        CHECK(its_volume(cutter.its) > 0.f);

        // Extruded symmetrically about the cut plane and centered on its axis.
        const BoundingBoxf3 bb = cutter.bounding_box();
        CHECK_THAT(bb.min.z(), WithinAbs(-7.5, 1e-4));
        CHECK_THAT(bb.max.z(), WithinAbs(7.5, 1e-4));
        CHECK_THAT(bb.min.x() + bb.max.x(), WithinAbs(0., 1e-4));
        CHECK_THAT(bb.min.y() + bb.max.y(), WithinAbs(0., 1e-4));
    }
}

TEST_CASE("A shaped cut splits a cube into an inside plug and the outside body", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_centered_cube(model, 20.);

    const double cut_height = 20.;
    TriangleMesh cutter(make_cookie_cutter(CutShapeKind::Circle, 10., cut_height));

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, Transform3d::Identity(), attrs);
    const ModelObjectPtrs         &results = cut.perform_with_shape(cutter);

    REQUIRE(results.size() == 1);
    const ModelObject *result = results.front();
    REQUIRE(result->volumes.size() == 2);

    double inside = 0., outside = 0.;
    for (const ModelVolume *v : result->volumes) {
        CHECK(its_num_open_edges(v->mesh().its) == 0);
        const double vol = volume_of(v);
        if (vol < 0.5 * 20. * 20. * 20.)
            inside = vol;
        else
            outside = vol;
    }

    // The two pieces tile the original cube.
    CHECK_THAT(inside + outside, WithinRel(8000., 1e-3));
    // The plug is a 10mm cylinder through the 20mm cube (a 64-gon prism, slightly under pi*r^2).
    CHECK_THAT(inside, WithinRel(1568., 1e-2));
}

TEST_CASE("A shaped cut of a selected volume leaves the other parts untouched", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_two_part_object(model);
    REQUIRE(obj->volumes.size() == 2);

    const BoundingBoxf3 untouched_before = volume_world_box(obj, obj->volumes[1]);

    // Shape centered on the first 10mm cube (which spans [0,10]^3).
    TriangleMesh cutter(make_cookie_cutter(CutShapeKind::Circle, 4., 20.));

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(5., 5., 5.)), attrs, { 0 });
    const ModelObjectPtrs         &results = cut.perform_with_shape(cutter);

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

TEST_CASE("A shaped cut bails out when the shape misses the object", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = model.add_object();
    obj->add_instance();
    obj->add_volume(TriangleMesh(its_make_cube(10., 10., 10.)), ModelVolumeType::MODEL_PART, false);

    // Cut plane far away from the part, so the prism never intersects it and the inside piece
    // would come back empty.
    TriangleMesh cutter(make_cookie_cutter(CutShapeKind::Circle, 4., 20.));

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(100., 100., 5.)), attrs);
    const ModelObjectPtrs         &results = cut.perform_with_shape(cutter);

    // The failed split must not produce a partial result.
    CHECK(results.empty());
}

TEST_CASE("A shaped cut that misses one part still cuts the others", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_two_part_object(model);
    REQUIRE(obj->volumes.size() == 2);

    const BoundingBoxf3 untouched_before = volume_world_box(obj, obj->volumes[1]);

    // Shape centered on the first 10mm cube only; the second cube at x in [20,30] is missed.
    TriangleMesh cutter(make_cookie_cutter(CutShapeKind::Circle, 4., 20.));

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(5., 5., 5.)), attrs);
    const ModelObjectPtrs         &results = cut.perform_with_shape(cutter);

    REQUIRE(results.size() == 1);
    const ModelObject *result = results.front();
    // Volume 0 is split into two, volume 1 (missed by the shape) is carried over whole.
    REQUIRE(result->volumes.size() == 3);

    bool found_untouched = false;
    for (const ModelVolume *v : result->volumes) {
        const BoundingBoxf3 bb = volume_world_box(result, v);
        if (bb.min.isApprox(untouched_before.min, 1e-3) && bb.max.isApprox(untouched_before.max, 1e-3))
            found_untouched = true;
    }
    CHECK(found_untouched);
}

TEST_CASE("A depth-limited shaped cut removes only a pocket", "[CutUtils]")
{
    Model        model;
    ModelObject *obj = make_centered_cube(model, 20.);

    const double radius = 5.;
    const double depth  = 6.;
    const double margin = 1.;
    // Blind pocket on the top face: the plane sits on the top of the cube (z = 10) and the prism
    // reaches `depth` into the object while a small margin stays outside, so the cut opens at the
    // surface instead of leaving a cavity inside.
    TriangleMesh cutter(make_cookie_cutter(CutShapeKind::Circle, 2. * radius, -depth, margin));

    const ModelObjectCutAttributes attrs = ModelObjectCutAttribute::KeepUpper | ModelObjectCutAttribute::KeepLower | ModelObjectCutAttribute::KeepAsParts;
    Cut                            cut(obj, 0, translation_transform(Vec3d(0., 0., 10.)), attrs);
    const ModelObjectPtrs         &results = cut.perform_with_shape(cutter);

    REQUIRE(results.size() == 1);
    const ModelObject *result = results.front();
    REQUIRE(result->volumes.size() == 2);

    const double v0      = volume_of(result->volumes[0]);
    const double v1      = volume_of(result->volumes[1]);
    const double inside  = std::min(v0, v1);
    const double outside = std::max(v0, v1);

    // The pocket is only a fraction of the 20mm cube, but the two pieces still tile it.
    const double polygon_area = 0.5 * 64. * radius * radius * std::sin(2. * PI / 64.);
    CHECK_THAT(inside, WithinRel(polygon_area * depth, 0.02));
    CHECK_THAT(inside + outside, WithinRel(8000., 1e-3));
}

TEST_CASE("Face snap points cover the corners, edge midpoints and centre of a square face", "[CutUtils]")
{
    indexed_triangle_set square;
    square.vertices = { Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(10.f, 10.f, 0.f), Vec3f(0.f, 10.f, 0.f) };
    square.indices  = { Vec3i32(0, 1, 2), Vec3i32(0, 2, 3) };

    const std::vector<FaceSnapPoint> pts = face_snap_points(square, { 0, 1 });
    REQUIRE(pts.size() == 9);

    const auto count_of = [&pts](FaceSnapKind kind) {
        return std::count_if(pts.begin(), pts.end(), [kind](const FaceSnapPoint &p) { return p.kind == kind; });
    };
    CHECK(count_of(FaceSnapKind::Corner) == 4);
    CHECK(count_of(FaceSnapKind::EdgeMid) == 4);
    CHECK(count_of(FaceSnapKind::FaceCenter) == 1);

    const auto has_point = [&pts](const Vec3d &expected) {
        return std::any_of(pts.begin(), pts.end(),
                           [&expected](const FaceSnapPoint &p) { return (p.pos - expected).norm() < 1e-9; });
    };
    CHECK(has_point(Vec3d(0., 0., 0.)));
    CHECK(has_point(Vec3d(10., 0., 0.)));
    CHECK(has_point(Vec3d(10., 10., 0.)));
    CHECK(has_point(Vec3d(0., 10., 0.)));
    CHECK(has_point(Vec3d(5., 0., 0.)));
    CHECK(has_point(Vec3d(10., 5., 0.)));
    CHECK(has_point(Vec3d(5., 10., 0.)));
    CHECK(has_point(Vec3d(0., 5., 0.)));
    CHECK(has_point(Vec3d(5., 5., 0.)));

    CHECK(pts.front().kind == FaceSnapKind::Corner);
    REQUIRE(pts.back().kind == FaceSnapKind::FaceCenter);
    CHECK_THAT(pts.back().pos.x(), WithinAbs(5.0, 1e-9));
    CHECK_THAT(pts.back().pos.y(), WithinAbs(5.0, 1e-9));
    CHECK_THAT(pts.back().pos.z(), WithinAbs(0.0, 1e-9));
}

TEST_CASE("Face snap points are empty for a closed surface", "[CutUtils]")
{
    const indexed_triangle_set cube = its_make_cube(10., 10., 10.);
    std::vector<int>         all(cube.indices.size());
    std::iota(all.begin(), all.end(), 0);

    CHECK(face_snap_points(cube, all).empty());
}

TEST_CASE("Nearest face snap picks the closest candidate within tolerance", "[CutUtils]")
{
    indexed_triangle_set square;
    square.vertices = { Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(10.f, 10.f, 0.f), Vec3f(0.f, 10.f, 0.f) };
    square.indices  = { Vec3i32(0, 1, 2), Vec3i32(0, 2, 3) };

    const std::vector<FaceSnapPoint> pts     = face_snap_points(square, { 0, 1 });
    const auto                       project = [](const Vec3d &p) { return Vec2d(p.x(), p.y()); };
    FaceSnapPoint                    best;

    REQUIRE(nearest_face_snap(pts, project, Vec2d(4.6, 5.2), best));
    CHECK(best.kind == FaceSnapKind::FaceCenter);

    REQUIRE(nearest_face_snap(pts, project, Vec2d(0.5, 0.5), best));
    CHECK(best.kind == FaceSnapKind::Corner);
    CHECK_THAT(best.pos.norm(), WithinAbs(0.0, 1e-9));

    REQUIRE(nearest_face_snap(pts, project, Vec2d(5.2, 0.4), best));
    CHECK(best.kind == FaceSnapKind::EdgeMid);
    CHECK_THAT((best.pos - Vec3d(5., 0., 0.)).norm(), WithinAbs(0.0, 1e-9));

    CHECK(!nearest_face_snap(pts, project, Vec2d(30., 30.), best));

    // Equidistant from a corner, two edge midpoints and the centre: the first kind emitted wins.
    REQUIRE(nearest_face_snap(pts, project, Vec2d(2.5, 2.5), best));
    CHECK(best.kind == FaceSnapKind::Corner);
}

TEST_CASE("Face snap tolerance grows with the spacing of the candidates and stays inside its bounds", "[CutUtils]")
{
    const auto project = [](const Vec3d &p) { return Vec2d(p.x(), p.y()); };

    // A face scaled by `side / 10` has its candidates `side / 2` pixels apart, so the stick distance
    // is half that gap, bounded by the floor and the cap.
    const auto tolerance_for = [&project](double side) {
        const double u = side / 10.;
        indexed_triangle_set square;
        square.vertices = { Vec3f(0.f, 0.f, 0.f), Vec3f(float(10. * u), 0.f, 0.f),
                            Vec3f(float(10. * u), float(10. * u), 0.f), Vec3f(0.f, float(10. * u), 0.f) };
        square.indices  = { Vec3i32(0, 1, 2), Vec3i32(0, 2, 3) };

        FaceSnapPoint best;
        double        tolerance = 0.0;
        REQUIRE(nearest_face_snap(face_snap_points(square, { 0, 1 }), project, Vec2d(5.2 * u, 0.4 * u), best, 8., 48.,
                                 &tolerance));
        CHECK(best.kind == FaceSnapKind::EdgeMid);
        return tolerance;
    };

    const double small = tolerance_for(10.);
    const double mid   = tolerance_for(100.);
    const double big   = tolerance_for(1000.);

    CHECK_THAT(small, WithinAbs(8.0, 1e-6)); // the gap is below the floor
    CHECK(mid > small);                      // grows with the face
    CHECK(mid < big);
    CHECK_THAT(big, WithinAbs(48.0, 1e-6));  // and is capped

    // Half the distance from the cursor to the next candidate over.
    CHECK_THAT(mid, WithinAbs(0.5 * (Vec2d(50., 50.) - Vec2d(52., 4.)).norm(), 1e-6));
}

TEST_CASE("A candidate beyond the stick distance is not snapped", "[CutUtils]")
{
    indexed_triangle_set square;
    square.vertices = { Vec3f(0.f, 0.f, 0.f), Vec3f(10.f, 0.f, 0.f), Vec3f(10.f, 10.f, 0.f), Vec3f(0.f, 10.f, 0.f) };
    square.indices  = { Vec3i32(0, 1, 2), Vec3i32(0, 2, 3) };
    const std::vector<FaceSnapPoint> pts     = face_snap_points(square, { 0, 1 });
    const auto                       project = [](const Vec3d &p) { return Vec2d(p.x(), p.y()); };
    FaceSnapPoint                    best;

    // 0.45 px from the edge midpoint: inside the default floor, outside a tightened bound.
    CHECK(nearest_face_snap(pts, project, Vec2d(5.2, 0.4), best, 8., 48., nullptr));
    CHECK(!nearest_face_snap(pts, project, Vec2d(5.2, 0.4), best, 0.1, 0.1, nullptr));
}

