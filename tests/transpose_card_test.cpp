// SPDX-License-Identifier: GPL-3.0-or-later
// The transposition card and the octave-shift offer fit an 800x480 window up to the density of a 7"
// screen (x1.5), under the bar and its band and above the keyboard, with every row 44 pt, both
// with and without the fit strip. The numbers are the real layout's (compute_layout) and the real
// tokens (app/ui/transpose_card.hpp).
#include "ui/toast_regions.hpp"
#include "ui/transpose_card.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr BarStyle kStyles[] = {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock};

struct Room {
    float display_w = 800.0f;
    float below_y = 0.0f;
    float keyboard_top = 0.0f;
    float u = 1.0f;
};

// What the host hands the cards for an 800x480 window at `unit`, the bar pinned or not.
Room room_at(BarStyle style, float unit, bool pinned) {
    PinnedBar pb;
    pb.pinned = pinned;
    pb.style = style;
    pb.has_song = true;
    const ScreenLayout lay =
        compute_layout(800.0f, 480.0f, kDefaultPxPerMm * unit, LookSettings{}, pb);
    Room r;
    const float band_bottom =
        pinned ? lay.bar_under.bottom() : lay.top_row.bottom() + band_height_pt(true, unit);
    r.below_y = band_bottom + tok::kS2 * unit;
    r.keyboard_top = lay.keyboard.y;
    r.u = unit;
    return r;
}

} // namespace

TEST_CASE("transpose card: at 800x480 it fits whole up to x1.5, without the strip") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f, 1.37f, 1.5f}) {
                const Room r = room_at(style, unit, pinned);
                const TransposeCardPlan p =
                    plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, false, 3);
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(unit);
                CHECK(p.fits());
                CHECK(p.w <= r.display_w - tok::kS5 * unit + 0.01f);
                // Two rows of 44 pt and the padding round them.
                CHECK(p.h >= 2.0f * tok::kHit * unit);
            }
        }
    }
}

TEST_CASE(
    "transpose card: with the strip it fits 800x480 up to x1.25, and at x1.5 the strip scrolls") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f}) {
                const Room r = room_at(style, unit, pinned);
                const TransposeCardPlan p =
                    plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, true, 3);
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(unit);
                CHECK(p.fits());
            }
            // At x1.5 the card with its strip is taller than the room: the stepper and the octave
            // moves are what stays whole (the card without the strip fits), the strip below them
            // scrolls into view, and no more is hidden than the strip itself.
            const Room r = room_at(style, 1.5f, pinned);
            const TransposeCardPlan with =
                plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, true, 3);
            const TransposeCardPlan without =
                plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, false, 3);
            CAPTURE(static_cast<int>(style));
            CAPTURE(pinned);
            CHECK_FALSE(with.fits());
            CHECK(without.fits());
            CHECK(with.h - with.max_h < strip_height(3, r.u));
        }
    }
}

TEST_CASE("transpose card: the strip grows the card by the strip and a gap, a bar at a time") {
    const Room r = room_at(BarStyle::Strip, 1.0f, false);
    const TransposeCardPlan plain =
        plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, false, 3);
    for (int bars = 0; bars <= kStripMaxBars; ++bars) {
        const TransposeCardPlan with =
            plan_transpose_card(r.display_w, r.below_y, r.keyboard_top, r.u, true, bars);
        CHECK(with.h == doctest::Approx(plain.h + tok::kHitGap + strip_height(bars, 1.0f)));
    }
    CHECK(strip_height(1, 1.0f) > strip_height(0, 1.0f));
    CHECK(strip_height(kStripMaxBars + 5, 1.0f) == strip_height(kStripMaxBars, 1.0f));
}

TEST_CASE("transpose card: a card too tall for the room says so, so it scrolls") {
    // A short window: the strip does not fit under the bar before the keyboard.
    const TransposeCardPlan p = plan_transpose_card(800.0f, 200.0f, 330.0f, 1.5f, true, 3);
    CHECK_FALSE(p.fits());
    CHECK(p.max_h < p.h);
}

TEST_CASE("offer card: one row of answers fits 800x480 up to x1.37 without the strip") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f, 1.37f}) {
                const Room r = room_at(style, unit, pinned);
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(unit);
                const TransposeCardPlan plain =
                    plan_offer_card(r.display_w, r.below_y, r.keyboard_top, r.u, 1, false);
                CHECK(plain.fits());
                CHECK(plain.w <= r.display_w - tok::kS5 * unit + 0.01f);
            }
        }
    }
}

TEST_CASE("offer card: with the strip it fits 800x480 up to x1.25, and scrolls from x1.37 only the "
          "strip") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f, 1.37f, 1.5f}) {
                const Room r = room_at(style, unit, pinned);
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(unit);
                const TransposeCardPlan with =
                    plan_offer_card(r.display_w, r.below_y, r.keyboard_top, r.u, 1, true);
                const TransposeCardPlan without =
                    plan_offer_card(r.display_w, r.below_y, r.keyboard_top, r.u, 1, false);
                if (unit <= 1.25f) {
                    CHECK(with.fits());
                }
                // Whatever the size, the card without its strip fits whole and less than the strip
                // is hidden.
                CHECK(without.fits());
                CHECK(with.h - with.max_h < strip_height(2, r.u));
            }
        }
    }
}

TEST_CASE("offer card: a second row of answers adds exactly one row and its gap") {
    const Room r = room_at(BarStyle::Strip, 1.0f, false);
    const TransposeCardPlan one =
        plan_offer_card(r.display_w, r.below_y, r.keyboard_top, 1.0f, 1, false);
    const TransposeCardPlan two =
        plan_offer_card(r.display_w, r.below_y, r.keyboard_top, 1.0f, 2, false);
    CHECK(two.h == doctest::Approx(one.h + tok::kHit + tok::kHitGap));
}

TEST_CASE(
    "offer card: a narrow window shrinks the card, never wider than the window less a margin") {
    const TransposeCardPlan p = plan_offer_card(360.0f, 100.0f, 400.0f, 1.0f, 2, false);
    CHECK(p.w <= 360.0f - tok::kS5);
}
