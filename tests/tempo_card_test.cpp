// SPDX-License-Identifier: GPL-3.0-or-later
// The tempo popover's card fits an 800x480 window at the density of a 7" screen (x1.37): one column
// where it fits, else two, and every row keeps its 44 pt. The numbers are the real layout's
// (compute_layout) and the real tokens (app/ui/tempo_card.hpp).
#include "ui/tempo_card.hpp"
#include "ui/toast_regions.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <cstdio>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr BarStyle kStyles[] = {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock};

// What the host hands the plan for an 800x480 window at `unit`, the bar pinned or not, with a song.
TempoCardInput input_at(BarStyle style, float unit, bool pinned, int left_rows, int right_rows) {
    PinnedBar pb;
    pb.pinned = pinned;
    pb.style = style;
    pb.has_song = true;
    const ScreenLayout lay =
        compute_layout(800.0f, 480.0f, kDefaultPxPerMm * unit, LookSettings{}, pb);
    TempoCardInput in;
    in.display_w = 800.0f;
    in.keyboard_top = lay.keyboard.y;
    // Under the band: its own row when pinned, else hanging under the revealed bar.
    const float band_bottom =
        pinned ? lay.bar_under.bottom() : lay.top_row.bottom() + band_height_pt(true, unit);
    in.below_y = band_bottom + tok::kS2 * unit;
    in.u = unit;
    in.left_rows = left_rows;
    in.right_rows = right_rows;
    return in;
}

} // namespace

TEST_CASE("tempo card: at 800x480 it fits whole from x1.0 to x1.4, one column or two") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.1f, 1.25f, 1.37f, 1.4f}) {
                // The readout, slider and presets; the metronome (a narrow bar has no toggle) and
                // the count-in.
                const TempoCardInput in = input_at(style, unit, pinned, 3, 2);
                const TempoCardPlan p = plan_tempo_card(in);
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(unit);
                std::printf("tempo card 800x480 x%.2f %s: %s, %.0f x %.0f of %.0f px high\n",
                            static_cast<double>(unit), pinned ? "pinned" : "unpinned",
                            p.two_columns ? "two columns" : "one column", static_cast<double>(p.w),
                            static_cast<double>(p.h), static_cast<double>(p.max_h));
                CHECK(p.fits());
                CHECK(p.w <= in.display_w - tok::kS5 * unit + 0.01f);
                CHECK(p.y + p.h <= in.keyboard_top);
                // A column holds four presets, each at least a hit area wide, a dead gap apart.
                const float cell = (p.col_w - 3.0f * tok::kHitGap * unit) / 4.0f;
                CHECK(cell >= tok::kHit * unit * 0.99f);
            }
        }
    }
}

TEST_CASE("tempo card: the rows never touch, never overlap, never under a hit area") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            // Every row keeps the dead gap, whatever the window: where the room is short the
            // padding gives way first and a card that still does not fit scrolls.
            for (const float unit : {1.0f, 1.37f, 1.5f, 1.6f}) {
                const TempoCardPlan p = plan_tempo_card(input_at(style, unit, pinned, 3, 2));
                CHECK(p.row_gap == doctest::Approx(tok::kHitGap * unit));
                CHECK(p.pad_y >= 0.0f);
                CHECK(p.pad_y <= tok::kS3 * unit);
                // Three rows of a hit area and the pixel the columns keep, at the least.
                CHECK(p.h >= 3.0f * tok::kHit * unit);
            }
        }
    }
}

TEST_CASE("tempo card: denser still it does not fit and scrolls, the rows no shorter than a hit area") {
    for (const BarStyle style : kStyles) {
        const TempoCardPlan p = plan_tempo_card(input_at(style, 1.6f, true, 3, 2));
        CHECK_FALSE(p.fits());
        CHECK(p.h >= 3.0f * tok::kHit * 1.6f);
    }
}

