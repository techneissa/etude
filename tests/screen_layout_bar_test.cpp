// SPDX-License-Identifier: GPL-3.0-or-later
// screen_layout: bar (split from screen_layout_test.cpp).
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

// ---- The control bar's form -----------------------------------------------------------------

TEST_CASE("bar form: the richest form that fits the room, boundaries included") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f};
    CHECK(fit_bar_form(1000.0f, 1000.0f, w) == BarForm::Full);
    CHECK(fit_bar_form(720.0f, 720.0f, w) == BarForm::Full); // exactly at
    CHECK(fit_bar_form(719.0f, 719.0f, w) == BarForm::Compact); // just under
    CHECK(fit_bar_form(640.0f, 640.0f, w) == BarForm::Compact);
    CHECK(fit_bar_form(639.0f, 639.0f, w) == BarForm::Tight);
    CHECK(fit_bar_form(560.0f, 560.0f, w) == BarForm::Tight);
    CHECK(fit_bar_form(559.0f, 559.0f, w) == BarForm::Minimal);
    CHECK(fit_bar_form(420.0f, 420.0f, w) == BarForm::Minimal);
    CHECK(fit_bar_form(100.0f, 100.0f, w) == BarForm::Minimal); // none fits: the bar is cut
    CHECK(fit_bar_form(0.0f, 0.0f, w) == BarForm::Minimal);
}

TEST_CASE("bar form: the full form needs the comfortable room, the others the least") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f};
    // 740 px of room, but a comfortable status text leaves only 700: the full bar does not fit.
    CHECK(fit_bar_form(700.0f, 740.0f, w) == BarForm::Compact);
    CHECK(fit_bar_form(720.0f, 740.0f, w) == BarForm::Full);
    CHECK(fit_bar_form(0.0f, 600.0f, w) == BarForm::Tight);
    CHECK(fit_bar_form(0.0f, 300.0f, w) == BarForm::Minimal);
}

TEST_CASE("bar form: the metronome toggle stays until only the minimal form is left") {
    CHECK(bar_has_metronome_toggle(BarForm::Full));
    CHECK(bar_has_metronome_toggle(BarForm::Compact));
    CHECK(bar_has_metronome_toggle(BarForm::Tight));
    CHECK_FALSE(bar_has_metronome_toggle(BarForm::Minimal));
}

TEST_CASE("bar form: bad values choose the minimal form") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f};
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const float inf = std::numeric_limits<float>::infinity();
    CHECK(fit_bar_form(nan, nan, w) == BarForm::Minimal);
    CHECK(fit_bar_form(1000.0f, nan, w) == BarForm::Minimal);
    CHECK(fit_bar_form(-5.0f, -5.0f, w) == BarForm::Minimal);
    CHECK(fit_bar_form(inf, inf, w) == BarForm::Minimal);
    // A form whose width is not a usable number is skipped, not chosen.
    CHECK(fit_bar_form(1000.0f, 1000.0f, BarFormWidths{nan, 640.0f, 560.0f, 420.0f}) ==
          BarForm::Compact);
    CHECK(fit_bar_form(1000.0f, 1000.0f, BarFormWidths{0.0f, 0.0f, 0.0f, 0.0f}) ==
          BarForm::Minimal);
    CHECK(fit_bar_form(1000.0f, 1000.0f, BarFormWidths{-3.0f, 640.0f, 560.0f, 420.0f}) ==
          BarForm::Compact);
    CHECK(fit_bar_form(nan, 1000.0f, w) == BarForm::Compact); // no comfortable room: not full
}

TEST_CASE("bar form: the choice never gets richer as the window narrows") {
    const BarFormWidths w{720.0f, 640.0f, 560.0f, 420.0f};
    int last = 0;
    for (int room = 1200; room >= 0; room -= 7) {
        const auto r = static_cast<float>(room);
        const int form = static_cast<int>(fit_bar_form(r - 60.0f, r, w));
        CHECK(form >= last);
        last = form;
    }
}

