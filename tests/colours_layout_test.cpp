// SPDX-License-Identifier: GPL-3.0-or-later
// The Effects page fits its body with nothing to scroll, whole or split into its two
// halves, at the window sizes the app supports, and every cell on it keeps a full touch target.
// The sheet, its rail and page area are the app's own (app/ui/sheet_layout.hpp), the top row the
// real layout's (compute_layout).
#include "ui/colours_layout.hpp"
#include "ui/sheet_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <cstdio>
#include <string>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct Case {
    float w;
    float h;
    float u;
};
// The sizes the page must fit with nothing to scroll: the laptop and desktop sizes, at the UI
// scales a denser screen of that size gets. Its hit areas keep their 12 mm and their 2 mm gaps, so
// the 800x480 panel (7 inch) scrolls this page, as the sheet's long pages do.
constexpr Case kFits[] = {{1024.0f, 600.0f, 1.0f},  {1366.0f, 768.0f, 1.0f},
                          {1366.0f, 768.0f, 1.25f}, {1920.0f, 1080.0f, 1.0f},
                          {1920.0f, 1080.0f, 1.25f}, {1920.0f, 1080.0f, 1.5f}};
constexpr float kEps = 1e-3f;

PageArea area_for(Case c) {
    const ScreenLayout lay = compute_layout(c.w, c.h, kDefaultPxPerMm * c.u, LookSettings{});
    const SheetFrame sheet = sheet_frame(c.w, c.h, lay.top_row.bottom() + tok::kS2 * c.u, c.u);
    const RailLayout rail = rail_layout(sheet.w, sheet.h, kRailPages, c.u);
    return page_area(sheet, rail, c.u);
}

std::string where(Case c) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0fx%.0f x%.2f", c.w, c.h, static_cast<double>(c.u));
    return buf;
}

} // namespace

TEST_CASE("colours page: fits its body with nothing to scroll, whole or in two halves") {
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        REQUIRE(area.columns == 2);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        MESSAGE(where(c) << " arrangement " << static_cast<int>(l.arrangement) << " body "
                         << area.w << "x" << area.body_h << " tallest " << colours_height(l));
        CHECK(colours_height(l) <= area.body_h + kEps);
        // Two columns and the gap fit the width.
        CHECK(l.col_w * 2.0f + l.gap <= area.w + kEps);
    }
}

TEST_CASE("colours page: whole when it fits, then split in columns, then stacked") {
    const float u = 1.0f;
    const ColoursLayout roomy = colours_layout(900.0f, 2000.0f, u);
    CHECK(roomy.arrangement == ColoursArrangement::Whole);
    CHECK(colours_layout(900.0f, roomy.whole_h, u).arrangement == ColoursArrangement::Whole);
    const ColoursLayout less = colours_layout(900.0f, roomy.whole_h - 1.0f, u);
    CHECK(less.arrangement == ColoursArrangement::SplitColumns);
    CHECK(colours_layout(900.0f, less.split_columns_h, u).arrangement ==
          ColoursArrangement::SplitColumns);
    CHECK(colours_layout(900.0f, less.split_columns_h - 1.0f, u).arrangement ==
          ColoursArrangement::SplitStacked);
    // Each step down is no taller than the one before at the same width.
    CHECK(less.split_columns_h <= less.whole_h);
}

TEST_CASE("colours page: every cell keeps a full touch target") {
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        for (const float w : {568.0f, 668.0f, 900.0f, 1200.0f}) {
            CAPTURE(u);
            CAPTURE(w);
            const ColoursLayout l = colours_layout(w * u, 600.0f * u, u);
            const float hit = tok::kHit * u - kEps;
            CHECK(l.chip_h >= hit);
            // Five scheme chips side by side.
            CHECK((l.col_w - 4.0f * tok::kHitGap * u) / static_cast<float>(kSchemeChips) >=
                  hit - 1.0f);
            for (const float width : {l.col_w, w * u}) {
                // Palette cells: as wide as their name and strip need.
                const int cols = palette_columns(width, u);
                const float cell_w =
                    (width - static_cast<float>(cols - 1) * l.gap) / static_cast<float>(cols);
                if (width >= kPaletteMinW * u) {
                    CHECK(cell_w >= kPaletteMinW * u - kEps);
                }
                // Swatch cells: a hit area and the dead gap.
                CHECK((width + tok::kHitGap * u) / static_cast<float>(swatches_per_row(width, u)) -
                          tok::kHitGap * u >=
                      hit);
            }
            // Hit tiles.
            CHECK((l.col_w - 3.0f * tok::kHitGap * u) / 4.0f >= hit);
            CHECK(kHitTileH * u >= hit);
        }
    }
}

