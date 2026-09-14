#include "FreeZ.hpp"

#include <atomic>

namespace Slic3r {
namespace OrcaExt {

namespace {
std::atomic<bool> s_free_z{ false };
} // namespace

void set_free_z(bool enabled)
{
    s_free_z.store(enabled, std::memory_order_relaxed);
}

bool free_z()
{
    return s_free_z.load(std::memory_order_relaxed);
}

} // namespace OrcaExt
} // namespace Slic3r
