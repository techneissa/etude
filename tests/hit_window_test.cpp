// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/hit_window.hpp"
#include "etude/session_state.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("the presets are 40, 80 and 150 ms") {
    CHECK(hit_window_ms(HitWindowPreset::Strict) == 40);
    CHECK(hit_window_ms(HitWindowPreset::Normal) == 80);
    CHECK(hit_window_ms(HitWindowPreset::Relaxed) == 150);
}

TEST_CASE("Normal is the default window") {
    CHECK(AppSettings{}.hit_window_ms == kHitWindowNormalMs);
    CHECK(hit_window_preset_index(AppSettings{}.hit_window_ms) ==
          static_cast<int>(HitWindowPreset::Normal));
}

TEST_CASE("a window equal to a preset selects it, any other selects none") {
    CHECK(hit_window_preset_index(40) == static_cast<int>(HitWindowPreset::Strict));
    CHECK(hit_window_preset_index(80) == static_cast<int>(HitWindowPreset::Normal));
    CHECK(hit_window_preset_index(150) == static_cast<int>(HitWindowPreset::Relaxed));
    for (int ms : {20, 39, 41, 79, 81, 149, 151, 250}) CHECK(hit_window_preset_index(ms) == -1);
}

TEST_CASE("every preset sits inside the slider's range") {
    for (int ms : kHitWindowPresetMs) {
        CHECK(ms >= 20);
        CHECK(ms <= 250);
    }
}