TEST_CASE("colours page: the colour control and quick picks fit with nothing to scroll") {
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        const float cw = l.col_w;
        const float picker = picker_height(cw, c.u);
        const float quicks = quick_picks_height(c.u);
        switch (l.arrangement) {
        case ColoursArrangement::Whole:
            // The chips, the swatches and the control; the preview and the quick picks.
            CHECK(l.chip_h + l.gap + swatches_height(cw, c.u) + l.gap + picker <=
                  area.body_h + kEps);
            CHECK(preview_height(cw, c.u) + l.gap + quicks <= area.body_h + kEps);
            break;
        case ColoursArrangement::SplitColumns:
            CHECK(l.chip_h + l.gap + picker <= area.body_h + kEps);
            CHECK(l.preview_h + l.gap + quicks <= area.body_h + kEps);
            break;
        case ColoursArrangement::SplitStacked:
            // The control and the quick picks in two columns, nothing else.
            CHECK(picker_height(l.pick_left_w, c.u) <= area.body_h + kEps);
            CHECK(quicks <= area.body_h + kEps);
            CHECK(l.pick_left_w + l.gap + l.pick_right_w <= area.w + kEps);
            CHECK(l.pick_right_w >= quick_picks_min_width(c.u) - kEps);
            break;
        }
    }
}

TEST_CASE("colours page: the sheet's two columns are too narrow for the quick picks' eight cells") {
    // A hit area and its dead gap each, eight across, is wider than half the sheet's page at its
    // largest (1000 pt less the rail): the page is the Colours | Hit effects switch there.
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        CHECK(l.col_w < quick_picks_min_width(c.u));
        CHECK(l.arrangement == ColoursArrangement::SplitStacked);
    }
}

TEST_CASE("colours page: the swatch row is where it was with the colour control open or closed") {
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        if (l.arrangement == ColoursArrangement::Whole) {
            CHECK(whole_swatches_top(l, true) == whole_swatches_top(l, false));
            // Under the chips, so what opens beneath them cannot push them.
            CHECK(whole_swatches_top(l, true) == doctest::Approx(l.chip_h + l.gap));
        }
    }
}

TEST_CASE("colours page: the quick picks are eight columns, so a narrower column is stacked") {
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        for (const float w : {568.0f, 640.0f, 700.0f, 720.0f, 792.0f, 1000.0f}) {
            CAPTURE(u);
            CAPTURE(w);
            const ColoursLayout l = colours_layout(w * u, 3000.0f * u, u);
            if (l.col_w < quick_picks_min_width(u)) {
                CHECK(l.arrangement == ColoursArrangement::SplitStacked);
            } else {
                CHECK(l.arrangement == ColoursArrangement::Whole);
            }
            // Wherever the quick picks are drawn, a cell is a hit area.
            const float width = l.arrangement == ColoursArrangement::SplitStacked ? l.pick_right_w
                                                                                 : l.col_w;
            CHECK((width + tok::kHitGap * u) / static_cast<float>(kQuickColumns) - tok::kHitGap * u >=
                  tok::kHit * u - kEps);
        }
    }
}

TEST_CASE("colours page: the square, the hue bar and every cell keep a full touch target") {
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        for (const float w : {208.0f, 280.0f, 392.0f, 540.0f, 700.0f}) {
            CAPTURE(u);
            CAPTURE(w);
            const float width = w * u;
            // The square is as wide as the control and at least as tall as a hit area; the header
            // and the hue bar are hit rows.
            CHECK(picker_square_height(width, u) >= tok::kHit * u - kEps);
            CHECK(picker_square_height(width, u) <= kSquareMaxH * u + kEps);
            CHECK(picker_height(width, u) ==
                  doctest::Approx((2.0f * tok::kHit + 2.0f * tok::kHitGap) * u +
                                  picker_square_height(width, u)));
        }
        // The quick picks: a heading, four rows, a heading and two rows of recent colours.
        CHECK(quick_picks_height(u) ==
              doctest::Approx((2.0f * kSectionH + 6.0f * tok::kCell - 2.0f * tok::kHitGap) * u));
    }
    CHECK(kRecentRows == 2);
    CHECK(kRecentColours == 2 * kQuickColumns);
}

