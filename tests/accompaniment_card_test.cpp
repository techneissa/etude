// SPDX-License-Identifier: GPL-3.0-or-later
// The accompaniment menu's card fits an 800x480 window from x1.0 to the 7 inch panel's x1.37, so its
// volume row is never cut off (#239): one column where it fits, else two, and every row keeps its
// hit area and a dead gap to the next. At x1.5 a card of four parts and the volume is more than the
// window holds and scrolls with a drag (tested below). The numbers are the real layout's
// (compute_layout) and the real tokens (app/ui/accompaniment_card.hpp).
#include "ui/accompaniment_card.hpp"
#include "ui/toast_regions.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr BarStyle kStyles[] = {BarStyle::Strip, BarStyle::Groups, BarStyle::Dock};

// What the host hands the plan for a window of `w` x `h` at `unit`, the bar pinned or not, with a
// song; `rows` is All, the parts and the volume.
AccompCardInput input_at(BarStyle style, float w, float h, float unit, bool pinned, int rows) {
    PinnedBar pb;
    pb.pinned = pinned;
    pb.style = style;
    pb.has_song = true;
    const ScreenLayout lay = compute_layout(w, h, kDefaultPxPerMm * unit, LookSettings{}, pb);
    AccompCardInput in;
    in.display_w = w;
    in.keyboard_top = lay.keyboard.y;
    const float band_bottom =
        pinned ? lay.bar_under.bottom() : lay.top_row.bottom() + band_height_pt(true, unit);
    in.below_y = band_bottom + tok::kS2 * unit;
    in.u = unit;
    in.rows = rows;
    return in;
}

} // namespace

TEST_CASE("accompaniment card: at 800x480 it fits whole from x1.0 to the 7 inch panel's x1.37, "
          "one column or two") {
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            for (const float unit : {1.0f, 1.25f, 1.37f}) {
                for (const int rows : {3, 4, 5}) { // two, three or four parts, with All and Volume
                    const AccompCardInput in = input_at(style, 800.0f, 480.0f, unit, pinned, rows);
                    const AccompCardPlan p = plan_accomp_card(in);
                    CAPTURE(static_cast<int>(style));
                    CAPTURE(pinned);
                    CAPTURE(unit);
                    CAPTURE(rows);
                    CHECK(p.fits());
                    CHECK(p.row_gap >= tok::kHitGap * unit - 0.01f);
                    CHECK(p.w <= in.display_w - tok::kS5 * unit + 0.01f);
                    CHECK(p.first_rows >= (rows + 1) / 2);
                    // The volume's track keeps room: a column is never under its floor.
                    CHECK(p.col_w >= kAccompColMin * unit - 2.0f * p.pad);
                }
            }
        }
    }
}

TEST_CASE("accompaniment card: at x1.5 on 800x480 up to three parts fit whole; four parts and the "
          "volume scroll") {
    // At x1.5 the window is 533x320 points: under the bar and its band and over the keys there is
    // room for two rows of a hit area and a dead gap each (209 px), not the three rows (231 px) a
    // card of five rows needs in its first column, and no third column fits across. The rows keep
    // their hit area and gap, so the card is the room's height and a drag scrolls the rest.
    for (const BarStyle style : kStyles) {
        for (const bool pinned : {false, true}) {
            const float unit = 1.5f;
            for (const int rows : {3, 4}) {
                const AccompCardPlan p =
                    plan_accomp_card(input_at(style, 800.0f, 480.0f, unit, pinned, rows));
                CAPTURE(static_cast<int>(style));
                CAPTURE(pinned);
                CAPTURE(rows);
                CHECK(p.fits());
                CHECK(p.row_gap >= tok::kHitGap * unit - 0.01f);
            }
            const AccompCardInput in = input_at(style, 800.0f, 480.0f, unit, pinned, 5);
            const AccompCardPlan p = plan_accomp_card(in);
            CAPTURE(static_cast<int>(style));
            CAPTURE(pinned);
            CHECK_FALSE(p.fits());
            CHECK(p.columns == 2);
            CHECK(p.first_rows == 3);
            CHECK(p.row_gap >= tok::kHitGap * unit - 0.01f); // still a dead gap between rows
            // The card (drawn at `max_h` tall) shows at least two whole rows and their gap, and what
            // it hides is a scroll away: the rows below the first two are at most `h - max_h` down.
            CHECK(p.max_h >= 2.0f * tok::kHit * unit + p.row_gap);
            const float hidden = p.h - p.max_h;
            CHECK(hidden > 0.0f);
            CHECK(hidden < tok::kHit * unit + p.row_gap); // less than a row: one drag reaches it
        }
    }
}

TEST_CASE("accompaniment card: the rows never touch, and are never under a hit area") {
    for (const BarStyle style : kStyles) {
        const AccompCardPlan p = plan_accomp_card(input_at(style, 800.0f, 480.0f, 1.5f, true, 4));
        CHECK(p.columns == 2);
        CHECK(p.fits());
        CHECK(p.row_gap >= tok::kHitGap * 1.5f - 0.01f); // a dead gap, even where room is short
        // Two rows of a hit area and the pixel the columns keep, at the least.
        CHECK(p.h >= 2.0f * tok::kHit * 1.5f);
    }
}

TEST_CASE("accompaniment card: where one column fits it stays one column, as at 1366x768") {
    for (const BarStyle style : kStyles) {
        const AccompCardInput in = input_at(style, 1366.0f, 768.0f, 1.0f, true, 5);
        const AccompCardPlan p = plan_accomp_card(in);
        CHECK(p.columns == 1);
        CHECK(p.w == doctest::Approx(tok::kMenuW));
        CHECK(p.first_rows == 5);
        CHECK(p.fits());
    }
}

TEST_CASE("accompaniment card: a window too small for any shape gets the shorter one, to scroll") {
    AccompCardInput in = input_at(BarStyle::Strip, 800.0f, 480.0f, 1.5f, true, 8);
    in.keyboard_top = in.below_y + 60.0f; // a sliver of room
    const AccompCardPlan p = plan_accomp_card(in);
    CHECK_FALSE(p.fits());
    CHECK(p.columns == 2);
}

TEST_CASE("accompaniment card: a window too narrow for two columns keeps the one") {
    AccompCardInput in = input_at(BarStyle::Strip, 800.0f, 480.0f, 1.5f, true, 4);
    in.display_w = 360.0f;
    const AccompCardPlan p = plan_accomp_card(in);
    CHECK(p.columns == 1);
    CHECK(p.w <= in.display_w);
}
