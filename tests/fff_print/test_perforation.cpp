#include <catch2/catch_all.hpp>

#include "libslic3r/ClipperUtils.hpp"
#include "libslic3r/Layer.hpp"
#include "libslic3r/Model.hpp"
#include "libslic3r/PerforationPattern.hpp"
#include "libslic3r/Print.hpp"
#include "libslic3r/TriangleMesh.hpp"

#include "test_helpers.hpp"

#include <algorithm>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::Test;

namespace {

// A 30x30x20 hollow box: a 30mm shell with a 26mm cavity, open at the top, walls 2mm thick.
ModelObject *hollow_box(Model &model, bool perforate)
{
    ModelObject *object = model.add_object();
    object->name = "hollow_box.stl";
    object->add_volume(make_cube(30., 30., 20.), ModelVolumeType::MODEL_PART, false);

    TriangleMesh cavity = make_cube(26., 26., 20.);
    cavity.translate(2.f, 2.f, 2.f); // leaves 2mm walls and floor, open top
    object->add_volume(std::move(cavity), ModelVolumeType::NEGATIVE_VOLUME, false);

    if (perforate) {
        // A honeycomb stamped through the -X wall (x = 0), spanning the wall thickness and a
        // little beyond on both sides so the cut is unambiguous.
        ExPolygon face;
        face.contour.points = {Point::new_scale(2., 2.), Point::new_scale(28., 2.),
                               Point::new_scale(28., 18.), Point::new_scale(2., 18.)};

        PerforationParams params;
        params.kind    = PerforationKind::Honeycomb;
        params.spacing = 6.;
        params.width   = 3.;
        params.margin  = 3.;

        const ExPolygons pattern = make_perforation_pattern(ExPolygons{face}, params);
        REQUIRE(!pattern.empty());

        // Same mapping the gizmo uses: plane origin at the wall middle, u = +Y, v = +Z,
        // n = -X (outward), so a point at (u, v, z) is (0.5 - z, u, v) in world space.
        const indexed_triangle_set prism = extrude_perforation_pattern(
            pattern, Vec3d(0.5, 0., 0.), Vec3d(0., 1., 0.), Vec3d(0., 0., 1.), Vec3d(-1., 0., 0.), -2., 2.);
        REQUIRE(!prism.indices.empty());
        object->add_volume(TriangleMesh(prism), ModelVolumeType::NEGATIVE_VOLUME, false);
    }

    object->add_instance();
    object->ensure_on_bed();
    return object;
}

Print &sliced(Model &model, Print &print, const DynamicPrintConfig &config, bool perforate)
{
    hollow_box(model, perforate);
    print.auto_assign_extruders(model.objects.front());
    print.apply(model, config);
    print.validate();
    print.set_status_silent();
    print.process();
    return print;
}

// Wall loops at a given Z, summed over regions.
int loops_at_z(const Print &print, double z)
{
    int count = 0;
    for (const Layer *layer : print.objects().front()->layers())
        if (std::abs(layer->print_z - z) < 0.51 * layer->height)
            for (const LayerRegion *region : layer->regions())
                count += int(region->perimeters.entities.size());
    return count;
}

DynamicPrintConfig perforation_config()
{
    DynamicPrintConfig config = DynamicPrintConfig::full_print_config();
    config.set_deserialize_strict({
        {"layer_height", 0.2},
        {"initial_layer_print_height", 0.2},
        {"wall_loops", 2},
        {"sparse_infill_density", "0%"},
        {"top_shell_layers", 0},
        {"bottom_shell_layers", 2},
    });
    return config;
}

} // namespace

TEST_CASE("A honeycomb negative volume cuts holes through a hollow box wall", "[Perforation]")
{
    const DynamicPrintConfig config = perforation_config();

    // Slice the same box with and without the perforation. The holes add wall loops to every
    // affected layer, so a mid-height layer must carry more perimeters perforated.
    Model  plain_model;
    Print  plain_print;
    sliced(plain_model, plain_print, config, false);

    Model  cut_model;
    Print  cut_print;
    sliced(cut_model, cut_print, config, true);

    const double z = 10.0; // well inside the perforated band
    REQUIRE(loops_at_z(plain_print, z) > 0);
    REQUIRE(loops_at_z(cut_print, z) > loops_at_z(plain_print, z));
}
