#include <catch2/catch_all.hpp>

#include "libslic3r/OrcaExt/SupportPaintTypes.hpp"

#include <string>
#include <vector>

using namespace Slic3r;
using namespace Slic3r::OrcaExt;

namespace {

EnforcerBlockerType state_of(const char *label)
{
    for (const SupportPaintType &t : support_paint_types())
        if (t.label != nullptr && label == std::string(t.label))
            return t.state;
    return EnforcerBlockerType::NONE;
}

// Mirrors the gizmo seeding: every auto-selectable type enabled by default.
std::vector<EnforcerBlockerType> default_enabled()
{
    std::vector<EnforcerBlockerType> v;
    for (const SupportPaintType &t : support_paint_types())
        if (!t.rules.empty() && t.auto_enabled_by_default)
            v.push_back(t.state);
    return v;
}

} // namespace

TEST_CASE("A large low flat overhang is painted as grid", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 1000.;
    f.span_mm        = 100.;
    f.height_mm      = 10.;
    f.wall_angle_deg = 85.;
    f.curvature      = 0.05;
    f.gap_below_mm   = 1000.;

    // NeoWave is off by default, so Grid competes against Snug/Organic only.
    CHECK(support_paint_classify(f, default_enabled()) == state_of("Grid"));
}

TEST_CASE("A small elevated overhang is painted as organic", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 15.;
    f.span_mm        = 10.;
    f.height_mm      = 40.;
    f.wall_angle_deg = 80.;
    f.curvature      = 0.1;
    f.gap_below_mm   = 1000.;

    CHECK(support_paint_classify(f, default_enabled()) == state_of("Organic"));
}

TEST_CASE("A wide elevated curved overhang is painted as organic", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 200.;
    f.span_mm        = 30.;
    f.height_mm      = 60.;
    f.wall_angle_deg = 40.;
    f.curvature      = 0.7;
    f.gap_below_mm   = 1000.;

    CHECK(support_paint_classify(f, default_enabled()) == state_of("Organic"));
}

TEST_CASE("A near-horizontal hard ceiling is painted as NeoWave when enabled", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 300.;
    f.span_mm        = 50.;
    f.height_mm      = 100.;
    f.wall_angle_deg = 88.;
    f.curvature      = 0.1;
    f.gap_below_mm   = 0.8;

    std::vector<EnforcerBlockerType> enabled = default_enabled();
    enabled.push_back(state_of("NeoWave"));

    CHECK(support_paint_classify(f, enabled) == state_of("NeoWave"));
}

TEST_CASE("NeoWave is never chosen when its type is disabled", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 300.;
    f.span_mm        = 50.;
    f.height_mm      = 100.;
    f.wall_angle_deg = 88.;
    f.curvature      = 0.1;
    f.gap_below_mm   = 0.8;

    CHECK(support_paint_classify(f, default_enabled()) != state_of("NeoWave"));
}

TEST_CASE("A flat overhang above the part becomes tree with build-plate-only", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 1000.;
    f.span_mm        = 100.;
    f.height_mm      = 10.;
    f.wall_angle_deg = 85.;
    f.curvature      = 0.05;
    f.gap_below_mm   = 2.; // model geometry directly below

    // Normally this is a rigid Grid region.
    CHECK(support_paint_classify(f, default_enabled()) == state_of("Grid"));
    // But a build-plate-only restriction would drop a normal column resting on the model, so the
    // tree type is preferred.
    CHECK(support_paint_classify(f, default_enabled(), true) == state_of("Organic"));
}

TEST_CASE("Build-plate-only keeps normal support when nothing is below the region", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 1000.;
    f.span_mm        = 100.;
    f.height_mm      = 10.;
    f.wall_angle_deg = 85.;
    f.curvature      = 0.05;
    f.gap_below_mm   = 1.0e30; // open down to the plate

    CHECK(support_paint_classify(f, default_enabled(), true) == state_of("Grid"));
}

TEST_CASE("Build-plate-only keeps normal support when no tree type is enabled", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 1000.;
    f.span_mm        = 100.;
    f.height_mm      = 10.;
    f.wall_angle_deg = 85.;
    f.curvature      = 0.05;
    f.gap_below_mm   = 2.;

    const std::vector<EnforcerBlockerType> enabled{state_of("Snug"), state_of("Grid")};
    CHECK(support_paint_classify(f, enabled, true) == state_of("Grid"));
}

TEST_CASE("A region that matches no rule is left unpainted", "[OrcaSupportPaint]")
{
    SupportRegionFeatures f;
    f.area_mm2       = 10.;
    f.span_mm        = 5.;
    f.height_mm      = 2.;
    f.wall_angle_deg = 50.;
    f.curvature      = 0.;
    f.gap_below_mm   = 1000.;

    CHECK(support_paint_classify(f, default_enabled()) == EnforcerBlockerType::NONE);
}

TEST_CASE("Clause membership decays outside the preferred window", "[OrcaSupportPaint]")
{
    SupportPaintRule::Term t;
    t.feature  = SupportFeature::GapBelow;
    t.min_val  = 0.;
    t.max_val  = 2.;
    t.soft_min = 0.;
    t.soft_max = 6.;

    CHECK(support_term_membership(t, 1.0) == 1.0);
    CHECK(support_term_membership(t, 4.0) == 0.5);
    CHECK(support_term_membership(t, 6.0) == 0.0);
    CHECK(support_term_membership(t, 100.0) == 0.0);
}

TEST_CASE("A hard term rejects a clause outside its window", "[OrcaSupportPaint]")
{
    SupportPaintRule rule;
    rule.terms.push_back(SupportPaintRule::Term{SupportFeature::Area, 0., 30., 0., 0., 1., true});

    SupportRegionFeatures inside;
    inside.area_mm2 = 20.;
    CHECK(support_rule_score(rule, inside) == 1.0);

    SupportRegionFeatures outside;
    outside.area_mm2 = 200.;
    CHECK(support_rule_score(rule, outside) == 0.0);
}
