// SPDX-License-Identifier: GPL-3.0-or-later
#include "loop_wake.hpp"

#include <array>
#include <atomic>

namespace etude::app {

namespace {

std::atomic<bool> g_waiting{false};
std::atomic<std::uint64_t> g_requests{0};
std::atomic<void (*)()> g_hook{nullptr};
std::array<std::atomic<std::uint64_t>, static_cast<std::size_t>(WakeSource::Count)> g_by_source{};

} // namespace

// Sequentially consistent on purpose: the loop stores `waiting` then reads the count, a waker adds
// to the count then reads `waiting`, so one of the two always sees the other.
void wake_loop(WakeSource source) noexcept {
    g_by_source[static_cast<std::size_t>(source)].fetch_add(1, std::memory_order_relaxed);
    g_requests.fetch_add(1);
    if (g_waiting.exchange(false)) {
        if (void (*hook)() = g_hook.load()) {
            hook();
        }
    }
}

void set_wake_hook(void (*hook)()) noexcept { g_hook.store(hook); }

std::uint64_t wake_requests() noexcept { return g_requests.load(); }

bool arm_wait(std::uint64_t seen) noexcept {
    g_waiting.store(true);
    if (g_requests.load() != seen) {
        g_waiting.store(false);
        return false;
    }
    return true;
}

void disarm_wait() noexcept { g_waiting.store(false); }

} // namespace etude::app
