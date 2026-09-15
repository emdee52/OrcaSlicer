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

// A single paintable support type. This table is the single extension point for support painting:
// a new support type is one row here (plus, if it needs them, fields on SupportPaintOverrides).
// The gizmo builds its radio buttons and palette from this list, and the slicing dispatcher runs
// one engine pass per painted row.
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
};

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