TEST_CASE("tempo card: where one column fits it stays one column, as at 1366x768") {
    // 1366x768 and up have the room: nothing about the card changes there.
    for (const BarStyle style : kStyles) {
        PinnedBar pb;
        pb.pinned = true;
        pb.style = style;
        pb.has_song = true;
        const ScreenLayout lay = compute_layout(1366.0f, 768.0f, kDefaultPxPerMm, LookSettings{}, pb);
        TempoCardInput in;
        in.display_w = 1366.0f;
        in.keyboard_top = lay.keyboard.y;
        in.below_y = lay.bar_under.bottom() + tok::kS2;
        in.u = 1.0f;
        in.left_rows = 4; // even with the typed tempo
        in.right_rows = 2;
        const TempoCardPlan p = plan_tempo_card(in);
        CHECK_FALSE(p.two_columns);
        CHECK(p.y == in.below_y);
        CHECK(p.w == doctest::Approx(kTempoCardW));
        CHECK(p.fits());
    }
}

TEST_CASE("tempo card: a window too small for any shape gets the shorter one, to scroll") {
    TempoCardInput in = input_at(BarStyle::Strip, 1.5f, true, 4, 2);
    in.keyboard_top = in.below_y + 60.0f; // a sliver of room
    const TempoCardPlan p = plan_tempo_card(in);
    CHECK_FALSE(p.fits());
    CHECK(p.two_columns);
}

TEST_CASE("tempo card: a window too narrow for two columns keeps the one") {
    TempoCardInput in = input_at(BarStyle::Strip, 1.5f, true, 3, 2);
    in.display_w = 360.0f;
    const TempoCardPlan p = plan_tempo_card(in);
    CHECK_FALSE(p.two_columns);
    CHECK(p.w <= in.display_w);
}

TEST_CASE("tempo card: the height is its rows, a hit area each, with the gaps and the padding") {
    CHECK(tempo_card_height(1, tok::kS1, tok::kS3, 1.0f) == doctest::Approx(tok::kHit + 2.0f * tok::kS3 + 1.0f));
    CHECK(tempo_card_height(3, tok::kS1, tok::kS3, 1.0f) ==
          doctest::Approx(3.0f * tok::kHit + 2.0f * tok::kS1 + 2.0f * tok::kS3 + 1.0f));
    // The rows scale with the unit; the gap and the padding are the caller's, already scaled.
    CHECK(tempo_card_height(3, 0.0f, 0.0f, 1.5f) - 1.0f ==
          doctest::Approx(1.5f * (tempo_card_height(3, 0.0f, 0.0f, 1.0f) - 1.0f)));
}

TEST_CASE(
    "tempo card: with the number pad up the entry card keeps above it, one column, 12 mm rows") {
    // 800x480 at 1.25: the pad is two rows of keys at the bottom; the card is the readout and the
    // typed tempo's field, laid out to the pad's top.
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            const float unit = 1.25f;
            TempoCardInput in = input_at(style, unit, pinned, 2, 0);
            const float pad_h =
                2.0f * (osk_tok::kKeyPitchMin * unit) + 2.0f * osk_tok::kKeyPad * unit;
            in.keyboard_top = 480.0f - pad_h;
            in.single = true;
            const TempoCardPlan p = plan_tempo_card(in);
            CAPTURE(static_cast<int>(style));
            CAPTURE(pinned);
            CHECK_FALSE(p.two_columns);
            // The card stands clear of the pad (a gap above it) and keeps its two rows whole.
            CHECK(in.below_y + p.h <= in.keyboard_top + 0.5f);
            CHECK(p.h >= 2.0f * tok::kHit * unit);
        }
    }
}

TEST_CASE("tempo card: the second column has the metronome and the Flow/Wait row only where the bar lacks them") {
    // The count-in is always there; the Flow/Wait row only where the bar has no chip (the least form).
    CHECK(tempo_right_rows(false, false) == 1);
    CHECK(tempo_right_rows(true, false) == 2);
    CHECK(tempo_right_rows(false, true) == 2);
    CHECK(tempo_right_rows(true, true) == 3);
    for (const BarForm form : {BarForm::Full, BarForm::Compact, BarForm::Tight, BarForm::Minimal,
                               BarForm::Lean, BarForm::Bare, BarForm::Least}) {
        CAPTURE(static_cast<int>(form));
        CHECK(!bar_has_mode(form) == (form == BarForm::Least));
    }
}
