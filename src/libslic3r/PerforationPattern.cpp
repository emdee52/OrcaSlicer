#include "PerforationPattern.hpp"

#include "ClipperUtils.hpp"
#include "Tesselate.hpp"
#include "libslic3r.h"

#include <algorithm>
#include <cmath>

namespace Slic3r {

namespace {

constexpr double PI = 3.14159265358979323846;

// Guard against a huge mesh when spacing is tiny relative to the face.
constexpr int MAX_FEATURES = 20000;

ExPolygons rotate_copy(const ExPolygons &in, double angle)
{
    ExPolygons out = in;
    for (ExPolygon &p : out)
        p.rotate(angle);
    return out;
}

// One family of parallel slots at `theta`, clipped to `domain`.
ExPolygons line_family(const ExPolygons &domain, double theta, double spacing_mm, double width_mm)
{
    if (spacing_mm <= 0. || width_mm <= 0.)
        return {};

    // Work axis-aligned: rotate the domain by -theta, lay horizontal strips, rotate back.
    const ExPolygons rotated = rotate_copy(domain, -theta);
    const BoundingBox bbox    = get_extents(rotated);

    const coord_t w  = coord_t(scale_(width_mm));
    const coord_t sp = coord_t(scale_(spacing_mm));
    if (w <= 0 || sp <= 0)
        return {};

    const coord_t x0 = bbox.min.x() - sp;
    const coord_t x1 = bbox.max.x() + sp;
    const coord_t y0 = bbox.min.y();
    const coord_t y1 = bbox.max.y();

    Polygons strips;
    int guard = 0;
    for (coord_t y = y0 + sp / 2; y <= y1 && guard < MAX_FEATURES; y += sp, ++guard) {
        Polygon r;
        r.points = {Point(x0, y - w / 2), Point(x1, y - w / 2), Point(x1, y + w / 2), Point(x0, y + w / 2)};
        strips.emplace_back(std::move(r));
    }
    if (strips.empty())
        return {};

    const ExPolygons slots = rotate_copy(union_ex(strips), theta);
    return intersection_ex(domain, slots);
}

// Pointy-top regular hexagon of circumradius `radius` (scaled units) centred at (cx, cy).
Polygon make_hexagon(coord_t cx, coord_t cy, double radius)
{
    Polygon p;
    p.points.reserve(6);
    for (int k = 0; k < 6; ++k) {
        const double a = (30. + 60. * k) * PI / 180.;
        p.points.emplace_back(cx + coord_t(std::lround(radius * std::cos(a))),
                              cy + coord_t(std::lround(radius * std::sin(a))));
    }
    return p;
}

ExPolygons honeycomb(const ExPolygons &domain, double spacing_mm, double width_mm)
{
    // Across-flats of the hole equals `width`, so the wall between holes is spacing - width.
    const double r  = scale_(width_mm) / std::sqrt(3.);
    const double dx = scale_(spacing_mm);
    const double dy = dx * std::sqrt(3.) / 2.;
    if (r <= 0. || dx <= 0. || dy <= 0.)
        return {};

    const BoundingBox bbox = get_extents(domain);
    const double    x0     = bbox.min.x() - dx;
    const double    x1     = bbox.max.x() + dx;
    const double    y0     = bbox.min.y() - dy;
    const double    y1     = bbox.max.y() + dy;

    Polygons cells;
    int      guard = 0;
    int      row   = 0;
    for (double y = y0; y <= y1 && guard < MAX_FEATURES; y += dy, ++row) {
        const double xoff = (row & 1) ? dx * 0.5 : 0.;
        for (double x = x0 + xoff; x <= x1 && guard < MAX_FEATURES; x += dx, ++guard)
            cells.emplace_back(make_hexagon(coord_t(std::lround(x)), coord_t(std::lround(y)), r));
    }
    if (cells.empty())
        return {};

    return intersection_ex(domain, union_ex(cells));
}

// Round holes of diameter `width` on a staggered grid. The nearest neighbour distance is
// `spacing` (horizontal) and the row offset is spacing/2, so the wall is spacing - width.
ExPolygons circles(const ExPolygons &domain, double spacing_mm, double width_mm)
{
    const double r  = scale_(width_mm) / 2.;
    const double dx = scale_(spacing_mm);
    const double dy = dx * std::sqrt(3.) / 2.;
    if (r <= 0. || dx <= 0. || dy <= 0.)
        return {};

    const BoundingBox bbox = get_extents(domain);
    const double    x0     = bbox.min.x() - dx;
    const double    x1     = bbox.max.x() + dx;
    const double    y0     = bbox.min.y() - dy;
    const double    y1     = bbox.max.y() + dy;

    Polygons cells;
    int      guard = 0;
    int      row   = 0;
    for (double y = y0; y <= y1 && guard < MAX_FEATURES; y += dy, ++row) {
        const double xoff = (row & 1) ? dx * 0.5 : 0.;
        for (double x = x0 + xoff; x <= x1 && guard < MAX_FEATURES; x += dx, ++guard) {
            Polygon c;
            const int seg = 24;
            c.points.reserve(seg);
            for (int k = 0; k < seg; ++k) {
                const double a = 2. * PI * k / seg;
                c.points.emplace_back(coord_t(std::lround(x + r * std::cos(a))),
                                      coord_t(std::lround(y + r * std::sin(a))));
            }
            cells.emplace_back(std::move(c));
        }
    }
    if (cells.empty())
        return {};

    return intersection_ex(domain, union_ex(cells));
}

} // namespace

ExPolygons make_perforation_pattern(const ExPolygons &domain, const PerforationParams &params)
{
    if (domain.empty())
        return {};

    const double   margin = std::max(0.0, params.margin);
    const ExPolygons inset = margin > 0. ? offset_ex(domain, -scale_(margin)) : domain;
    if (inset.empty())
        return {};

    const double spacing = std::max(params.spacing, 0.2);
    // Width is used exactly as given. If it leaves less than PERFORATION_MIN_WALL between holes
    // the holes merge; that is the caller's choice and the UI warns, but the builder never
    // silently changes the value.
    const double width   = std::max(params.width, 0.01);
    const double theta   = params.angle_deg * PI / 180.;

    ExPolygons holes;
    switch (params.kind) {
    case PerforationKind::Lines:
        holes = line_family(inset, theta, spacing, width);
        break;
    case PerforationKind::Grid:
        // Two orthogonal families: '+' at angle 0, 'x' at angle 45.
        holes = union_ex(line_family(inset, theta, spacing, width),
                         line_family(inset, theta + PI / 2., spacing, width));
        break;
    case PerforationKind::Honeycomb:
        holes = honeycomb(inset, spacing, width);
        break;
    case PerforationKind::Circles:
        holes = circles(inset, spacing, width);
        break;
    }

    if (holes.empty())
        return {};

    return params.invert ? diff_ex(inset, holes) : holes;
}

indexed_triangle_set extrude_perforation_pattern(const ExPolygons &polys, const Vec3d &origin,
                                                 const Vec3d &ux, const Vec3d &uy, const Vec3d &n,
                                                 double z0, double z1)
{
    indexed_triangle_set its;
    if (polys.empty() || z1 <= z0)
        return its;

    const auto place = [&](double x, double y, double z) -> stl_vertex {
        return (origin + x * ux + y * uy + z * n).cast<float>();
    };

    // Caps. triangulate_expolygons_3d works in unscaled (mm) coordinates; its winding
    // follows the `flip` flag, so the bottom faces -n and the top faces +n.
    const auto add_cap = [&](double z, bool flip) {
        const std::vector<Vec3d> tri = triangulate_expolygons_3d(polys, z, flip);
        for (size_t i = 0; i + 2 < tri.size(); i += 3) {
            const uint32_t b = uint32_t(its.vertices.size());
            its.vertices.push_back(place(tri[i + 0].x(), tri[i + 0].y(), z));
            its.vertices.push_back(place(tri[i + 1].x(), tri[i + 1].y(), z));
            its.vertices.push_back(place(tri[i + 2].x(), tri[i + 2].y(), z));
            its.indices.emplace_back(b, b + 1, b + 2);
        }
    };
    add_cap(z0, NORMALS_DOWN);
    add_cap(z1, NORMALS_UP);

    // Side walls, one quad strip per contour. The winding matches the caps for a CCW outer
    // contour and CW holes, which is what ExPolygon guarantees.
    const auto add_walls = [&](const Polygon &poly) {
        const size_t o = poly.points.size();
        if (o < 3)
            return;
        const uint32_t b = uint32_t(its.vertices.size());
        for (const Point &p : poly.points)
            its.vertices.push_back(place(SCALING_FACTOR * p.x(), SCALING_FACTOR * p.y(), z0));
        for (const Point &p : poly.points)
            its.vertices.push_back(place(SCALING_FACTOR * p.x(), SCALING_FACTOR * p.y(), z1));
        for (uint32_t i = b + 1; i < b + o; ++i) {
            its.indices.emplace_back(i - 1, i, i + o - 1);
            its.indices.emplace_back(i, i + o, i + o - 1);
        }
        its.indices.emplace_back(b + o - 1, b, b + 2 * o - 1);
        its.indices.emplace_back(b, b + o, b + 2 * o - 1);
    };

    for (const ExPolygon &p : polys) {
        add_walls(p.contour);
        for (const Polygon &h : p.holes)
            add_walls(h);
    }
    return its;
}

} // namespace Slic3r
