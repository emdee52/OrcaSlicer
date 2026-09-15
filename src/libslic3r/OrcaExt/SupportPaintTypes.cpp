// [ORCAPORT FILE] SupportPaintTypes - extensible registry of paintable support types
// Source: preFlight v1.3.0 support painter (fork sha f74dc69); NeoWave from NEOTKOCM_RELEASE_2_36.md
#include "SupportPaintTypes.hpp"

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

std::vector<SupportPaintType> make_default_registry()
{
    std::vector<SupportPaintType> v;

    // Legacy generic enforcer: no forced style, the object's own support type/style applies.
    {
        SupportPaintType t;
        t.state = SUPPORT_PAINT_DEFAULT;
        t.label = "Default";
        t.color[0] = 0.5f; t.color[1] = 1.f;  t.color[2] = 0.5f; t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsDefault;
        v.emplace_back(t);
    }
    // Snug: classic engine, object supports forced to snug in the painted region.
    {
        SupportPaintType t;
        t.state = st_snug;
        t.label = "Snug";
        t.color[0] = 0.4f; t.color[1] = 0.8f; t.color[2] = 1.f;  t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsSnug;
        v.emplace_back(t);
    }
    // Grid: classic engine, grid base pattern in the painted region.
    {
        SupportPaintType t;
        t.state = st_grid;
        t.label = "Grid";
        t.color[0] = 0.7f; t.color[1] = 0.5f; t.color[2] = 1.f;  t.color[3] = 1.f;
        t.is_tree = false;
        t.support_type = stNormalAuto;
        t.support_style = smsGrid;
        v.emplace_back(t);
    }
    // Organic: tree engine, organic style in the painted region.
    {
        SupportPaintType t;
        t.state = st_organic;
        t.label = "Organic";
        t.color[0] = 0.2f; t.color[1] = 0.9f; t.color[2] = 0.4f; t.color[3] = 1.f;
        t.is_tree = true;
        t.support_type = stTreeAuto;
        t.support_style = smsTreeOrganic;
        v.emplace_back(t);
    }
    // NeoWave: classic engine. The painter hardcodes its tested recipe: hollow base, wave
    // interface, wave roof, smart roof order and a single wall loop.
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
