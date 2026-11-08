// SPDX-License-Identifier: GPL-3.0-or-later
// screen_layout: scale (split from screen_layout_test.cpp).
#include "etude/panel_size.hpp"
#include "etude/screen_layout.hpp"
#include <cmath>
#include <limits>
#include <utility>
#include <vector>
#include <doctest/doctest.h>
#include "sample_looks.hpp"

#include "screen_layout_test_support.hpp"

using namespace etude;
using namespace etude::test;
using namespace screen_layout_test_support;

// ---- The UI's one scale ----

TEST_CASE("ui_scale: the density alone up to 1366x768, grown with the window, capped at 1.6") {
    // At 96 dpi a pixel is a point.
    CHECK(ui_scale(800, 480, kPpmm96) == doctest::Approx(1.0f));
    CHECK(ui_scale(1366, 768, kPpmm96) == doctest::Approx(1.0f));
    CHECK(ui_scale(1920, 1080, kPpmm96) == doctest::Approx(1920.0f / 1366.0f)); // 1.4056
    CHECK(ui_scale(2560, 1440, kPpmm96) == doctest::Approx(kUiMaxGrow));
    CHECK(ui_scale(3840, 2160, kPpmm96) == doctest::Approx(kUiMaxGrow));
    // The narrower axis decides: a wide short window grows by its height.
    CHECK(ui_scale(2560, 1080, kPpmm96) == doctest::Approx(1080.0f / 768.0f));
    CHECK(ui_scale(1366, 1080, kPpmm96) == doctest::Approx(1.0f));
}

TEST_CASE("ui_scale: the clamp boundaries, exactly at and just past 1.0 and 1.6") {
    // Exactly the design size: 1.0; a pixel smaller: still 1.0 (never below the density).
    CHECK(ui_window_grow(1366, 768, kPpmm96) == 1.0f);
    CHECK(ui_window_grow(1365, 767, kPpmm96) == 1.0f);
    // A pixel bigger on both axes: just over 1.
    CHECK(ui_window_grow(1367, 769, kPpmm96) > 1.0f);
    CHECK(ui_window_grow(1367, 769, kPpmm96) == doctest::Approx(1367.0f / 1366.0f));
    // Exactly 1.6 times the design size: 1.6; past it: still 1.6; just under: under 1.6.
    CHECK(ui_window_grow(1366.0f * 1.6f, 768.0f * 1.6f, kPpmm96) == doctest::Approx(1.6f));
    CHECK(ui_window_grow(2200, 1240, kPpmm96) == kUiMaxGrow);
    CHECK(ui_window_grow(2180, 1226, kPpmm96) < kUiMaxGrow);
    CHECK(ui_window_grow(2180, 1226, kPpmm96) == doctest::Approx(2180.0f / 1366.0f));
}

TEST_CASE("ui_scale: a denser screen keeps the size in points; --ui-scale is density") {
    // A 2x screen with a 1366x768-point window: twice the pixels per design point, no growth.
    CHECK(ui_scale(2732, 1536, 2.0f * kPpmm96) == doctest::Approx(2.0f));
    // 1920x1080 points on a 2x screen: the density times the window's growth.
    CHECK(ui_scale(3840, 2160, 2.0f * kPpmm96) == doctest::Approx(2.0f * 1920.0f / 1366.0f));
    // --ui-scale 1.5 at 1920x1080 pixels is a 1280x720-point window at density 1.5.
    CHECK(ui_scale(1920, 1080, 1.5f * kPpmm96) == doctest::Approx(1.5f));
    // Never below the density, so a hit area (46 pt, 12 mm) keeps its physical size.
    for (const float d : {kPpmm96, 5.0f, 7.5f}) {
        for (const float w : {800.0f, 1024.0f, 1366.0f, 1920.0f, 2560.0f, 5120.0f}) {
            CHECK(ui_scale(w, w * 0.5625f, d) >= d / kPpmm96 - 1e-5f);
            CHECK(ui_scale(w, w * 0.5625f, d) <= kUiMaxGrow * d / kPpmm96 + 1e-5f);
        }
    }
    // Bad values: 96 dpi, the design size.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(ui_scale(1366, 768, nan) == doctest::Approx(1.0f));
    CHECK(ui_scale(nan, 768, kPpmm96) == doctest::Approx(1.0f));
    CHECK(ui_scale(0, 0, 0) == doctest::Approx(1.0f));
}

