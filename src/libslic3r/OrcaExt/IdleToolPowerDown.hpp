#ifndef slic3r_OrcaExt_IdleToolPowerDown_hpp_
#define slic3r_OrcaExt_IdleToolPowerDown_hpp_

// [ORCAPORT:MT-1] [ORCAPORT:MT-2] App-level settings for turning off idle hotends.
//
// These are not print/profile settings. The GUI stores them in app_config and mirrors them
// here, so the G-code post-processor in libslic3r can honour them without depending on the
// GUI. The G-code post-processor needs the FINAL G-code to know whether a tool comes back,
// which is why the decision lives there and not at plan time.

namespace Slic3r {
namespace OrcaExt {

struct IdleToolPowerDownSettings
{
    bool enabled{ false }; // MT-1: switch a tool off once it has no extrusion left.
    bool deep{ false };    // MT-2: also switch it off while it merely waits.
};

void                      set_idle_tool_power_down(bool enabled);
void                      set_idle_tool_power_down_deep(bool deep);
IdleToolPowerDownSettings idle_tool_power_down_settings();

} // namespace OrcaExt
} // namespace Slic3r

#endif
