// SPDX-License-Identifier: GPL-3.0-or-later
// Replaces the global operator new for the whole test binary, counting allocations made while a
// thread's flag is set.
#include "alloc_count.hpp"

#include <cstdlib>
#include <new>

namespace {
thread_local bool g_counting = false;
thread_local int g_allocations = 0;
thread_local int g_frees = 0;
thread_local int g_last_frees = 0;
} // namespace

void* operator new(std::size_t size) {
    if (g_counting) {
        ++g_allocations;
    }
    if (void* p = std::malloc(size == 0 ? 1 : size)) {
        return p;
    }
    throw std::bad_alloc();
}
void operator delete(void* p) noexcept {
    if (g_counting && p != nullptr) {
        ++g_frees;
    }
    std::free(p);
}
void operator delete(void* p, std::size_t) noexcept {
    if (g_counting && p != nullptr) {
        ++g_frees;
    }
    std::free(p);
}

namespace etude::test {

void start_counting_allocations() {
    g_allocations = 0;
    g_frees = 0;
    g_counting = true;
}

int frees_counted() { return g_last_frees; }

int stop_counting_allocations() {
    g_counting = false;
    g_last_frees = g_frees;
    return g_allocations;
}

} // namespace etude::test