TEST_CASE("colours page: the sheet's lift never changes whole or split") {
    // A body within the lift (6 pt) of fitting the whole page, at each lift value the sheet runs
    // through: the page and its header lay out against the unlifted body, so the arrangement is
    // the same from the first frame of a slide to the last.
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        const float whole_h = colours_layout(900.0f, 3000.0f, u).whole_h;
        for (const float extra : {-3.0f, -0.5f, 0.0f, 0.5f, 3.0f, 6.0f}) {
            PageArea area;
            area.w = 900.0f;
            area.body_y = 100.0f;
            area.body_h = whole_h + extra;
            const ColoursArrangement settled = colours_layout(area.w, area.body_h, u).arrangement;
            for (float lift = 0.0f; lift <= 6.0f * u; lift += 0.5f) {
                CAPTURE(u);
                CAPTURE(extra);
                CAPTURE(lift);
                const LiftedBody body = lifted_body(area, lift);
                CHECK(body.layout_h == area.body_h);
                CHECK(colours_layout(area.w, body.layout_h, u).arrangement == settled);
                // Only the window moves: down by the lift and shorter by it.
                CHECK(body.top == doctest::Approx(area.body_y + lift));
                CHECK(body.top + body.window_h == doctest::Approx(area.body_y + area.body_h));
            }
        }
    }
}

TEST_CASE("colours page: the press rows and the screen rows have room wherever they are drawn") {
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        CHECK(l.press_h == doctest::Approx(control_rows_height(kPressRows, c.u)));
        CHECK(l.screen_h == doctest::Approx(control_rows_height(kScreenRows, c.u)));
        // Every row keeps its 12 mm hit area and the dead gap between: three press rows, five
        // screen rows, and both in one column on the whole page.
        CHECK(l.press_h == doctest::Approx((3.0f * tok::kHit + 2.0f * tok::kHitGap) * c.u));
        CHECK(l.screen_h == doctest::Approx((5.0f * tok::kHit + 4.0f * tok::kHitGap) * c.u));
        CHECK(l.effects_h == doctest::Approx((8.0f * tok::kHit + 7.0f * tok::kHitGap) * c.u));
        if (l.arrangement == ColoursArrangement::Whole) {
            CHECK(l.tiles_h + l.gap + l.effects_h + l.gap + l.preview_h <= area.body_h + kEps);
        } else {
            // Under the hit tiles, beside the screen rows; stacked, under the full-width preview.
            const float top = l.arrangement == ColoursArrangement::SplitStacked
                                  ? l.stacked_preview_h
                                  : l.preview_h;
            CHECK(top + l.gap + l.tiles_h + l.gap + l.press_h <= area.body_h + kEps);
            CHECK(top + l.gap + l.screen_h <= area.body_h + kEps);
        }
    }
}

TEST_CASE("colours page: the Colours half has a live preview at every size it fits") {
    for (const Case c : kFits) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        if (l.arrangement != ColoursArrangement::SplitStacked) {
            continue; // the columns' preview is the right column's top
        }
        // The preview across the top, a usable height, and under it the chips, palettes and
        // swatches, all inside the body: nothing to scroll.
        CHECK(l.stacked_preview_h >= kPreviewMinH * c.u - kEps);
        CHECK(l.stacked_preview_h <= kPreviewMaxH * c.u + kEps);
        const float stacked = l.stacked_preview_h + l.gap + l.chip_h + l.gap +
                              palettes_height(area.w, c.u) + l.gap + swatches_height(area.w, c.u);
        CHECK(stacked <= area.body_h + kEps);
        CHECK(colours_height(l) <= area.body_h + kEps);
        // The Hit effects half has the same preview, the same height, over its two columns.
        CHECK(l.stacked_effects_h <= area.body_h + kEps);
        // With the colour control open the preview stays while the control and the quick picks fit
        // under it, else it leaves (never the other way: the controls keep their full height).
        if (l.stacked_pick_preview_h > 0.0f) {
            CHECK(l.stacked_pick_preview_h == doctest::Approx(l.stacked_preview_h));
            CHECK(l.stacked_pick_preview_h + l.gap + quick_picks_height(c.u) <=
                  area.body_h + kEps);
        }
    }
}

