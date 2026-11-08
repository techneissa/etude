// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/ui_grow_hold.hpp"

#include <cmath>

#include <doctest/doctest.h>

using namespace etude;

namespace {
constexpr float kPpmm96 = 96.0f / 25.4f;
constexpr std::int64_t kMs = 1'000'000;
} // namespace

TEST_CASE("ui grow hold: the first size is taken at once") {
    UiGrowHold hold;
    CHECK(hold.grow(1920, 1080, kPpmm96, 0) == doctest::Approx(1920.0f / 1366.0f));
}

TEST_CASE("ui grow hold: a resize keeps the old growth until the size is still") {
    UiGrowHold hold;
    CHECK(hold.grow(1366, 768, kPpmm96, 0) == doctest::Approx(1.0f));
    // A maximise: a new size every 16 ms.
    std::int64_t t = 1000 * kMs;
    for (int i = 1; i <= 10; ++i, t += 16 * kMs) {
        CHECK(hold.grow(1366.0f + 55.0f * static_cast<float>(i), 768.0f + 31.0f * static_cast<float>(i),
                        kPpmm96, t) == doctest::Approx(1.0f));
    }
    const std::int64_t last = t - 16 * kMs;
    // Still, but just under the settle time: held. Exactly at it: the new growth.
    CHECK(hold.grow(1916, 1078, kPpmm96, last + kUiGrowSettleNs - 1) == doctest::Approx(1.0f));
    CHECK(hold.grow(1916, 1078, kPpmm96, last + kUiGrowSettleNs) ==
          doctest::Approx(1916.0f / 1366.0f));
}

TEST_CASE("ui grow hold: a size that changes again restarts the wait") {
    UiGrowHold hold;
    hold.grow(1366, 768, kPpmm96, 0);
    hold.grow(1920, 1080, kPpmm96, 1000 * kMs);
    hold.grow(2000, 1100, kPpmm96, 1000 * kMs + kUiGrowSettleNs - kMs);
    CHECK(hold.grow(2000, 1100, kPpmm96, 1000 * kMs + kUiGrowSettleNs) == doctest::Approx(1.0f));
    CHECK(hold.grow(2000, 1100, kPpmm96, 1000 * kMs + 2 * kUiGrowSettleNs) ==
          doctest::Approx(1100.0f / 768.0f));
}

TEST_CASE("ui grow hold: a new density (a scale chosen, not a resize) is taken at once") {
    UiGrowHold hold;
    hold.grow(1920, 1080, kPpmm96, 0);
    CHECK(hold.grow(1920, 1080, kPpmm96 * 1.5f, kMs) ==
          doctest::Approx(ui_window_grow(1920, 1080, kPpmm96 * 1.5f)));
}

TEST_CASE("ui grow hold: a size that changes inside the UI's own band changes nothing") {
    UiGrowHold hold;
    hold.grow(1000, 600, kPpmm96, 0);
    // Both below the design size: growth 1 throughout, nothing held.
    CHECK(hold.grow(1200, 700, kPpmm96, kMs) == doctest::Approx(1.0f));
}

TEST_CASE("ui grow hold: a minimised window (no size) changes nothing, and a restore keeps the scale") {
    UiGrowHold hold;
    const float big = 1920.0f / 1366.0f;
    CHECK(hold.grow(1920, 1080, kPpmm96, 0) == doctest::Approx(big));
    // Minimised for a second: 0x0, then a size that is not a number.
    for (std::int64_t t = kMs; t <= 1000 * kMs; t += 16 * kMs) {
        CHECK(hold.grow(0, 0, kPpmm96, t) == doctest::Approx(big));
    }
    CHECK(hold.grow(std::nanf(""), 1080, kPpmm96, 1001 * kMs) == doctest::Approx(big));
    CHECK(hold.grow(1920, -1, kPpmm96, 1002 * kMs) == doctest::Approx(big));
    // Restored to the size it had: the same scale at once, and it stays.
    CHECK(hold.grow(1920, 1080, kPpmm96, 1010 * kMs) == doctest::Approx(big));
    CHECK(hold.grow(1920, 1080, kPpmm96, 1010 * kMs + kUiGrowSettleNs) == doctest::Approx(big));
}

TEST_CASE("ui grow hold: a first call with no size answers without holding anything") {
    UiGrowHold hold;
    CHECK(hold.grow(0, 0, kPpmm96, 0) == doctest::Approx(1.0f));
    // The first real size is taken at once.
    CHECK(hold.grow(1920, 1080, kPpmm96, kMs) == doctest::Approx(1920.0f / 1366.0f));
}
