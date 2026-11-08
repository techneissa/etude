// SPDX-License-Identifier: GPL-3.0-or-later
#include "alloc_probe.hpp"

#include <imgui.h>

#include <dlfcn.h>
#ifdef __APPLE__
#include <malloc/malloc.h>
#else
#include <malloc.h>
#endif

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <new>

namespace {
thread_local bool g_counting = false;
thread_local std::uint64_t g_ours = 0;
thread_local std::uint64_t g_system = 0;

// The bytes operator new has handed out and operator delete has taken back, over every thread,
// while tracking is on (a block that was made before it started counts when it is freed). A small
// block counts what the allocator reserved for it; a block of kBigBlock or more counts the size
// that was asked, which is kept in a table, since some allocators round a large block up by a
// quarter and the song's tables are all large blocks.
std::atomic<bool> g_tracking{false};
std::atomic<std::int64_t> g_live{0};
std::atomic<std::int64_t> g_peak{0};

constexpr std::size_t kBigBlock = 64 * 1024;
constexpr std::size_t kBigSlots = 1 << 14;
struct BigBlock {
    const void* block = nullptr;
    std::size_t size = 0;
};
BigBlock g_big[kBigSlots];
std::atomic_flag g_big_lock = ATOMIC_FLAG_INIT;
const void* const kGone = &g_big_lock; // marks a slot whose block was freed

struct BigLock {
    BigLock() {
        while (g_big_lock.test_and_set(std::memory_order_acquire)) {
        }
    }
    ~BigLock() { g_big_lock.clear(std::memory_order_release); }
};

std::size_t slot_of(const void* p) {
    return (reinterpret_cast<std::uintptr_t>(p) >> 4) * 0x9E3779B97F4A7C15ull >> 40 &
           (kBigSlots - 1);
}

std::size_t reserved_size(void* p) {
#ifdef __APPLE__
    return malloc_size(p);
#else
    return malloc_usable_size(p);
#endif
}

void add_live(std::int64_t size) {
    const std::int64_t live = g_live.fetch_add(size, std::memory_order_relaxed) + size;
    std::int64_t peak = g_peak.load(std::memory_order_relaxed);
    while (live > peak && !g_peak.compare_exchange_weak(peak, live, std::memory_order_relaxed)) {
    }
}

void track_alloc(void* p, std::size_t asked) {
    if (asked < kBigBlock) {
        add_live(static_cast<std::int64_t>(reserved_size(p)));
        return;
    }
    {
        const BigLock lock;
        std::size_t i = slot_of(p);
        while (g_big[i].block != nullptr && g_big[i].block != kGone) {
            i = (i + 1) & (kBigSlots - 1);
        }
        g_big[i] = {p, asked};
    }
    add_live(static_cast<std::int64_t>(asked));
}

void track_free(void* p) {
    if (p == nullptr) {
        return;
    }
    const std::size_t reserved = reserved_size(p);
    if (reserved >= kBigBlock) {
        const BigLock lock;
        for (std::size_t i = slot_of(p); g_big[i].block != nullptr; i = (i + 1) & (kBigSlots - 1)) {
            if (g_big[i].block == p) {
                const std::size_t asked = g_big[i].size;
                g_big[i] = {kGone, 0};
                g_live.fetch_sub(static_cast<std::int64_t>(asked), std::memory_order_relaxed);
                return;
            }
        }
    }
    g_live.fetch_sub(static_cast<std::int64_t>(reserved), std::memory_order_relaxed);
}

// True when `caller` is in this executable or in the C++ runtime.
bool is_ours(const void* caller) {
    static const void* const own_base = [] {
        Dl_info info{};
        return dladdr(reinterpret_cast<const void*>(&is_ours), &info) != 0 ? info.dli_fbase
                                                                           : nullptr;
    }();
    Dl_info info{};
    if (dladdr(caller, &info) == 0) {
        return true; // unknown: count it as ours rather than hide it
    }
    return info.dli_fbase == own_base ||
           (info.dli_fname != nullptr && (std::strstr(info.dli_fname, "libc++") != nullptr ||
                                          std::strstr(info.dli_fname, "libstdc++") != nullptr));
}

void count(const void* caller) {
    if (is_ours(caller)) {
        ++g_ours;
    } else {
        ++g_system;
    }
}

void* counted_alloc(std::size_t size, void* /*user*/) {
    if (g_counting) {
        count(__builtin_return_address(0));
    }
    return std::malloc(size == 0 ? 1 : size);
}

void counted_free(void* p, void* /*user*/) { std::free(p); }
} // namespace

void* operator new(std::size_t size) {
    if (g_counting) {
        count(__builtin_return_address(0));
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        if (g_tracking.load(std::memory_order_relaxed)) {
            track_alloc(p, size);
        }
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (g_tracking.load(std::memory_order_relaxed)) {
        track_free(p);
    }
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (g_tracking.load(std::memory_order_relaxed)) {
        track_free(p);
    }
    std::free(p);
}

namespace etude::app {

void count_allocations(bool on) { g_counting = on; }

std::uint64_t allocations_counted() { return g_ours; }
std::uint64_t system_allocations_counted() { return g_system; }

void track_heap(bool on) {
    g_tracking.store(on, std::memory_order_relaxed);
    g_live.store(0, std::memory_order_relaxed);
    g_peak.store(0, std::memory_order_relaxed);
}

std::int64_t heap_live_bytes() { return g_live.load(std::memory_order_relaxed); }
std::int64_t heap_peak_bytes() { return g_peak.load(std::memory_order_relaxed); }
void heap_reset_peak() { g_peak.store(g_live.load(std::memory_order_relaxed)); }

void count_imgui_allocations() { ImGui::SetAllocatorFunctions(counted_alloc, counted_free); }

} // namespace etude::app
