// SPDX-License-Identifier: GPL-3.0-or-later
// Output pixels per millimetre from the panel's size, the Settings override and the UI scale
// (app/panel_scale.hpp), and what the UI's design point comes to in millimetres.
#include "panel_scale.hpp"

#include "etude/panel_size.hpp"
#include "etude/screen_layout.hpp"
#include "etude/session_state.hpp"

#include <utility>

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

namespace {
constexpr PanelMm kPi{344.0f, 194.0f};
constexpr PanelMm kSeven{154.0f, 86.0f};

// A headless run: the window is the panel.
PanelScale headless(PanelMm mm) {
    PanelScale s;
    s.mm = mm;
    s.headless = true;
    return s;
}
} // namespace

TEST_CASE("panel scale: a headless window stands for the whole panel") {
    const PanelScale pi = headless(kPi);
    CHECK(pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 1.0f) ==
          doctest::Approx(5.58f).epsilon(0.01));
    CHECK(pi.px_per_mm(1366.0f, 768.0f, 1.0f, 0, 1.0f) == doctest::Approx(3.97f).epsilon(0.01));
    CHECK(headless(kSeven).px_per_mm(800.0f, 480.0f, 1.0f, 0, 1.0f) ==
          doctest::Approx(5.38f).epsilon(0.01));
}

TEST_CASE("panel scale: a headless run ignores the Settings override (the flag is its panel)") {
    const PanelScale pi = headless(kPi);
    CHECK(pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 154, 1.0f) ==
          pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 1.0f));
}

TEST_CASE("panel scale: a window on a real display uses the display's points per millimetre") {
    PanelScale mac;
    mac.mm = {302.0f, 196.0f};
    mac.display = {3024.0f, 1964.0f, 1512.0f}; // a retina laptop: 1512 points over 302 mm
    // The window's output pixels per point is 2: 1512 / 302 x 2 = 10 pixels per mm.
    CHECK(mac.px_per_mm(2560.0f, 1440.0f, 2.0f, 0, 1.0f) == doctest::Approx(10.01f).epsilon(0.01));
    // A window with no high pixel density: 5 pixels per mm.
    CHECK(mac.px_per_mm(1280.0f, 720.0f, 1.0f, 0, 1.0f) == doctest::Approx(5.0f).epsilon(0.01));
}

TEST_CASE("panel scale: nothing known is 96 dpi at the window's pixel density") {
    const PanelScale none;
    CHECK(none.px_per_mm(1280.0f, 720.0f, 1.0f, 0, 1.0f) == doctest::Approx(kFallbackPxPerMm));
    CHECK(none.px_per_mm(2560.0f, 1440.0f, 2.0f, 0, 1.0f) ==
          doctest::Approx(2.0f * kFallbackPxPerMm));
    CHECK(none.px_per_mm(1280.0f, 720.0f, 0.0f, 0, 1.0f) == doctest::Approx(kFallbackPxPerMm));
}

TEST_CASE("panel scale: the Settings width overrides the panel's own size") {
    PanelScale wrong; // a real display whose panel lies about its size
    wrong.mm = {160.0f, 90.0f};
    wrong.display = {1920.0f, 1080.0f, 1920.0f};
    CHECK(wrong.px_per_mm(1920.0f, 1080.0f, 1.0f, 344, 1.0f) ==
          doctest::Approx(1920.0f / 344.0f).epsilon(0.002));
    // The override works with nothing known too.
    const PanelScale none;
    CHECK(none.px_per_mm(1920.0f, 1080.0f, 1.0f, 344, 1.0f) ==
          doctest::Approx(1920.0f / 344.0f).epsilon(0.002));
    // A width under the least is no override.
    CHECK(wrong.px_per_mm(1920.0f, 1080.0f, 1.0f, kMinScreenWidthMm - 1, 1.0f) ==
          doctest::Approx(1920.0f / 160.0f).epsilon(0.002));
}

TEST_CASE("panel scale: the UI scale multiplies on top and never shrinks") {
    const PanelScale pi = headless(kPi);
    const float base = pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 1.0f);
    CHECK(pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 1.5f) == doctest::Approx(base * 1.5f));
    CHECK(pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 0.5f) == doctest::Approx(base));
    CHECK(pi.px_per_mm(1920.0f, 1080.0f, 1.0f, 0, 0.0f) == doctest::Approx(base));
}

TEST_CASE("panel scale: a design point is the same physical size on every panel") {
    // The UI's unit is `ui_scale` pixels per design point: the density (px per mm over 96 dpi's)
    // times the window's growth (1 up to 1366x768 points). A hit area of 12 mm is therefore the
    // same number of design points everywhere, and never under 12 mm.
    const float hit_pt = kMinHitMm * kDefaultPxPerMm;
    for (const auto& c : {std::pair{kPi, std::pair{1920.0f, 1080.0f}},
                          std::pair{kPi, std::pair{1366.0f, 768.0f}},
                          std::pair{kSeven, std::pair{800.0f, 480.0f}}}) {
        const PanelScale s = headless(c.first);
        const float ppmm = s.px_per_mm(c.second.first, c.second.second, 1.0f, 0, 1.0f);
        const float u = ui_scale(c.second.first, c.second.second, ppmm);
        CHECK(hit_pt * u / ppmm >= kMinHitMm - 0.001f);
    }
}
