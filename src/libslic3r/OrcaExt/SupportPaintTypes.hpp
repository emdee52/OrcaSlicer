// [ORCAPORT FILE] SupportPaintTypes - extensible registry of paintable support types
// Source: preFlight v1.3.0 support painter (fork sha f74dc69); NeoWave from NEOTKOCM_RELEASE_2_36.md
#ifndef slic3r_OrcaExt_SupportPaintTypes_hpp_
#define slic3r_OrcaExt_SupportPaintTypes_hpp_

#include <cstdint>
#include <vector>

#include "../PrintConfig.hpp"
#include "../TriangleSelector.hpp"

namespace Slic3r {

class PrintObject;
class ModelObject;

namespace OrcaExt {

// ---------------------------------------------------------------------------------------------
// Automatic-painting feature model
//
// The automatic painter measures each overhang region once and scores every registered support
// type against that measurement. A new support type is one registry row (see below); it does not
// touch the classifier or the gizmo. Adding a new *feature* means one value in SupportFeature plus
// one computation in SupportAutoPaint, after which every type can weigh against it.
// ---------------------------------------------------------------------------------------------

// Measurements of one overhang region, shared by every support type's scoring rule.
struct SupportRegionFeatures
{
    double area_mm2{0.};        // region surface area
    double span_mm{0.};         // largest XY extent of the region ("bridge length" proxy)
    double height_mm{0.};       // lowest point of the region above the plate
    double wall_angle_deg{0.};  // 0 = vertical wall, 90 = horizontal ceiling
    double curvature{0.};       // 0 = flat, 1 = highly curved
    double gap_below_mm{1.0e30}; // distance from the region down to the next model surface (inf = none)
};

enum class SupportFeature { Area, Span, Height, WallAngle, Curvature, GapBelow, Count };

// One conjunctive clause: a set of ranged preferences over the feature vector. The clause score is
// the weight-averaged membership of each term; a `hard` term outside its [min,max] window zeroes
// the whole clause. A type may carry several clauses (OR) and keeps its best-scoring one.
struct SupportPaintRule
{
    struct Term
    {
        SupportFeature feature{SupportFeature::Area};
        double         min_val{0.};   // full credit inside [min_val, max_val]
        double         max_val{0.};
        double         soft_min{0.};  // zero credit at/below soft_min (<= min_val)
        double         soft_max{0.};  // zero credit at/above soft_max (>= max_val)
        double         weight{1.};    // relative importance within the clause
        bool           hard{false};   // outside [min_val,max_val] rejects the clause
    };
    std::vector<Term> terms;
    double            weight{1.};     // clause strength within the type's OR group
};

// A single paintable support type. This table is the single extension point for support painting:
// a new support type is one row here (plus, if it needs them, fields on SupportPaintOverrides).
// The gizmo builds its radio buttons and palette from this list, and the slicing dispatcher runs
// one engine pass per painted row. The `rules` field drives automatic selection.
struct SupportPaintOverrides
{
    bool   force_base_pattern{false};
    SupportMaterialPattern base_pattern{smpDefault};

    bool   force_interface_pattern{false};
    SupportMaterialInterfacePattern interface_pattern{smipAuto};

    bool   force_wave_roof_pattern{false};
    SupportMaterialWaveRoofPattern wave_roof_pattern{smwrpWave};

    bool   force_wave_roof_order{false};
    SupportMaterialWaveRoofOrder wave_roof_order{smwroSmart};

    bool   force_wave_wall_loops{false};
    int    wave_wall_loops{1};
};

struct SupportPaintType
{
    // Paint state stored in ModelVolume::supported_facets. Support painting and MMU painting use
    // separate facet annotations, so states 3+ are free here (values 3..17 serialise in the
    // existing TriangleSelector bitstream).
    EnforcerBlockerType  state{EnforcerBlockerType::NONE};
    // User-visible radio label (English; the GUI localises it).
    const char          *label{""};
    // Prepare-view colour (RGBA).
    float                color[4]{0.f, 0.f, 0.f, 1.f};
    // Engine dispatch. `is_tree` selects TreeSupport; otherwise the Normal/classic engine.
    bool                 is_tree{false};
    SupportType          support_type{stNormalAuto};
    SupportMaterialStyle support_style{smsDefault};
    // Forced object-config overrides applied only to this painted region's engine pass.
    SupportPaintOverrides overrides;
    // Automatic-painting scoring: OR of clauses. Empty => this row is not auto-selectable
    // (e.g. the legacy generic "Default" enforcer, which is chosen manually).
    std::vector<SupportPaintRule> rules;
    // Eligibility floor: the type only wins when its weighted score reaches this value.
    double min_score{1.};
    // Global bias against the other types (future strength/material weighting UI). A score is
    // multiplied by this before comparison.
    double priority{1.};
    // Seeds the gizmo's per-type checkbox the first time it is shown.
    bool   auto_enabled_by_default{true};
};

// Value of one feature from the measurement vector.
double support_feature_value(SupportFeature feature, const SupportRegionFeatures &f);

// Membership of `value` in the term window: 1 inside [min_val,max_val], decaying linearly to 0 at
// the soft bounds outside it (soft bounds default to the hard bounds when unset).
double support_term_membership(const SupportPaintRule::Term &term, double value);

// Score of one clause in [0,1] (0 when a hard term misses).
double support_rule_score(const SupportPaintRule &rule, const SupportRegionFeatures &features);

// Score of a type: its best-scoring clause times its priority (0 when it has no rules).
double support_type_score(const SupportPaintType &type, const SupportRegionFeatures &features);

// Pick the best-matching enabled support type for the region, or NONE to leave it unpainted.
// `enabled` is the set of states the user allowed automatic painting to choose; an empty set
// means every auto-selectable type is eligible. When `build_plate_only` is set and the region has
// model geometry directly below it, an eligible tree type is preferred: a normal/grid column there
// would rest on the model and be dropped by the build-plate-only restriction, while a tree can
// branch down to the plate.
EnforcerBlockerType support_paint_classify(const SupportRegionFeatures &features,
                                           const std::vector<EnforcerBlockerType> &enabled = {},
                                           bool build_plate_only = false);

// The ordered registry (guarded statics). The first entry is the legacy generic "Default"
// enforcer, which keeps the object's own support type/style so existing painted projects are
// unaffected.
const std::vector<SupportPaintType> &support_paint_types();

// Append a new support type to the registry. Must be called before any painting/slicing that
// relies on it (e.g. from an init function), otherwise the data-only table above is enough.
void register_support_paint_type(const SupportPaintType &type);

// Lookup by paint state, or nullptr when the state is not a registered support type.
const SupportPaintType *support_paint_type(EnforcerBlockerType state);

// Legacy generic enforcer/blocker states (object style / exclusion), never style-forcing.
inline constexpr EnforcerBlockerType SUPPORT_PAINT_DEFAULT = EnforcerBlockerType::ENFORCER;
inline constexpr EnforcerBlockerType SUPPORT_PAINT_BLOCKER = EnforcerBlockerType::BLOCKER;

// True when `object` has at least one facet painted with any registered explicit support style
// (i.e. a state other than the legacy default enforcer/blocker/none).
bool has_painted_support_styles(const PrintObject &object);

// True when `object` has at least one facet painted with the given non-legacy style state.
bool has_painted_support_style(const PrintObject &object, EnforcerBlockerType state);

} // namespace OrcaExt
} // namespace Slic3r

#endif // slic3r_OrcaExt_SupportPaintTypes_hpp_