TEST_CASE("ui_scale: floored at kUiMinScale, and the layout and the kit read the same value") {
    // --ui-scale 0.5 at 96 dpi is a density of 0.5: below the floor.
    const float ppmm = 0.5f * kPpmm96;
    CHECK(ui_scale(800, 480, ppmm) == kUiMinScale);
    CHECK(ui_scale(1000, 600, ppmm) == kUiMinScale);
    // Exactly at the floor, and a hair either side.
    CHECK(ui_scale(800, 480, kUiMinScale * kPpmm96) == doctest::Approx(kUiMinScale));
    CHECK(ui_scale(800, 480, 0.74f * kPpmm96) == kUiMinScale);
    CHECK(ui_scale(800, 480, 0.76f * kPpmm96) == doctest::Approx(0.76f));
    // A window that grows lifts a small density over the floor on its own: 0.5 x 1.6 = 0.8.
    CHECK(ui_scale(2200, 1240, ppmm) == doctest::Approx(0.5f * kUiMaxGrow));
    CHECK(ui_scale(2200, 1240, 0.3f * kPpmm96) == kUiMinScale); // 0.3 x 1.6 = 0.48, below it
    // Only the product is floored: the window's growth alone is what it was (800 px at a density
    // of 0.5 is a 1600x960 pt window).
    CHECK(ui_window_grow(800, 480, ppmm) == doctest::Approx(1600.0f / 1366.0f));
    // The top row follows the same floor: 15 mm at the floor's scale, not at the bare density.
    CHECK(compute_layout(800, 480, ppmm, LookSettings{}).top_row.h ==
          std::ceil(kMinTouchTargetMm * kUiMinScale * kDefaultPxPerMm));
}

TEST_CASE("ui_scale: the floor is in points, so a retina window floors at twice the pixels") {
    // A 2x display (pixel density 2) running --ui-scale 0.5: the density is 2 x 0.5 = 1.0, a unit of
    // 1 px = 0.5 pt, under the floor of 0.75 pt = 1.5 px.
    const float ppmm = 2.0f * 0.5f * kPpmm96;
    CHECK(ui_scale(1600, 960, ppmm, 2.0f) == doctest::Approx(2.0f * kUiMinScale));
    CHECK(ui_scale(1600, 960, ppmm, 2.0f) / 2.0f == doctest::Approx(kUiMinScale)); // in points
    // The same window as a 1x display would not be floored by the retina rule.
    CHECK(ui_scale(1600, 960, ppmm, 1.0f) == doctest::Approx(1.0f * ui_window_grow(1600, 960, ppmm)));
    // A 2x display at its own density is above the floor: 2 px = 1 pt.
    CHECK(ui_scale(1600, 960, 2.0f * kPpmm96, 2.0f) == doctest::Approx(2.0f));
    // A bad density counts as 1.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(ui_scale(800, 480, 0.5f * kPpmm96, nan) == kUiMinScale);
    CHECK(ui_scale(800, 480, 0.5f * kPpmm96, 0.0f) == kUiMinScale);
    // The layout reads the same value: the pinned bar's band is design points x that unit.
    PinnedBar pin;
    pin.pinned = true;
    pin.style = BarStyle::Strip;
    pin.has_song = true;
    const ScreenLayout l = compute_layout(1600, 960, ppmm, LookSettings{}, pin, 2.0f);
    REQUIRE(l.bar_fit == BarFit::Full);
    CHECK(l.bar_under.h == std::ceil(bar_under_row_pt(true) * 2.0f * kUiMinScale));
    CHECK(l.top_row.h == std::ceil(kMinTouchTargetMm * 2.0f * kUiMinScale * kDefaultPxPerMm));
}