namespace {

// 800x480 up to 21:9 and beyond, on the densities the app meets.
std::vector<Case> pinned_cases() {
    return {
        {800, 480, kPpmm96},  {800, 480, 7.5f},      {1024, 600, kPpmm96},  {1280, 720, kPpmm96},
        {1366, 768, kPpmm96}, {1920, 1080, kPpmm96}, {2560, 1080, kPpmm96}, {3440, 1440, kPpmm96},
        {2560, 1080, 5.0f},   {3440, 1440, 5.5f},    {1280, 800, 6.0f},     {1024, 440, kPpmm96},
        {1600, 960, kPpmm96},
    };
}

} // namespace

TEST_CASE("a bar that is not pinned, or pinned with no song, leaves the layout as it was") {
    for (const Case& c : pinned_cases()) {
        CAPTURE(c.w);
        CAPTURE(c.h);
        const ScreenLayout plain = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        CHECK(plain.bar_under.empty());
        for (const BarStyle style : kBarStyles) {
            for (const bool has_song : {false, true}) {
                const ScreenLayout unpinned =
                    compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {false, style, has_song});
                CHECK(unpinned.note_area.y == plain.note_area.y);
                CHECK(unpinned.bar_under.empty());
            }
            const ScreenLayout no_song =
                compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {true, style, false});
            CHECK(no_song.bar_under.empty()); // the bar fits in the top row
            CHECK(no_song.note_area.y == plain.note_area.y);
        }
    }
}

TEST_CASE("a pinned bar with a song gets its band under the top row, in all three styles") {
    for (const Case& c : pinned_cases()) {
        for (const BarStyle style : kBarStyles) {
            CAPTURE(c.w);
            CAPTURE(c.h);
            CAPTURE(c.ppmm);
            CAPTURE(static_cast<int>(style));
            const ScreenLayout plain = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
            const ScreenLayout l =
                compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {true, style, true});
            check_invariants(l, c.w, c.h, "pinned");
            const float want = std::ceil(bar_under_row_pt(true) * ui_scale(c.w, c.h, c.ppmm));
            CHECK(l.bar_under.y == l.top_row.bottom());
            CHECK(l.bar_under.w == c.w);
            CHECK(l.bar_under.h == want); // there is room at every one of these sizes
            // The notes start below the whole bar, and so does the chord label.
            CHECK(l.note_area.y >= l.top_row.bottom() + want);
            CHECK(l.chord_label.y == l.bar_under.bottom());
            CHECK(l.keyboard.bottom() == c.h);
            CHECK(l.note_area.h >= kMinNoteAreaShare * c.h);
            // What the bar took came out of the keyboard and the notes, not the top row.
            CHECK(l.top_row.h == plain.top_row.h);
            CHECK(l.top_row.h + l.bar_under.h + l.chord_label.h + l.note_area.h + l.keyboard.h ==
                  c.h);
        }
    }
}

TEST_CASE("the section band is 52 pt, 50 pt slim, in every bar style, and nothing without a song") {
    CHECK(bar_under_row_pt(true) == 52.0f);
    // A touch target (46 pt, 12 mm) and room for the bar's buttons above to keep their dead gap.
    CHECK(bar_under_row_pt(true, true) == 50.0f);
    CHECK(bar_under_row_pt(true, true) >= kMinHitPt);
    CHECK(bar_under_row_pt(false) == 0.0f);
    CHECK(bar_under_row_pt(false, true) == 0.0f);
    for (const Case& c : pinned_cases()) {
        float first = -1.0f;
        for (const BarStyle style : kBarStyles) {
            const ScreenLayout l =
                compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {true, style, true});
            if (first < 0.0f) {
                first = l.bar_under.h;
            }
            CHECK(l.bar_under.h == first); // a row of its own, the same under every style
        }
    }
}

