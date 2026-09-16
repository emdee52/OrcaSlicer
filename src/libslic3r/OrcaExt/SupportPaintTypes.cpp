// [ORCAPORT FILE] SupportPaintTypes - extensible registry of paintable support types
// Source: preFlight v1.3.0 support painter (fork sha f74dc69); NeoWave from NEOTKOCM_RELEASE_2_36.md
#include "SupportPaintTypes.hpp"

#include <algorithm>
#include <cmath>

#include "../Model.hpp"
#include "../Print.hpp"

namespace Slic3r {
namespace OrcaExt {

namespace {

// Explicit style states. Values 3+ are unused by support painting (the MMU painter lives in a
// separate facet annotation) and serialise through the existing TriangleSelector bitstream.
inline constexpr EnforcerBlockerType st_snug    = static_cast<EnforcerBlockerType>(3);
inline constexpr EnforcerBlockerType st_grid    = static_cast<EnforcerBlockerType>(4);
inline constexpr EnforcerBlockerType st_organic = static_cast<EnforcerBlockerType>(5);
inline constexpr EnforcerBlockerType st_neowave = static_cast<EnforcerBlockerType>(6);

constexpr double INF = 1.0e30;

// A hard (required) range term. soft bounds are left at 0 to mean "no soft falloff".
SupportPaintRule::Term hard_term(SupportFeature feature, double min_val, double max_val, double weight = 1.)
{
    SupportPaintRule::Term t;
    t.feature = feature;
    t.min_val = min_val;
    t.max_val = max_val;
    t.weight  = weight;
    t.hard    = true;
    return t;
}

// A soft (preferred) range term: full credit inside [min_val,max_val], decaying to zero between
// the hard window and [soft_min,soft_max].
SupportPaintRule::Term soft_term(SupportFeature feature, double min_val, double max_val,
                                 double soft_min, double soft_max, double weight = 1.)
{
    SupportPaintRule::Term t;
    t.feature  = feature;
    t.min_val  = min_val;
    t.max_val  = max_val;
    t.soft_min = soft_min;
    t.soft_max = soft_max;
    t.weight   = weight;
    t.hard     = false;
    return t;
}

std::vector<SupportPaintType> make_default_registry()
{
    std::vector<SupportPaintType> v;

    // Legacy generic enforcer: no forced style, the object's own support type/style applies.
    // It has no scoring rules, so automatic painting never selects it.
    {
        SupportPaintType t;
        t.state = SUPPORT_PAINT_DEFAULT;
        t.label = "Default";
        // Amber: the legacy light green was too close to Organic (tree) in the painter palette.
        t.color[0] = 1.f; t.color[1] = 0.65f; t.color[2] = 0.f; t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsDefault;
        v.emplace_back(t);
    }
    // Snug: classic engine, snug style. Broad functional default for any overhang large enough
    // to leave a support footprint; low priority so Grid/Organic/NeoWave can win when apt.
    {
        SupportPaintType t;
        t.state = st_snug;
        t.label = "Snug";
        t.color[0] = 0.4f; t.color[1] = 0.8f; t.color[2] = 1.f;  t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsSnug;
        t.rules = { SupportPaintRule{ { hard_term(SupportFeature::Area, 50., INF) }, 1. } };
        t.min_score = 0.5;
        t.priority  = 1.;
        v.emplace_back(t);
    }
    // Grid: classic engine, grid base. Maximum rigidity for large, low, flat overhangs; loses to
    // Snug above the height ceiling (tall towers are better off snug/organic).
    {
        SupportPaintType t;
        t.state = st_grid;
        t.label = "Grid";
        t.color[0] = 0.7f; t.color[1] = 0.5f; t.color[2] = 1.f;  t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsGrid;
        t.rules = { SupportPaintRule{ {
            hard_term(SupportFeature::Area,   400., INF),
            hard_term(SupportFeature::Span,    40., INF),
            hard_term(SupportFeature::Height,   0., 50.)
        }, 1. } };
        t.min_score = 0.9;
        t.priority  = 2.;
        v.emplace_back(t);
    }
    // Organic: tree engine. Small elevated patches, or curved elevated surfaces, where a tree
    // routes around the model and saves material.
    {
        SupportPaintType t;
        t.state = st_organic;
        t.label = "Organic";
        t.color[0] = 0.2f; t.color[1] = 0.9f; t.color[2] = 0.4f; t.color[3] = 1.f;
        t.is_tree = true;
        t.support_type = stTreeAuto;
        t.support_style = smsTreeOrganic;
        t.rules = {
            SupportPaintRule{ { hard_term(SupportFeature::Area,    0., 30.),
                               hard_term(SupportFeature::Height,  4., INF) }, 1. },
            SupportPaintRule{ { hard_term(SupportFeature::Curvature, 0.5, 1.),
                               hard_term(SupportFeature::Height,   10., INF) }, 1. }
        };
        t.min_score = 0.6;
        t.priority  = 3.;
        v.emplace_back(t);
    }
    // NeoWave: classic engine. Near-horizontal, hard overhangs - optionally right above another
    // surface (tight gap). Opt-in: enabled by default is false, the user checks it on.
    {
        SupportPaintType t;
        t.state = st_neowave;
        t.label = "NeoWave";
        t.color[0] = 0.f; t.color[1] = 0.8f; t.color[2] = 0.8f; t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stWaveSupport;
        t.support_style = smsDefault;
        t.overrides.force_base_pattern      = true; t.overrides.base_pattern      = smpNone;
        t.overrides.force_interface_pattern = true; t.overrides.interface_pattern = smipWave;
        t.overrides.force_wave_roof_pattern = true; t.overrides.wave_roof_pattern = smwrpWave;
        t.overrides.force_wave_roof_order   = true; t.overrides.wave_roof_order   = smwroSmart;
        t.overrides.force_wave_wall_loops   = true; t.overrides.wave_wall_loops   = 1;
        t.rules = {
            SupportPaintRule{ { hard_term(SupportFeature::WallAngle, 70., 90.),
                               soft_term(SupportFeature::GapBelow,   0., 1.5, 0., 4.) }, 1. },
            SupportPaintRule{ { hard_term(SupportFeature::WallAngle, 78., 90.) }, 1. }
        };
        t.min_score = 0.5;
        t.priority  = 3.;
        t.auto_enabled_by_default = false;
        v.emplace_back(t);
    }
    return v;
}

std::vector<SupportPaintType> &registry()
{
    static std::vector<SupportPaintType> v = make_default_registry();
    return v;
}

} // namespace

double support_feature_value(SupportFeature feature, const SupportRegionFeatures &f)
{
    switch (feature) {
    case SupportFeature::Area:      return f.area_mm2;
    case SupportFeature::Span:      return f.span_mm;
    case SupportFeature::Height:    return f.height_mm;
    case SupportFeature::WallAngle: return f.wall_angle_deg;
    case SupportFeature::Curvature: return f.curvature;
    case SupportFeature::GapBelow:  return f.gap_below_mm;
    default:                        return 0.;
    }
}

double support_term_membership(const SupportPaintRule::Term &term, double value)
{
    const double lo = std::min(term.min_val, term.max_val);
    const double hi = std::max(term.min_val, term.max_val);
    if (value >= lo && value <= hi)
        return 1.;

    if (value < lo) {
        // soft_min marks the zero-credit bound; no soft band when it is not below min_val.
        if (term.soft_min >= lo || value <= term.soft_min)
            return 0.;
        return (value - term.soft_min) / (lo - term.soft_min);
    }

    // value > hi
    if (term.soft_max <= hi || value >= term.soft_max)
        return 0.;
    return (term.soft_max - value) / (term.soft_max - hi);
}

double support_rule_score(const SupportPaintRule &rule, const SupportRegionFeatures &features)
{
    if (rule.terms.empty())
        return 0.;

    double sum = 0., sum_w = 0.;
    for (const SupportPaintRule::Term &term : rule.terms) {
        const double value = support_feature_value(term.feature, features);
        if (term.hard) {
            const double lo = std::min(term.min_val, term.max_val);
            const double hi = std::max(term.min_val, term.max_val);
            if (value < lo || value > hi)
                return 0.;
            sum   += term.weight;
            sum_w += term.weight;
        } else {
            sum   += term.weight * support_term_membership(term, value);
            sum_w += term.weight;
        }
    }
    return sum_w > 0. ? (sum / sum_w) * rule.weight : 0.;
}

double support_type_score(const SupportPaintType &type, const SupportRegionFeatures &features)
{
    double best = 0.;
    for (const SupportPaintRule &rule : type.rules)
        best = std::max(best, support_rule_score(rule, features));
    return best * type.priority;
}

EnforcerBlockerType support_paint_classify(const SupportRegionFeatures &features,
                                           const std::vector<EnforcerBlockerType> &enabled,
                                           bool build_plate_only)
{
    const SupportPaintType *best            = nullptr;
    const SupportPaintType *best_tree       = nullptr;
    double                  best_score      = 0.;
    double                  best_tree_score = 0.;

    // "Model geometry below" = the downward ray hit a surface, i.e. a normal support column would
    // rest on the part rather than reach the plate.
    const bool part_below = features.gap_below_mm < 1.0e29;

    for (const SupportPaintType &t : registry()) {
        if (t.rules.empty())
            continue;
        // An empty `enabled` set means "all auto-selectable types are allowed".
        if (!enabled.empty() && std::find(enabled.begin(), enabled.end(), t.state) == enabled.end())
            continue;
        const double score = support_type_score(t, features);
        // Eligible types compete normally.
        if (score >= t.min_score && (best == nullptr || score > best_score)) {
            best       = &t;
            best_score = score;
        }
        // Tree types are candidates for the build-plate-only override even when their own rules did
        // not reach min_score: a large flat overhang above the part is still better off on a tree,
        // whose branches can reach the plate, than on a normal column that would rest on the model.
        if (t.is_tree && (best_tree == nullptr || score > best_tree_score)) {
            best_tree       = &t;
            best_tree_score = score;
        }
    }

    // [ORCAPORT:PF-10-auto] Build-plate-only drops any support whose base would rest on the model,
    // so for a region with the part directly below, a branching (tree) type is the only one that
    // can actually reach the plate. Prefer it when one is enabled.
    if (build_plate_only && part_below && best_tree != nullptr)
        return best_tree->state;

    return best != nullptr ? best->state : EnforcerBlockerType::NONE;
}

const std::vector<SupportPaintType> &support_paint_types()
{
    return registry();
}

void register_support_paint_type(const SupportPaintType &type)
{
    registry().emplace_back(type);
}

const SupportPaintType *support_paint_type(EnforcerBlockerType state)
{
    for (const SupportPaintType &t : registry())
        if (t.state == state)
            return &t;
    return nullptr;
}

bool has_painted_support_styles(const PrintObject &object)
{
    const ModelObject *mo = object.model_object();
    if (mo == nullptr)
        return false;
    for (const ModelVolume *mv : mo->volumes) {
        if (!mv->is_model_part())
            continue;
        for (const SupportPaintType &t : registry())
            if (t.state != SUPPORT_PAINT_DEFAULT && t.state != SUPPORT_PAINT_BLOCKER &&
                mv->supported_facets.has_facets(*mv, t.state))
                return true;
    }
    return false;
}

bool has_painted_support_style(const PrintObject &object, EnforcerBlockerType state)
{
    const ModelObject *mo = object.model_object();
    if (mo == nullptr)
        return false;
    for (const ModelVolume *mv : mo->volumes)
        if (mv->is_model_part() && mv->supported_facets.has_facets(*mv, state))
            return true;
    return false;
}

} // namespace OrcaExt
} // namespace Slic3r