TEST_CASE("the pinned bar's band is design points x the one scale, below and above the density "
          "the floor lifts, for each bar style at 800x480") {
    for (const float factor : {0.5f, 1.5f}) {
        for (const BarStyle style : {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock}) {
            const float ppmm = factor * kPpmm96;
            PinnedBar pin;
            pin.pinned = true;
            pin.style = style;
            pin.has_song = true;
            const ScreenLayout l = compute_layout(800, 480, ppmm, LookSettings{}, pin);
            CAPTURE(factor);
            CAPTURE(static_cast<int>(style));
            check_invariants(l, 800, 480, "density");
            REQUIRE(l.bar_fit == BarFit::Full);
            CHECK(l.bar_under.h ==
                  std::ceil(bar_under_row_pt(true) * ui_scale(800, 480, ppmm)));
            CHECK(l.note_area.y >= l.bar_under.bottom());
            CHECK(l.top_row.h >= std::ceil(kMinTouchTargetMm * kUiMinScale * kDefaultPxPerMm));
        }
    }
}

TEST_CASE("the status line is the UI's body size on the one scale, not a share of the height") {
    const struct {
        float w;
        float h;
        float font;
    } cases[] = {{800, 480, 16.0f}, {1366, 768, 16.0f}, {1920, 1080, 16.0f * 1920.0f / 1366.0f},
                 {2560, 1440, 16.0f * 1.6f}};
    for (const auto& c : cases) {
        CAPTURE(c.w);
        const ScreenLayout l = compute_layout(c.w, c.h, kPpmm96, LookSettings{});
        CHECK(l.status_visible);
        CHECK(l.status_font_px == doctest::Approx(c.font));
        CHECK(l.status_font_px == doctest::Approx(kStatusFontPt * ui_scale(c.w, c.h, kPpmm96)));
    }
}

// ---- The section band and its lane ----

namespace {

struct ScaledCase {
    float w;
    float h;
    float scale; // the app's --ui-scale: a denser screen
};

std::vector<ScaledCase> band_cases() {
    std::vector<ScaledCase> out;
    for (const auto& [w, h] :
         {std::pair{800.0f, 480.0f}, std::pair{1366.0f, 768.0f}, std::pair{1920.0f, 1080.0f},
          std::pair{1024.0f, 768.0f}, std::pair{2560.0f, 1080.0f}}) {
        for (const float scale : {1.0f, 1.15f, 1.25f, 1.5f}) {
            out.push_back({w, h, scale});
        }
    }
    return out;
}

} // namespace

TEST_CASE("pinned: the band has its own row at 800x480, 1366x768 and 1920x1080, scales 1 to 1.5") {
    for (const ScaledCase& c : band_cases()) {
        for (const BarStyle style : kBarStyles) {
            CAPTURE(c.w);
            CAPTURE(c.h);
            CAPTURE(c.scale);
            const float ppmm = kPpmm96 * c.scale;
            const float u = ui_scale(c.w, c.h, ppmm);
            const ScreenLayout plain = compute_layout(c.w, c.h, ppmm, LookSettings{});
            const ScreenLayout l =
                compute_layout(c.w, c.h, ppmm, LookSettings{}, {true, style, true});
            check_invariants(l, c.w, c.h, "band");
            CHECK(l.lane.empty());
            CHECK(l.lane_fit == LaneFit::Closed);
            switch (l.bar_fit) {
            case BarFit::Full:
                CHECK(l.bar_under.h == std::ceil(kSectionBandPt * u));
                break;
            case BarFit::Slim:
                CHECK(l.bar_under.h == std::ceil(kSlimSectionBandPt * u));
                break;
            case BarFit::Fallback:
                // Laid out unpinned, exactly: the band is never cut and never over the notes.
                CHECK(l.bar_under.empty());
                CHECK(l.note_area.y == plain.note_area.y);
                CHECK(l.note_area.h == plain.note_area.h);
                break;
            case BarFit::NotPinned:
                FAIL("pinned is never reported as not pinned");
                break;
            }
            // The notes start below the band and keep their share.
            CHECK(l.note_area.y >= l.bar_under.bottom());
            CHECK(l.note_area.h >= kMinNoteAreaShare * c.h);
            // The band's controls keep a touch target: the band is never under a hit (46 pt).
            if (!l.bar_under.empty()) {
                CHECK(l.bar_under.h >= kMinHitPt * u - 1e-3f);
            }
        }
    }
    // The three sizes the app is shown at all get the full band at scale 1.
    for (const auto& [w, h] :
         {std::pair{800.0f, 480.0f}, std::pair{1366.0f, 768.0f}, std::pair{1920.0f, 1080.0f}}) {
        const ScreenLayout l =
            compute_layout(w, h, kPpmm96, LookSettings{}, {true, BarStyle::Strip, true});
        CHECK(l.bar_fit == BarFit::Full);
        CHECK(l.bar_under.h == std::ceil(kSectionBandPt * ui_scale(w, h, kPpmm96)));
    }
}