namespace {

float band_px(BarStyle /*style*/, bool slim, const Case& c) {
    return std::ceil(bar_under_row_pt(true, slim) * ui_scale(c.w, c.h, c.ppmm));
}

// 800x480 to 21:9, at densities from a desktop monitor up to 16 px/mm (a dense small panel).
std::vector<Case> dense_cases() {
    std::vector<Case> out;
    const float sizes[][2] = {{800, 480},   {1024, 600},  {1280, 720}, {1920, 1080},
                              {2560, 1080}, {3440, 1440}, {1000, 480}, {1120, 480}};
    for (const auto& sz : sizes) {
        for (float d = 2.0f; d <= 16.001f; d += 0.1f) { // fine: the slim band is 2 pt under the full one
            out.push_back({sz[0], sz[1], d});
        }
    }
    return out;
}

} // namespace

TEST_CASE("the pinned band is never cut: whole, slim, or not pinned at all, up to 16 px/mm") {
    bool saw_full = false;
    bool saw_slim = false;
    bool saw_fallback = false;
    for (const Case& c : dense_cases()) {
        for (const BarStyle style : kBarStyles) {
            CAPTURE(c.w);
            CAPTURE(c.h);
            CAPTURE(c.ppmm);
            CAPTURE(static_cast<int>(style));
            const LookSettings look;
            const ScreenLayout plain = compute_layout(c.w, c.h, c.ppmm, look);
            const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, look, {true, style, true});
            check_invariants(l, c.w, c.h, "pinned, dense");
            const float full = band_px(style, false, c);
            const float slim = band_px(style, true, c);
            const float keyboard_min = std::floor(kMinPinnedKeyboardShare * plain.keyboard.h);
            // What the top row leaves of the 60 percent that is not the note area's.
            const float budget = std::floor(c.h * (1.0f - kMinNoteAreaShare)) - plain.top_row.h;
            switch (l.bar_fit) {
            case BarFit::Full:
                saw_full = true;
                CHECK(l.bar_under.h == full);
                CHECK(full + keyboard_min <= budget);
                break;
            case BarFit::Slim:
                saw_slim = true;
                CHECK(l.bar_under.h == slim);
                CHECK(l.bar_under.h < full);
                // The full band was tried first and did not fit with a minimum keyboard.
                CHECK(full + keyboard_min > budget);
                CHECK(slim + keyboard_min <= budget);
                break;
            case BarFit::Fallback:
                saw_fallback = true;
                CHECK(l.bar_under.empty());
                CHECK(slim + keyboard_min > budget); // not even the slim form fits
                // The unpinned layout, exactly.
                CHECK(l.top_row.h == plain.top_row.h);
                CHECK(l.chord_label.y == plain.chord_label.y);
                CHECK(l.chord_label.h == plain.chord_label.h);
                CHECK(l.note_area.y == plain.note_area.y);
                CHECK(l.note_area.h == plain.note_area.h);
                CHECK(l.keyboard.y == plain.keyboard.y);
                CHECK(l.keyboard.h == plain.keyboard.h);
                break;
            case BarFit::NotPinned:
                FAIL("a pinned bar with a song is never reported as not pinned");
                break;
            }
            if (l.bar_fit != BarFit::Fallback) {
                // Whole or slim: the keyboard keeps at least 40 percent of what it would have, the
                // notes their share, and the notes start below the band.
                CHECK(l.keyboard.h >= keyboard_min);
                CHECK(l.note_area.h >= kMinNoteAreaShare * c.h);
                CHECK(l.note_area.y >= l.bar_under.bottom());
                CHECK(l.keyboard.h > 0.0f);
            }
        }
    }
    CHECK(saw_full);
    CHECK(saw_slim);
    CHECK(saw_fallback);
}

TEST_CASE("800x480 at 16 px/mm: the band no longer fits and falls back, never cut") {
    for (const BarStyle style : kBarStyles) {
        const ScreenLayout l = compute_layout(800, 480, 16.0f, LookSettings{}, {true, style, true});
        CAPTURE(static_cast<int>(style));
        CHECK(l.keyboard.h > 0.0f);
        check_invariants(l, 800, 480, "800x480 at 16 px/mm");
        CHECK(l.bar_fit == BarFit::Fallback);
        CHECK(l.bar_under.empty());
    }
}

