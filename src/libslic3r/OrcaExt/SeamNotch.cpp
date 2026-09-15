// [ORCAPORT FILE] SeamNotch - Nip/Tuck seam shaping for external perimeters
// Source: preFlight v1.3.0 (github.com/oozebot/preFlight), GCode.cpp:3979-4830; AGPLv3
#include "SeamNotch.hpp"

#include <algorithm>
#include <cmath>

#include "../libslic3r.h"

namespace Slic3r {
namespace OrcaExt {
namespace SeamNotch {

namespace {

constexpr double kPi = 3.14159265358979323846;

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

// Insert vertices within `taper` of one end and push them inward by depth*(1 - d/taper),
// maximum at the seam and tapering to zero at `taper`. `push` is the inward unit direction.
void resample_notch(Polyline3 &pl, bool at_start, double taper, double depth, const Vec2d &push) {
    Points3 &pts = pl.points;
    if (pts.size() < 2 || taper <= 0.0)
        return;
    if (!at_start)
        std::reverse(pts.begin(), pts.end());

    const double max_seg = std::max(taper * 0.15, 1.0);
    auto offset_at = [&](const Point3 &p, double d) -> Point3 {
        if (d >= taper)
            return p;
        const double f = depth * (1.0 - d / taper);
        Point3       q = p;
        q.x() += coord_t(std::lround(push.x() * f));
        q.y() += coord_t(std::lround(push.y() * f));
        return q;
    };

    Points3 out;
    out.reserve(pts.size() + 16);
    out.push_back(offset_at(pts[0], 0.0));

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
        // Only the portion of this segment inside the taper zone needs subdivision.
        const double split_len = std::min(seg, taper - dist);
        const int    nsub      = std::max(1, int(std::ceil(split_len / max_seg)));
        for (int s = 1; s <= nsub; ++s) {
            const double along = split_len * double(s) / double(nsub);
            const double t     = along / seg;
            Point3       q(coord_t(std::lround(a.x() + double(b.x() - a.x()) * t)),
                           coord_t(std::lround(a.y() + double(b.y() - a.y()) * t)), a.z());
            out.push_back(offset_at(q, dist + along));
        }
        if (split_len < seg)
            out.push_back(b);
        dist += seg;
    }

    if (!at_start)
        std::reverse(out.begin(), out.end());
    pts = std::move(out);
}

// Shorten a polyline from its end by `len` (scaled). Never drops below two points.
void clip_from_end(Polyline3 &pl, double len) {
    Points3 &pts = pl.points;
    if (len <= 0.0 || pts.size() < 3)
        return;
    double remaining = len;
    while (pts.size() > 2 && remaining > 1e-9) {
        Point3      &b   = pts.back();
        const Point3 &a  = pts[pts.size() - 2];
        const double seg = Vec2d(double(b.x() - a.x()), double(b.y() - a.y())).norm();
        if (seg <= remaining) {
            remaining -= seg;
            pts.pop_back();
        } else {
            const double t = (seg - remaining) / seg;
            b.x() = coord_t(std::lround(a.x() + double(b.x() - a.x()) * t));
            b.y() = coord_t(std::lround(a.y() + double(b.y() - a.y()) * t));
            remaining = 0.0;
        }
    }
}

} // namespace

bool apply(ExtrusionPaths &paths, bool loop_ccw, bool is_hole, SeamNotchType type,
           SeamNotchTarget target, double notch_width_factor, double corner_threshold_deg,
           int layer_index) {
    if (type == sntRegular || paths.empty())
        return false;
    if (target == snbtHolesOnly && !is_hole)
        return false;
    if (target == snbtOuterOnly && is_hole)
        return false;

    SeamNotchType notch = type;
    if (notch == sntAlternating)
        notch = (layer_index % 2 == 0) ? sntNip : sntTuck;

    const double ext_width = paths.front().width;
    if (!(ext_width > 0.0))
        return false;

    Vec2d d0 = first_dir(paths.front().polyline);
    Vec2d d1 = last_dir(paths.back().polyline);
    if (d0.squaredNorm() < 1e-12 || d1.squaredNorm() < 1e-12)
        return false;
    d0.normalize();
    d1.normalize();

    // A corner sharper than the threshold already hides the seam: skip.
    if (corner_threshold_deg > 0.0 && d0.dot(d1) < std::cos(corner_threshold_deg * kPi / 180.0))
        return false;

    const double taper = scale_(notch_width_factor * ext_width);
    const double depth = scale_(ext_width * 0.9);

    // Inward (into the solid) direction: left of travel for a CCW loop, flipped for CW, and
    // flipped again for a hole (whose enclosed region is the void).
    Vec2d left(-d0.y(), d0.x());
    Vec2d inward = loop_ccw ? left : -left;
    if (is_hole)
        inward = -inward;

    // Both preFlight and this port push along the seam bisector, oriented to the solid side.
    Vec2d push = d0 - d1;
    if (push.squaredNorm() < 1e-12)
        push = left;
    else
        push.normalize();
    if (push.dot(inward) < 0.0)
        push = -push;

    if (notch != sntTuck)
        resample_notch(paths.front().polyline, /*at_start=*/true, taper, depth, push);
    if (notch != sntNip)
        resample_notch(paths.back().polyline, /*at_start=*/false, taper, depth, push);

    // Asymmetric modes clip the non-notched endpoint so the wall does not double up.
    const double ext_adjust = scale_(ext_width * (1.0 - 0.5 * notch_width_factor));
    if (ext_adjust > 0.0) {
        if (notch == sntNip) {
            clip_from_end(paths.back().polyline, ext_adjust);
        } else if (notch == sntTuck) {
            Points3 &p = paths.front().polyline.points;
            std::reverse(p.begin(), p.end());
            clip_from_end(paths.front().polyline, ext_adjust);
            std::reverse(p.begin(), p.end());
        }
    }
    return true;
}

} // namespace SeamNotch
} // namespace OrcaExt
} // namespace Slic3r
