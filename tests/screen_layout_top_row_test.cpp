// SPDX-License-Identifier: GPL-3.0-or-later
// screen_layout: top_row (split from screen_layout_test.cpp).
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

// ---- The top row: status text, gear, and the bar when it is revealed ----

namespace {

std::vector<Case> wide_cases() {
    // 800x480 up to 21:9, at several densities.
    std::vector<Case> out;
    for (float h : {480.0f, 600.0f, 720.0f, 900.0f, 1080.0f, 1440.0f}) {
        for (int step = 0; step <= 8; ++step) {
            const float aspect =
                4.0f / 3.0f + (21.0f / 9.0f - 4.0f / 3.0f) * static_cast<float>(step) / 8.0f;
            const float w = std::max(800.0f, std::round(h * aspect));
            for (float d : {kPpmm96, 5.0f, 6.4f}) {
                out.push_back({w, h, d});
            }
        }
    }
    return out;
}

} // namespace

TEST_CASE("the top row is 15 mm tall times the UI's growth from 800x480 to 21:9, and the stack "
          "under it holds") {
    for (const Case& c : wide_cases()) {
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        check_invariants(l, c.w, c.h, "wide");
        CHECK(l.top_row.h ==
              std::ceil(kMinTouchTargetMm * c.ppmm * ui_window_grow(c.w, c.h, c.ppmm)));
        CHECK(l.top_row.h >= std::ceil(kMinTouchTargetMm * c.ppmm));
        CHECK(l.top_row.w == c.w);
        CHECK(l.chord_label.y == l.top_row.bottom());
        CHECK(l.note_area.y == l.chord_label.bottom());
        CHECK(l.keyboard.y == l.note_area.bottom());
        CHECK(l.keyboard.bottom() == c.h); // nothing below the keyboard
        CHECK(l.status_visible);
    }
    // The documented floor: 800x480 on a 5 px/mm panel (a 7-inch touch screen).
    CHECK(compute_layout(800, 480, 5.0f, LookSettings{}).top_row.h >= 75.0f);
}

TEST_CASE("the top row is capped in windows too short for a 15 mm row") {
    const ScreenLayout l = compute_layout(1280, 150, kPpmm96, LookSettings{});
    check_invariants(l, 1280, 150, "short");
    CHECK(l.top_row.h <= kMaxTopRowShare * 150.0f);
}

TEST_CASE("the gear is a square in the top-right corner and the status area never reaches it") {
    for (const Case& c : wide_cases()) {
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        CHECK(l.gear.w == l.top_row.h);
        CHECK(l.gear.h == l.top_row.h);
        CHECK(l.gear.right() == c.w);
        CHECK(l.gear.y == 0.0f);
        CHECK(l.status_text.x > 0.0f);
        CHECK(l.status_text.right() < l.gear.x);
        CHECK_FALSE(overlap(l.status_text, l.gear));
    }
}

TEST_CASE("the library and Learn buttons are squares at the left end, side by side, clear of the "
          "gear, on a row of kMenuBelowRowPt points or more") {
    int wide_rows = 0;
    for (const Case& c : wide_cases()) {
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        INFO(c.w << "x" << c.h);
        if (c.w < kMenuBelowRowPt * ui_scale(c.w, c.h, c.ppmm)) {
            continue; // a narrow row: the menu button's (next test)
        }
        ++wide_rows;
        CHECK(l.menu.empty());
        CHECK(l.library.x == 0.0f);
        CHECK(l.library.w == l.top_row.h);
        CHECK(l.library.h == l.top_row.h);
        CHECK(l.learn.x == l.library.right());
        CHECK(l.learn.y == 0.0f);
        CHECK(l.learn.w == l.top_row.h);
        CHECK(l.learn.h == l.top_row.h);
        CHECK_FALSE(overlap(l.library, l.learn));
        CHECK_FALSE(overlap(l.learn, l.gear));
        CHECK(l.learn.right() <= l.gear.x);
    }
    CHECK(wide_rows > 0);
}