TEST_CASE("the slim form comes before the fallback, and the full form before the slim") {
    // Walk the density up at 800x480: full first, then slim, then the fallback, in that order
    // (a denser panel only makes the top row and the band bigger).
    for (const BarStyle style : kBarStyles) {
        int stage = 0; // 0 full, 1 slim, 2 fallback
        for (float d = 2.0f; d <= 16.0f; d += 0.25f) {
            const ScreenLayout l = compute_layout(800, 480, d, LookSettings{}, {true, style, true});
            const int now = l.bar_fit == BarFit::Full ? 0 : l.bar_fit == BarFit::Slim ? 1 : 2;
            CAPTURE(d);
            CHECK(now >= stage);
            stage = std::max(stage, now);
        }
        CHECK(stage == 2); // every style has fallen back by 16 px/mm
    }
}

TEST_CASE("pinned with no song never falls back: there is no band to fit") {
    for (const Case& c : dense_cases()) {
        const ScreenLayout l =
            compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {true, BarStyle::Strip, false});
        CHECK(l.bar_fit == BarFit::Full);
        CHECK(l.bar_under.empty());
    }
}

TEST_CASE("an unpinned bar is reported as not pinned") {
    CHECK(compute_layout(800, 480, 3.78f, LookSettings{}).bar_fit == BarFit::NotPinned);
    CHECK(compute_layout(800, 480, 3.78f, LookSettings{}, {false, BarStyle::Dock, true}).bar_fit ==
          BarFit::NotPinned);
}

TEST_CASE("the keyboard share the pinned bar leaves is exactly the named minimum") {
    // At the edge: find the densest 800x480 where the full band fits and check the keyboard there
    // is at least the minimum; one step denser must not leave a keyboard under it either.
    for (const BarStyle style : kBarStyles) {
        for (float d = 2.0f; d <= 16.0f; d += 0.05f) {
            const ScreenLayout plain = compute_layout(800, 480, d, LookSettings{});
            const ScreenLayout l = compute_layout(800, 480, d, LookSettings{}, {true, style, true});
            if (l.bar_fit == BarFit::Fallback) {
                continue;
            }
            CAPTURE(d);
            CHECK(l.keyboard.h >= std::floor(kMinPinnedKeyboardShare * plain.keyboard.h));
        }
    }
    static_assert(kMinPinnedKeyboardShare == 0.40f);
}

TEST_CASE("pinned bar: every layout in the sweep keeps the invariants") {
    for (const Case& c : sweep_cases()) {
        for (const BarStyle style : kBarStyles) {
            const ScreenLayout l =
                compute_layout(c.w, c.h, c.ppmm, LookSettings{}, {true, style, true});
            check_invariants(l, c.w, c.h, "pinned sweep");
        }
    }
}

TEST_CASE("bar room: the status area less what the text keeps and the gap, and it fits split_top_row") {
    LookSettings look;
    for (const auto& [w, h] : {std::pair{800.0f, 480.0f}, std::pair{1600.0f, 960.0f},
                               std::pair{1024.0f, 600.0f}}) {
        const ScreenLayout l = compute_layout(w, h, kDefaultPxPerMm, look);
        CAPTURE(w);
        CHECK(bar_room(l, 0.0f) == l.status_text.w);
        const float room = bar_room(l, 150.0f);
        CHECK(room < l.status_text.w);
        CHECK(room == doctest::Approx(l.status_text.w - 150.0f -
                                      std::round(l.top_row.h * kTopRowMarginShare)));
        // A bar exactly that wide is not cut and the status text keeps what it was asked to.
        const TopRowSplit s = split_top_row(l, room, 150.0f, false);
        CHECK(s.bar.w == doctest::Approx(std::floor(room)));
        CHECK(s.status_text.w >= 150.0f - 1.0f);
    }
    const ScreenLayout l = compute_layout(800.0f, 480.0f, kDefaultPxPerMm, look);
    CHECK(bar_room(l, 100000.0f) == 0.0f);
    CHECK(bar_room(l, -5.0f) == l.status_text.w);
    CHECK(bar_room(l, std::numeric_limits<float>::quiet_NaN()) == l.status_text.w);
    CHECK(bar_room(ScreenLayout{}, 100.0f) == 0.0f);
}