TEST_CASE("the lane opens under the full band, pinned or not, and the notes start below it") {
    bool saw_full = false;
    bool saw_short = false;
    bool saw_no_room = false;
    for (const ScaledCase& c : band_cases()) {
        for (const bool pinned : {false, true}) {
            CAPTURE(c.w);
            CAPTURE(c.h);
            CAPTURE(c.scale);
            CAPTURE(pinned);
            const float ppmm = kPpmm96 * c.scale;
            const float u = ui_scale(c.w, c.h, ppmm);
            const ScreenLayout plain = compute_layout(c.w, c.h, ppmm, LookSettings{});
            const ScreenLayout band_only =
                compute_layout(c.w, c.h, ppmm, LookSettings{}, {pinned, BarStyle::Strip, true});
            const ScreenLayout l = compute_layout(c.w, c.h, ppmm, LookSettings{},
                                                  {pinned, BarStyle::Strip, true, true});
            check_invariants(l, c.w, c.h, "lane");
            const float keyboard_min = std::floor(kMinPinnedKeyboardShare * plain.keyboard.h);
            switch (l.lane_fit) {
            case LaneFit::Full:
                saw_full = true;
                CHECK(l.lane.h == std::ceil(kLanePt * u));
                break;
            case LaneFit::Short:
                saw_short = true;
                CHECK(l.lane.h >= std::ceil(kLaneMinPt * u));
                CHECK(l.lane.h < std::ceil(kLanePt * u));
                break;
            case LaneFit::NoRoom:
                saw_no_room = true;
                // As if the lane was never asked for.
                CHECK(l.lane.empty());
                CHECK(l.bar_under.h == band_only.bar_under.h);
                CHECK(l.note_area.y == band_only.note_area.y);
                CHECK(l.note_area.h == band_only.note_area.h);
                CHECK(l.bar_fit == band_only.bar_fit);
                break;
            case LaneFit::Closed:
                FAIL("a lane asked for with a song is never closed");
                break;
            }
            if (!l.lane.empty()) {
                // The full band over it, then the lane, then the notes; the keyboard keeps its
                // least share and the notes theirs.
                CHECK(l.bar_under.h == std::ceil(kSectionBandPt * u));
                CHECK(l.lane.y == l.bar_under.bottom());
                CHECK(l.note_area.y >= l.lane.bottom());
                CHECK(l.keyboard.h >= keyboard_min);
                CHECK(l.note_area.h >= kMinNoteAreaShare * c.h);
                CHECK(l.bar_fit == (pinned ? BarFit::Full : BarFit::NotPinned));
            }
        }
    }
    // Between the full lane and none the lane shortens: some window height at each width gives it
    // fewer points than `kLanePt` and no fewer than `kLaneMinPt`.
    for (float h = 400.0f; h <= 900.0f && !saw_short; h += 1.0f) {
        const ScreenLayout l = compute_layout(1366.0f, h, kPpmm96, LookSettings{},
                                              {true, BarStyle::Strip, true, true});
        if (l.lane_fit == LaneFit::Short) {
            saw_short = true;
            CHECK(l.lane.h >= std::ceil(kLaneMinPt));
            CHECK(l.lane.h < std::ceil(kLanePt));
        }
    }
    CHECK(saw_full);
    CHECK(saw_short);
    CHECK(saw_no_room);
    // At scale 1 the three sizes all open the lane.
    for (const auto& [w, h] :
         {std::pair{800.0f, 480.0f}, std::pair{1366.0f, 768.0f}, std::pair{1920.0f, 1080.0f}}) {
        CAPTURE(w);
        const ScreenLayout l =
            compute_layout(w, h, kPpmm96, LookSettings{}, {false, BarStyle::Groups, true, true});
        CHECK(l.lane_fit != LaneFit::NoRoom);
        CHECK(l.lane.h >= std::ceil(kLaneMinPt * ui_scale(w, h, kPpmm96)));
    }
}

