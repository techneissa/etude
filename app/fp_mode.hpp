// SPDX-License-Identifier: GPL-3.0-or-later
// Floating-point mode for the analysis thread.
#pragma once

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
#include <xmmintrin.h>
#endif

namespace etude::app {

// Flushes denormal results and inputs to zero on the calling thread. A decaying piano note ends in
// denormals, which are very slow on some CPUs. Per-thread: call it at the top of the thread.
inline void enable_flush_to_zero() {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__)
    _mm_setcsr(_mm_getcsr() | 0x8040u); // FTZ (bit 15) and DAZ (bit 6)
#elif defined(__aarch64__)
    unsigned long long fpcr = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(fpcr));
    fpcr |= (1ull << 24); // FZ
    __asm__ volatile("msr fpcr, %0" : : "r"(fpcr));
#endif
}

} // namespace etude::app
