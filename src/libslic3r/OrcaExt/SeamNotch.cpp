// [ORCAPORT FILE] SeamNotch - Nip/Tuck seam shaping for external perimeters
// Source: preFlight v1.3.0 (github.com/oozebot/preFlight), GCode.cpp:3979-4830; AGPLv3
#include "SeamNotch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>

#include <boost/log/trivial.hpp>

#include "../libslic3r.h"

namespace Slic3r {
namespace OrcaExt {
namespace SeamNotch {

namespace {

constexpr double kPi = 3.14159265358979323846;

bool debug_enabled() {
    static const bool enabled = std::getenv("ORCA_SEAM_NOTCH_DEBUG") != nullptr;
    return enabled;
}

Vec2d first_dir(const Polyline3 &pl) {
    if (pl.points.size() < 2)
        return Vec2d::Zero();
    const Point3 &a = pl.points[0];
    const Point3 &b = pl.points[1];
    return Vec2d(double(b.x() - a.x()), double(b.y() - a.y()));
}

Vec2d last_dir(const Polyline3 &pl) {
    if (pl.points.size() < 2)
        return Vec2d::Zero();
    const Point3 &a = pl.points[pl.points.size() - 2];
    const Point3 &b = pl.points.back();
    return Vec2d(double(b.x() - a.x()), double(b.y() - a.y()));
}

// Push the interior points within `taper` of one end inward by depth * sin(pi*d/taper). The
// seam endpoint itself is left at nominal, so the loop stays closed (approach A).
void push_zone(Polyline3 &pl, bool at_start, double taper, double depth, const Vec2d &push) {
    Points3 &pts = pl.points;
    if (pts.size() < 2 || taper <= 0.0)
        return;
    if (!at_start)
        std::reverse(pts.begin(), pts.end());

    const double max_seg = std::max(taper * 0.15, 1.0);
    auto         offset_at = [&](const Point3 &p, double d) -> Point3 {
        if (d <= 0.0 || d >= taper)
            return p;
        const double f = depth * std::sin(kPi * d / taper);
        Point3       q = p;
        q.x() += coord_t(std::lround(push.x() * f));
        q.y() += coord_t(std::lround(push.y() * f));
        return q;
    };

    Points3 out;
    out.reserve(pts.size() + 16);
    out.push_back(pts[0]); // seam endpoint: nominal

    double dist = 0.0;
    for (size_t i = 1; i < pts.size(); ++i) {
        const Point3 &a   = pts[i - 1];
        const Point3 &b   = pts[i];
        const double  seg = Vec2d(double(b.x() - a.x()), double(b.y() - a.y())).norm();
        if (dist >= taper || seg < 1e-9) {
            out.push_back(b);
            dist += seg;
            continue;
        }
        const double split = std::min(seg, taper - dist);
        const int    nsub  = std::max(1, int(std::ceil(split / max_seg)));
        for (int s = 1; s <= nsub; ++s) {
            const double along = split * double(s) / double(nsub);
            const double t     = along / seg;
            Point3       q(coord_t(std::lround(a.x() + double(b.x() - a.x()) * t)),
                           coord_t(std::lround(a.y() + double(b.y() - a.y()) * t)), a.z());
            out.push_back(offset_at(q, dist + along));
        }
        if (split < seg)
            out.push_back(b);
        dist += seg;
    }

    if (!at_start)
        std::reverse(out.begin(), out.end());
    pts = std::move(out);
}

} // namespace

ExternalNotch apply_external(ExtrusionPaths &paths, bool loop_ccw, bool is_hole, SeamNotchType type,
                             SeamNotchTarget target, double notch_width_factor,
                             double corner_threshold_deg, int layer_index) {
    ExternalNotch result;
    if (type == sntRegular || paths.empty())
        return result;
    if (target == snbtHolesOnly && !is_hole)
        return result;
    if (target == snbtOuterOnly && is_hole)
        return result;

    SeamNotchType notch = type;
    if (notch == sntAlternating)
        notch = (layer_index % 2 == 0) ? sntNip : sntTuck;

    const double ext_width = paths.front().width;
    if (!(ext_width > 0.0))
        return result;

    Vec2d d0 = first_dir(paths.front().polyline);
    Vec2d d1 = last_dir(paths.back().polyline);
    if (d0.squaredNorm() < 1e-12 || d1.squaredNorm() < 1e-12)
        return result;
    d0.normalize();
    d1.normalize();

    // A real outer corner already hides the seam. A hole is a polygonized circle, so its
    // "corners" are artifacts - never skip a hole on this test.
    if (!is_hole && corner_threshold_deg > 0.0
        && d0.dot(d1) < std::cos(corner_threshold_deg * kPi / 180.0)) {
        if (debug_enabled())
            BOOST_LOG_TRIVIAL(warning) << "[ORCAPORT:PF-1] notch skip: sharp corner";
        return result;
    }

    // Loop-length guard (preFlight) + taper clamp: never wrap a small loop.
    double loop_len = 0.0;
    for (const ExtrusionPath &p : paths)
        loop_len += p.polyline.length();
    const double notch_width_mm = notch_width_factor * ext_width;
    if (loop_len < scale_(notch_width_mm * 3.0)) {
        if (debug_enabled())
            BOOST_LOG_TRIVIAL(warning) << "[ORCAPORT:PF-1] notch skip: loop too short " << unscale_(loop_len)
                                       << "mm < " << (notch_width_mm * 3.0) << "mm";
        return result;
    }
    const double taper = std::min(scale_(notch_width_mm), 0.25 * loop_len);
    const double depth = scale_(ext_width * 0.9);

    // Inward (into the solid) direction: left of travel for a CCW loop, flipped for CW, and
    // flipped again for a hole. This is the radial direction on a bore - robust, unlike the
    // preFlight bisector which degenerates on a polygonized seam.
    const Vec2d left(-d0.y(), d0.x());
    Vec2d       inward = loop_ccw ? left : -left;
    if (is_hole)
        inward = -inward;

    if (notch != sntTuck)
        push_zone(paths.front().polyline, /*at_start=*/true, taper, depth, inward);
    if (notch != sntNip)
        push_zone(paths.back().polyline, /*at_start=*/false, taper, depth, inward);

    result.applied = true;
    result.seam    = paths.front().polyline.points.front().to_point();
    result.push    = inward;
    result.depth   = depth;
    result.taper   = taper;
    result.width   = scale_(ext_width);

    if (debug_enabled())
        BOOST_LOG_TRIVIAL(warning)
            << "[ORCAPORT:PF-1] notch APPLY layer=" << layer_index << " hole=" << (is_hole ? 1 : 0)
            << " ccw=" << (loop_ccw ? 1 : 0) << " type=" << int(notch) << " width=" << ext_width
            << " loop_len=" << unscale_(loop_len) << " taper=" << unscale_(taper)
            << " depth=" << unscale_(depth) << " push=(" << inward.x() << "," << inward.y() << ")";
    return result;
}

bool trim_inner(ExtrusionPaths &paths, const ExternalNotch &notch, double inner_width) {
    if (!notch.applied || paths.empty())
        return false;

    // Project the V-leg onto the inner centreline (~one bead in from the outer).
    const Vec2d target = notch.seam.cast<double>() + notch.push * (double(notch.width) + scale_(inner_width) * 0.5);

    double best_d2  = std::numeric_limits<double>::max();
    size_t best_pi  = 0;
    double best_arc = 0.0;
    for (size_t pi = 0; pi < paths.size(); ++pi) {
        const Points3 &p   = paths[pi].polyline.points;
        double         arc = 0.0;
        for (size_t i = 1; i < p.size(); ++i) {
            const Vec2d a(double(p[i - 1].x()), double(p[i - 1].y()));
            const Vec2d b(double(p[i].x()), double(p[i].y()));
            const Vec2d ab = b - a;
            const double l2 = ab.squaredNorm();
            const double t  = l2 > 1e-12 ? std::clamp((target - a).dot(ab) / l2, 0.0, 1.0) : 0.0;
            const double d2 = (target - (a + ab * t)).squaredNorm();
            if (d2 < best_d2) {
                best_d2  = d2;
                best_pi  = pi;
                best_arc = arc + ab.norm() * t;
            }
            arc += ab.norm();
        }
    }
    if (best_d2 == std::numeric_limits<double>::max())
        return false;
    // Only relieve an inner wall that belongs to the same seam (avoids cross-island trims).
    const double guard = double(scale_(3.0));
    if (best_d2 > guard * guard)
        return false;

    // Nudge the inner points near the crossing deeper into the solid (the relief pocket). The
    // inner loop stays closed, which the cut-a-gap approach would not.
    Points3      &p      = paths[best_pi].polyline.points;
    const double  relief = 0.5 * notch.depth;
    double        arc    = 0.0;
    bool          moved  = false;
    for (size_t i = 0; i < p.size(); ++i) {
        if (i > 0)
            arc += Vec2d(double(p[i].x() - p[i - 1].x()), double(p[i].y() - p[i - 1].y())).norm();
        const double d = std::abs(arc - best_arc);
        if (d < notch.taper) {
            const double f = relief * std::sin(kPi * d / notch.taper);
            p[i].x() += coord_t(std::lround(notch.push.x() * f));
            p[i].y() += coord_t(std::lround(notch.push.y() * f));
            moved = true;
        }
    }
    if (debug_enabled())
        BOOST_LOG_TRIVIAL(warning) << "[ORCAPORT:PF-1] inner relief " << (moved ? "APPLY" : "skip");
    return moved;
}

} // namespace SeamNotch
} // namespace OrcaExt
} // namespace Slic3r