TEST_CASE("a row under kMenuBelowRowPt points has the menu button in the library's place, and no "
          "destination is left without a way to it") {
    int narrow_rows = 0;
    for (const Case& c : wide_cases()) {
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        INFO(c.w << "x" << c.h << " at " << c.ppmm);
        if (c.w >= kMenuBelowRowPt * ui_scale(c.w, c.h, c.ppmm)) {
            continue;
        }
        ++narrow_rows;
        // The menu stands where the library's button would, a square as tall as the row; the
        // library and Learn have no button of their own, because the menu holds them.
        CHECK_FALSE(l.menu.empty());
        CHECK(l.menu.x == 0.0f);
        CHECK(l.menu.y == 0.0f);
        CHECK(l.menu.w == l.top_row.h);
        CHECK(l.menu.h == l.top_row.h);
        CHECK(l.library.empty());
        CHECK(l.learn.empty());
        CHECK_FALSE(overlap(l.menu, l.gear));
        CHECK(l.menu.right() <= l.gear.x);
        CHECK(l.status_text.x > 0.0f);
    }
    CHECK(narrow_rows > 0);
    // The 7 inch panel's 800x480 is 584 points wide.
    const ScreenLayout seven = compute_layout(800, 480, kPpmm96 * 1.37f, LookSettings{});
    CHECK_FALSE(seven.menu.empty());
    CHECK(seven.library.empty());
    CHECK(seven.learn.empty());
    // One point under and exactly at the threshold: the same rule at either side of it.
    const float ppmm = kPpmm96;
    const float edge = std::ceil(kMenuBelowRowPt * ui_scale(640, 480, ppmm));
    CHECK_FALSE(compute_layout(edge - 1.0f, 480, ppmm, LookSettings{}).menu.empty());
    CHECK(compute_layout(edge, 480, ppmm, LookSettings{}).menu.empty());
    CHECK_FALSE(compute_layout(edge, 480, ppmm, LookSettings{}).library.empty());
}

TEST_CASE("a window with room for one button and the gear has the menu button, not the library's") {
    const ScreenLayout narrow = compute_layout(150, 480, kPpmm96, LookSettings{});
    CHECK_FALSE(narrow.menu.empty());
    CHECK(narrow.library.empty());
    CHECK(narrow.learn.empty());
}

TEST_CASE("the note area gets the height the old status line had") {
    // Before the top row, a 1280x720 window at 96 dpi had a 57 px control bar and a 26 px status
    // line under it. Now the status is in the top row and the notes start 26 px higher.
    const ScreenLayout l = compute_layout(1280, 720, kPpmm96, LookSettings{});
    CHECK(l.top_row.h == 57.0f);
    const float old_note_h = 720.0f - 57.0f - 26.0f - l.chord_label.h - l.keyboard.h;
    CHECK(l.note_area.h == old_note_h + 26.0f);
}

TEST_CASE("a revealed bar takes the right end of the status area and the text shrinks") {
    for (const Case& c : wide_cases()) {
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        for (float bar_w : {100.0f, 400.0f, 700.0f, 5000.0f}) {
            const TopRowSplit s = split_top_row(l, bar_w, 0.0f, false);
            INFO("bar " << bar_w << " in " << c.w << "x" << c.h);
            CHECK(s.bar.w == std::min(bar_w, l.status_text.w));
            CHECK(s.bar.right() == l.status_text.right()); // next to the gear
            CHECK(s.bar.right() < l.gear.x);
            CHECK(s.bar.y == l.top_row.y);
            CHECK(s.bar.h == l.top_row.h);
            CHECK_FALSE(overlap(s.bar, l.gear));
            CHECK_FALSE(overlap(s.bar, s.status_text));
            if (!s.status_text.empty()) {
                CHECK(s.status_text.x == l.status_text.x);
                CHECK(s.status_text.right() < s.bar.x);
                CHECK(s.status_text.w < l.status_text.w);
            }
        }
    }
}

TEST_CASE(
    "a centred bar sits in the middle, moves right to keep status text, never past the gear") {
    const ScreenLayout l = compute_layout(1280, 720, kPpmm96, LookSettings{});
    const TopRowSplit mid = split_top_row(l, 400.0f, 0.0f, true);
    CHECK(mid.bar.x == 440.0f);
    CHECK(mid.status_text.right() < mid.bar.x);
    // Asked to keep 600 px of status text: the bar moves right of it.
    const TopRowSplit kept = split_top_row(l, 400.0f, 600.0f, true);
    CHECK(kept.bar.x > mid.bar.x);
    CHECK(kept.status_text.w >= 600.0f);
    CHECK(kept.bar.right() <= l.status_text.right());
    // Asked for more than fits: the bar stops at the gear and the text gives way.
    const TopRowSplit tight = split_top_row(l, 400.0f, 5000.0f, true);
    CHECK(tight.bar.right() == l.status_text.right());
    CHECK(tight.bar.w == 400.0f);
    CHECK_FALSE(overlap(tight.bar, tight.status_text));
}

TEST_CASE("no bar, or a bad width, leaves the status area whole") {
    const ScreenLayout l = compute_layout(1280, 720, kPpmm96, LookSettings{});
    for (float bad : {0.0f, -10.0f, std::numeric_limits<float>::quiet_NaN()}) {
        const TopRowSplit s = split_top_row(l, bad, 0.0f, false);
        CHECK(s.bar.empty());
        CHECK(s.status_text.x == l.status_text.x);
        CHECK(s.status_text.w == l.status_text.w);
    }
    // A bar as wide as the whole area leaves no status text at all, rather than a sliver under it.
    const TopRowSplit full = split_top_row(l, l.status_text.w, 0.0f, false);
    CHECK(full.status_text.empty());
    CHECK(full.bar.x == l.status_text.x);
}

