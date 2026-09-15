// [ORCAPORT FILE] NeoWaveContact - NeoWave support contact layer (Z-wave to reduce bonding)
// Source: NEOTKOCM_RELEASE_2_37.md; fork sha c3508e5a92
#include "NeoWaveContact.hpp"

#include <algorithm>
#include <cmath>
#include <limits>

#include "../ExtrusionEntity.hpp"
#include "../ExtrusionEntityCollection.hpp"
#include "../GCodeWriter.hpp"
#include "../Line.hpp"
#include "../Polyline.hpp"

namespace Slic3r {
namespace OrcaExt {
namespace NeoWaveContact {

namespace {

constexpr double kPi = 3.14159265358979323846;
// Micro-segments per wave period; 8 gives a smooth-enough sinusoid.
constexpr int kSegmentsPerPeriod = 8;
// Hard cap on micro-segments per input line (backstop for a mis-set near-zero period).
constexpr int kMaxSegmentsPerLine = 4096;

double path_line_width_mm(coord_t width_scaled) {
    const double w = unscale<double>(width_scaled);
    return w > 1e-9 ? w : 0.4;
}

double effective_period(double period_mm, coord_t width_scaled) {
    const double min_period = std::max(0.2, path_line_width_mm(width_scaled));
    double       p          = period_mm;
    if (p < 1e-9)
        p = path_line_width_mm(width_scaled);
    return std::max(p, min_period);
}

int segments_for_line(double line_length_mm, double period_mm) {
    const double seg_target = period_mm / double(kSegmentsPerPeriod);
    if (seg_target < 1e-9)
        return 1;
    return std::min(kMaxSegmentsPerLine,
                    std::max(1, int(std::ceil(line_length_mm / seg_target))));
}

} // namespace

double resolve_period_mm(double period_mm, coord_t width_scaled) {
    return effective_period(period_mm, width_scaled);
}

double xy_feedrate_cap(double period_mm, double amplitude_mm, double max_z_speed_mm_s,
                       coord_t width_scaled) {
    if (amplitude_mm < 1e-9)
        return std::numeric_limits<double>::max();
    const double period = effective_period(period_mm, width_scaled);
    // Z(t) = A*sin(2*pi*f*t), f = v_xy / period -> max |dZ/dt| = 2*pi*A*v_xy/period.
    const double xy_speed_max = max_z_speed_mm_s * period / (2.0 * kPi * amplitude_mm);
    return xy_speed_max * 60.0; // mm/min
}

std::string emit_part_wave(const ExtrusionPath                         &path,
                           GCodeWriter                                  &writer,
                           double                                        nominal_z,
                           double                                        F,
                           double                                        e_per_mm,
                           bool                                          force_no_extrusion,
                           const std::function<Vec2d(const Point &)>    &point_to_gcode,
                           double                                        amplitude_mm,
                           double                                        period_mm,
                           double                                        max_z_speed_mm_s) {
    std::string gcode;
    if (amplitude_mm < 1e-9)
        return gcode;

    const double period = effective_period(period_mm, coord_t(path.width));

    // Cap the XY feedrate so the implied Z feedrate stays within the limit.
    double                      F_wave = F;
    const double                cap    = xy_feedrate_cap(period, amplitude_mm, max_z_speed_mm_s,
                                                          coord_t(path.width));
    F_wave = std::min(F, cap);
    if (std::abs(F_wave - F) > 1e-9)
        gcode += writer.set_speed(F_wave, "", "");

    double weave_dist = 0.0;
    for (const Line3 &line : path.polyline.lines()) {
        const double line_length = line.length() * SCALING_FACTOR;
        if (line_length < 1e-9)
            continue;
        const double dE      = e_per_mm * line_length;
        const int    n_segs  = segments_for_line(line_length, period);
        const double seg_len = line_length / double(n_segs);
        const double dE_seg  = dE / double(n_segs);

        const Vec2d pt_a = point_to_gcode(line.a.to_point());
        const Vec2d pt_b = point_to_gcode(line.b.to_point());
        for (int si = 0; si < n_segs; ++si) {
            const double t     = double(si + 1) / double(n_segs);
            const Vec2d  pt    = pt_a + t * (pt_b - pt_a);
            const double d     = weave_dist + seg_len * double(si + 1);
            const double phase = (d / period) * 2.0 * kPi;
            // Upward-only: valleys sit at nominal_z (touch the interface), crests lift away.
            const double z = nominal_z + amplitude_mm * std::abs(std::sin(phase));
            gcode += writer.extrude_to_xyz(Vec3d(pt(0), pt(1), z), dE_seg, "", force_no_extrusion);
        }
        weave_dist += line_length;
    }
    return gcode;
}

std::string restore_z(GCodeWriter &writer, double nominal_z) {
    return writer.travel_to_z(nominal_z, "NeoWave contact: restore layer Z");
}

bool apply_support_wave(ExtrusionPath &path, double amplitude_mm, double period_mm) {
    if (amplitude_mm < 1e-9 || path.polyline.points.size() < 2)
        return false;

    const double  period     = effective_period(period_mm, coord_t(path.width));
    const coord_t amp_scaled = coord_t(amplitude_mm / SCALING_FACTOR);

    Points3 pts;
    pts.reserve(path.polyline.points.size() * 2);

    double weave_dist = 0.0;
    for (const Line3 &line : path.polyline.lines()) {
        const double line_length = line.length() * SCALING_FACTOR;
        if (line_length < 1e-9)
            continue;
        const int    n_segs  = segments_for_line(line_length, period);
        const double seg_len = line_length / double(n_segs);

        if (pts.empty())
            pts.emplace_back(line.a);

        const coord_t dx = line.b.x() - line.a.x();
        const coord_t dy = line.b.y() - line.a.y();
        for (int si = 0; si < n_segs; ++si) {
            const double t  = double(si + 1) / double(n_segs);
            const coord_t x = line.a.x() + coord_t(std::llround(double(dx) * t));
            const coord_t y = line.a.y() + coord_t(std::llround(double(dy) * t));
            const double  d = weave_dist + seg_len * double(si + 1);
            const double  phase = (d / period) * 2.0 * kPi;
            // Downward-only: peaks sit at the nominal support Z (touch the part), valleys drop
            // away from it. The Z here is a scaled offset relative to the layer's nominal Z.
            const coord_t z = coord_t(-double(amp_scaled) * std::abs(std::sin(phase)));
            pts.emplace_back(Point3(x, y, z));
        }
        weave_dist += line_length;
    }

    if (pts.size() < 2)
        return false;

    path.polyline.points = std::move(pts);
    path.z_contoured     = true;
    return true;
}

int apply_support_wave(std::vector<ExtrusionEntity *> &entities, double amplitude_mm, double period_mm) {
    int n = 0;
    for (ExtrusionEntity *ee : entities) {
        if (ee == nullptr)
            continue;
        if (auto *path = dynamic_cast<ExtrusionPath *>(ee)) {
            if (apply_support_wave(*path, amplitude_mm, period_mm))
                ++n;
        } else if (auto *mp = dynamic_cast<ExtrusionMultiPath *>(ee)) {
            for (ExtrusionPath &p : mp->paths)
                if (apply_support_wave(p, amplitude_mm, period_mm))
                    ++n;
        } else if (auto *loop = dynamic_cast<ExtrusionLoop *>(ee)) {
            for (ExtrusionPath &p : loop->paths)
                if (apply_support_wave(p, amplitude_mm, period_mm))
                    ++n;
        } else if (auto *coll = dynamic_cast<ExtrusionEntityCollection *>(ee)) {
            n += apply_support_wave(coll->entities, amplitude_mm, period_mm);
        }
    }
    return n;
}

} // namespace NeoWaveContact
} // namespace OrcaExt
} // namespace Slic3r
