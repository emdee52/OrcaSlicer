#include "IdleToolPowerDown.hpp"

#include <atomic>

namespace Slic3r {
namespace OrcaExt {

namespace {
std::atomic<bool> s_idle_tool_power_down{ false };
std::atomic<bool> s_idle_tool_power_down_deep{ false };
} // namespace

void set_idle_tool_power_down(bool enabled)
{
    s_idle_tool_power_down.store(enabled, std::memory_order_relaxed);
}

void set_idle_tool_power_down_deep(bool deep)
{
    s_idle_tool_power_down_deep.store(deep, std::memory_order_relaxed);
}

IdleToolPowerDownSettings idle_tool_power_down_settings()
{
    return { s_idle_tool_power_down.load(std::memory_order_relaxed),
             s_idle_tool_power_down_deep.load(std::memory_order_relaxed) };
}

} // namespace OrcaExt
} // namespace Slic3r