// ---- the scrubber's touch band ------------------------------------------------------------------

TEST_CASE("scrubber band: centred on the track, full height, when the row is clear of it") {
    const HitBand b = scrubber_hit_band(100.0f, 44.0f, 50.0f, 150.0f);
    CHECK(b.top == doctest::Approx(78.0f));
    CHECK(b.bottom == doctest::Approx(122.0f));
}

TEST_CASE("scrubber band: where it would reach into the row it moves down, keeping its height") {
    const HitBand b = scrubber_hit_band(60.0f, 44.0f, 50.0f, 150.0f);
    CHECK(b.top == doctest::Approx(50.0f));
    CHECK(b.height() == doctest::Approx(44.0f));
    CHECK(b.top <= 60.0f); // the track is still inside it
}

TEST_CASE("scrubber band: short of room below it moves up, then gets shorter, never into the row") {
    const HitBand up = scrubber_hit_band(100.0f, 44.0f, 50.0f, 110.0f);
    CHECK(up.bottom == doctest::Approx(110.0f));
    CHECK(up.height() == doctest::Approx(44.0f));
    const HitBand tight = scrubber_hit_band(60.0f, 44.0f, 50.0f, 80.0f);
    CHECK(tight.top == doctest::Approx(50.0f));
    CHECK(tight.bottom == doctest::Approx(80.0f));
}

TEST_CASE("scrubber band: bad input gives an empty band") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(scrubber_hit_band(nan, 44.0f, 50.0f, 150.0f).height() == 0.0f);
    CHECK(scrubber_hit_band(100.0f, 0.0f, 50.0f, 150.0f).height() == 0.0f);
    CHECK(scrubber_hit_band(100.0f, 44.0f, 50.0f, 20.0f).height() == 0.0f); // no room at all
}

TEST_CASE("scrubber band: never meets a top-row button, in any bar style, 800x480 to 21:9") {
    // Design points: a layout's pixels divided by how much denser the screen is than 96 dpi, which
    // is what the bar's tokens scale by. Every top-row button's hit rect spans the row.
    constexpr float kHitPt = 44.0f;            // the scrubber's band height (tok::kHit)
    constexpr float kScrubberHeightPt = 36.0f; // kHit less 8
    constexpr float kReachPt = 48.0f; // scrubber height plus 12: the bar window below the row
    const Case cases[] = {
        {800, 480, kPpmm96},   {1024, 600, kPpmm96},  {1280, 720, kPpmm96},  {1366, 768, kPpmm96},
        {1920, 1080, kPpmm96}, {2560, 1080, kPpmm96}, {3440, 1440, kPpmm96}, {2560, 1080, 5.0f},
        {800, 480, 7.5f},      {1280, 800, 6.0f},     {3440, 1440, 5.5f},    {1024, 440, kPpmm96},
    };
    for (const Case& c : cases) {
        CAPTURE(c.w);
        CAPTURE(c.h);
        CAPTURE(c.ppmm);
        const ScreenLayout l = compute_layout(c.w, c.h, c.ppmm, LookSettings{});
        const float u = ui_scale(c.w, c.h, c.ppmm);
        const float row_top = l.top_row.y / u;
        const float row_bottom = l.top_row.bottom() / u;
        const float h = row_bottom - row_top;
        // Where each style puts the track's centre (bar_strip, bar_dock and bar_groups).
        const float dock_h = std::min(h - 6.0f, 46.0f);
        const float centres[] = {
            row_bottom + kScrubberHeightPt * 0.5f,                                       // strip
            row_bottom - (h - dock_h) * 0.5f + 4.0f + (kScrubberHeightPt - 8.0f) * 0.5f, // dock
            row_bottom + 2.0f + (kScrubberHeightPt - 6.0f) * 0.5f,                       // groups
        };
        for (const float cy : centres) {
            CAPTURE(cy);
            const HitBand b = scrubber_hit_band(cy, kHitPt, row_bottom, row_bottom + kReachPt);
            CHECK(b.top >= row_bottom); // no overlap with any hit rect inside the row
            CHECK(b.bottom <= row_bottom + kReachPt);
            CHECK(b.height() == doctest::Approx(kHitPt)); // there is always room below
            CHECK(b.top <= cy);                           // the track is inside it
        }
        // A sweep: wherever the track is, the band stays out of the row.
        for (float cy = row_top; cy < row_bottom + 80.0f; cy += 0.5f) {
            const HitBand b = scrubber_hit_band(cy, kHitPt, row_bottom, row_bottom + kReachPt);
            CHECK(b.top >= row_bottom);
            CHECK(b.bottom <= row_bottom + kReachPt);
        }
    }
}
