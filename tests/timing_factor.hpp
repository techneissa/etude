// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdlib>

// Wall-clock bounds in tests are multiplied by this: 1 in a normal build, 10 under a sanitizer,
// which runs the same work several times slower.
#if defined(__SANITIZE_ADDRESS__) || defined(__SANITIZE_THREAD__)
#define ETUDE_TEST_SANITIZED 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer) || __has_feature(thread_sanitizer) ||                         \
    __has_feature(undefined_behavior_sanitizer) || __has_feature(memory_sanitizer)
#define ETUDE_TEST_SANITIZED 1
#endif
#endif

namespace etude::test {

#ifdef ETUDE_TEST_SANITIZED
inline constexpr double kTimingFactor = 10.0;
#else
inline constexpr double kTimingFactor = 1.0;
#endif

// Assertions on wall-clock or thread-CPU time run only under `just bench` (ETUDE_TIMING is set
// there), never in the unit suite: on a loaded machine they flake and never caught a bug there.
inline bool timing_run() { return std::getenv("ETUDE_TIMING") != nullptr; }

} // namespace etude::test

// CHECK_TIMING(expr): CHECK(expr) when timing is being measured, nothing otherwise (expr still
// compiles). TIMING_ONLY: a decorator for a test case that is nothing but a timing claim, so the
// suite skips it: TEST_CASE("name" * TIMING_ONLY). `just bench` runs the test files that name
// either one.
#define CHECK_TIMING(...)                                                                          \
    do {                                                                                           \
        if (::etude::test::timing_run()) {                                                       \
            CHECK(__VA_ARGS__);                                                                    \
        }                                                                                          \
    } while (0)
#define TIMING_ONLY doctest::skip(!::etude::test::timing_run())