TEST_CASE(
    "the lane at the edge of its room: exactly its least height opens, a pixel less does not") {
    // Grow the density at 800x480 until the lane no longer fits: Full, then Short, then NoRoom, in
    // that order; a Short lane is never under its least height.
    int stage = 0;
    for (float d = 2.0f; d <= 12.0f; d += 0.02f) {
        const ScreenLayout l =
            compute_layout(800, 480, d, LookSettings{}, {true, BarStyle::Dock, true, true});
        const int now = l.lane_fit == LaneFit::Full ? 0 : l.lane_fit == LaneFit::Short ? 1 : 2;
        CAPTURE(d);
        CHECK(now >= stage);
        stage = std::max(stage, now);
        if (l.lane_fit == LaneFit::Short) {
            CHECK(l.lane.h >= std::ceil(kLaneMinPt * ui_scale(800, 480, d)));
        }
    }
    CHECK(stage == 2);
}

TEST_CASE("no song, no lane; the lane closed leaves the layout as the band alone") {
    for (const ScaledCase& c : band_cases()) {
        const float ppmm = kPpmm96 * c.scale;
        const ScreenLayout plain = compute_layout(c.w, c.h, ppmm, LookSettings{});
        const ScreenLayout no_song =
            compute_layout(c.w, c.h, ppmm, LookSettings{}, {false, BarStyle::Strip, false, true});
        CHECK(no_song.lane_fit == LaneFit::Closed);
        CHECK(no_song.lane.empty());
        CHECK(no_song.bar_under.empty());
        CHECK(no_song.note_area.y == plain.note_area.y);
        const ScreenLayout closed =
            compute_layout(c.w, c.h, ppmm, LookSettings{}, {false, BarStyle::Strip, true, false});
        CHECK(closed.bar_under
                  .empty()); // unpinned: the band hangs under the bar, over nothing reserved
        CHECK(closed.note_area.y == plain.note_area.y);
    }
}

TEST_CASE("from 4:3 to 21:9 an open lane and a pinned band keep every invariant") {
    for (const Case& c : sweep_cases()) {
        for (const bool pinned : {false, true}) {
            const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{},
                                                  {pinned, BarStyle::Strip, true, true});
            check_invariants(l, c.w, c.h, "lane sweep");
            if (!l.lane.empty()) {
                CHECK(l.note_area.y >= l.lane.bottom());
            }
        }
    }
}