TEST_CASE("colours page: eight palettes are kept room whichever scheme is shown") {
    // Six built-in palettes and two saved ones, so the blocks below do not move with the scheme.
    CHECK(kPaletteCount == static_cast<int>(kAllPalettes.size()) + kSavedPalettes);
    for (const float u : {1.0f, 1.5f}) {
        for (const float w : {400.0f, 600.0f, 792.0f}) {
            const int cols = palette_columns(w * u, u);
            const int rows = (kPaletteCount + cols - 1) / cols;
            CAPTURE(w);
            CHECK(palettes_height(w * u, u) ==
                  doctest::Approx((kSectionH + tok::kCell * static_cast<float>(rows) - tok::kHitGap) * u));
        }
    }
}

TEST_CASE("colours page: where even the stacked halves are too tall (800x480 at UI scale 1.25 and "
          "up) the page is as tall as its content, so it scrolls") {
    for (const Case c : {Case{800.0f, 480.0f, 1.25f}, Case{800.0f, 480.0f, 1.5f}}) {
        CAPTURE(where(c));
        const PageArea area = area_for(c);
        const ColoursLayout l = colours_layout(area.w, area.body_h, c.u);
        // Too tall for the body: the page keeps the height of its tallest half and scrolls.
        CHECK(l.arrangement == ColoursArrangement::SplitStacked);
        CHECK(colours_height(l) > area.body_h);
        // The hit effects half is the preview, then the tiles and the press rows in one column and
        // the screen rows in the other; the page is at least as tall as either.
        CHECK(l.stacked_effects_h >= l.stacked_preview_h + l.gap + l.tiles_h + l.gap + l.press_h - kEps);
        CHECK(l.stacked_effects_h >= l.stacked_preview_h + l.gap + l.screen_h - kEps);
        CHECK(colours_height(l) >= l.stacked_effects_h - kEps);
        // The Colours half keeps its preview, at its least height: the page is made taller, never
        // a row shorter.
        CHECK(l.stacked_preview_h >= kPreviewMinH * c.u - kEps);
    }
}

TEST_CASE("colours page: the Hit effects column counts the dead gap it draws between its blocks") {
    // The whole page's right column draws the press rows, then the screen rows straight after: each
    // row leaves a dead gap under itself, so one dead gap lies between the blocks and the column is
    // eight rows and seven gaps tall. A gap drawn on top of that would run past the body.
    for (const float u : {1.0f, 1.25f, 1.5f}) {
        const ColoursLayout l = colours_layout(900.0f * u, 3000.0f * u, u);
        CHECK(l.effects_h == doctest::Approx(l.press_h + tok::kHitGap * u + l.screen_h));
        CHECK(l.effects_h == doctest::Approx(control_rows_height(kPressRows + kScreenRows, u)));
        CHECK(l.whole_h >= l.tiles_h + l.gap + l.effects_h + l.gap + l.preview_h - kEps);
    }
}

TEST_CASE("hold menu: it goes as soon as the cell it was opened on is not where it was") {
    const CellRect opened{100.0f, 400.0f, 150.0f, 450.0f};
    CHECK_FALSE(cell_moved(opened, opened));
    CHECK_FALSE(cell_moved(opened, {100.2f, 400.2f, 150.2f, 450.2f})); // rounding
    CHECK(cell_moved(opened, {100.0f, 380.0f, 150.0f, 430.0f}));       // the page scrolled up
    CHECK(cell_moved(opened, {100.0f, 460.0f, 150.0f, 510.0f}));       // or down
    CHECK(cell_moved(opened, {130.0f, 400.0f, 180.0f, 450.0f}));       // or sideways
    CHECK(cell_moved(opened, {100.0f, 400.0f, 150.0f, 470.0f}));       // or the cell changed size
}