TEST_CASE("fit_bar_form: past the minimal form, the pin goes, then the lean form, then the bare one, each at exactly its room") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f, 370.0f, 280.0f, 180.0f};
    CHECK(fit_bar_form(0.0f, 420.0f, w) == BarForm::Minimal); // with its pin
    CHECK(bar_keeps_pin(BarForm::Minimal, w, 420.0f));
    CHECK(fit_bar_form(0.0f, 419.0f, w) == BarForm::Minimal); // without it
    CHECK_FALSE(bar_keeps_pin(BarForm::Minimal, w, 419.0f));
    CHECK(fit_bar_form(0.0f, 370.0f, w) == BarForm::Minimal); // exactly at
    CHECK(fit_bar_form(0.0f, 369.0f, w) == BarForm::Lean);    // just under
    CHECK(fit_bar_form(0.0f, 280.0f, w) == BarForm::Lean);
    CHECK(fit_bar_form(0.0f, 279.0f, w) == BarForm::Bare);
    CHECK(fit_bar_form(0.0f, 180.0f, w) == BarForm::Bare);
    CHECK(fit_bar_form(0.0f, 10.0f, w) == BarForm::Bare); // none fits: the narrowest, cut
    // A caller that offers no leaner form keeps the minimal one, as before.
    const BarFormWidths four{720.0f, 640.0f, 560.0f, 420.0f};
    CHECK(fit_bar_form(0.0f, 10.0f, four) == BarForm::Minimal);
    // Lean offered, bare not: lean is the narrowest.
    BarFormWidths no_bare = w;
    no_bare.bare = 0.0f;
    CHECK(fit_bar_form(0.0f, 10.0f, no_bare) == BarForm::Lean);
    // A caller that offers the least form (no Flow/Wait chip) too: it takes the room where even the
    // bare form does not fit, at exactly its room, and is the narrowest.
    const BarFormWidths least{720.0f, 640.0f, 560.0f, 420.0f, 370.0f, 280.0f, 180.0f, 120.0f};
    CHECK(fit_bar_form(0.0f, 180.0f, least) == BarForm::Bare);
    CHECK(fit_bar_form(0.0f, 179.0f, least) == BarForm::Least);
    CHECK(fit_bar_form(0.0f, 10.0f, least) == BarForm::Least);
    CHECK(fit_bar_form(0.0f, 10.0f, w) == BarForm::Bare); // not offered: the bare form stays
    CHECK_FALSE(bar_keeps_pin(BarForm::Least, least, 1000.0f));
    CHECK_FALSE(bar_has_replay(BarForm::Least));
    CHECK_FALSE(bar_has_skips(BarForm::Least));
    CHECK_FALSE(bar_has_mode(BarForm::Least));
    CHECK(bar_has_mode(BarForm::Bare));
    // What each form keeps.
    CHECK(bar_has_replay(BarForm::Minimal));
    CHECK_FALSE(bar_has_replay(BarForm::Lean));
    CHECK(bar_has_skips(BarForm::Lean));
    CHECK_FALSE(bar_has_skips(BarForm::Bare));
    CHECK(bar_look_shortcuts(BarForm::Full) == 2);
    CHECK(bar_look_shortcuts(BarForm::Compact) == 1);
    CHECK(bar_look_shortcuts(BarForm::Tight) == 0);
    CHECK_FALSE(bar_has_metronome_toggle(BarForm::Lean));
    CHECK_FALSE(bar_chip_has_badge(BarForm::Minimal));
    CHECK(bar_chip_has_badge(BarForm::Tight));
}

TEST_CASE("ui_scale: a growth given stands in for the window's own, clamped to 1..1.6") {
    // A 1920x1080 window drawn with the growth a 1366x768 one had, as during a resize.
    CHECK(ui_scale(1920, 1080, kPpmm96, 1.0f, 1.0f) == doctest::Approx(1.0f));
    CHECK(ui_scale(1366, 768, kPpmm96, 1.0f, 1.4f) == doctest::Approx(1.4f));
    CHECK(ui_scale(1366, 768, kPpmm96, 1.0f, 3.0f) == doctest::Approx(kUiMaxGrow));
    CHECK(ui_scale(1920, 1080, kPpmm96, 1.0f, 0.5f) == doctest::Approx(1.0f));
    // None given (0, negative, NaN): the window's own.
    CHECK(ui_scale(1920, 1080, kPpmm96, 1.0f, 0.0f) == doctest::Approx(1920.0f / 1366.0f));
    CHECK(ui_scale(1920, 1080, kPpmm96, 1.0f, -1.0f) == doctest::Approx(1920.0f / 1366.0f));
    CHECK(ui_scale(1920, 1080, kPpmm96, 1.0f, std::numeric_limits<float>::quiet_NaN()) ==
          doctest::Approx(1920.0f / 1366.0f));
    // The layout's top row uses the same growth.
    const LookSettings look;
    const ScreenLayout held = compute_layout(1920, 1080, kPpmm96, look, {}, 1.0f, 1.0f);
    const ScreenLayout small = compute_layout(1366, 768, kPpmm96, look, {}, 1.0f);
    const ScreenLayout own = compute_layout(1920, 1080, kPpmm96, look, {}, 1.0f);
    CHECK(held.top_row.h == small.top_row.h);
    CHECK(own.top_row.h > held.top_row.h);
}
