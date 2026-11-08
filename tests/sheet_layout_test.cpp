// SPDX-License-Identifier: GPL-3.0-or-later
// The settings rail keeps every row a full touch target at every supported window size and UI
// scale, the rows never overlap, a narrow row's icon and caption stay inside it, and the rail
// scrolls when the rows do not fit. The sheet and rail are the app's own (app/ui/sheet_layout.hpp),
// the top row the real layout's (compute_layout).
#include "ui/sheet_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <cstdio>
#include <string>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct Size {
    float w;
    float h;
};
constexpr Size kSizes[] = {
    {800.0f, 480.0f}, {1024.0f, 600.0f}, {1366.0f, 768.0f}, {1920.0f, 1080.0f}};
constexpr float kScales[] = {1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

// The kit's unit in a window `size` at `--ui-scale` `scale` (a headless window has one pixel per
// point; the UI scale makes the screen denser): core's one scale.
float unit_for(Size size, float scale) {
    return ui_scale(size.w, size.h, kDefaultPxPerMm * scale);
}

// The sheet as the app places it: under the top row and a gap.
SheetFrame sheet_for(Size size, float scale) {
    const float u = unit_for(size, scale);
    const ScreenLayout lay =
        compute_layout(size.w, size.h, kDefaultPxPerMm * scale, LookSettings{});
    return sheet_frame(size.w, size.h, lay.top_row.bottom() + tok::kS2 * u, u);
}

std::string where(Size size, float u) {
    char buf[64];
    std::snprintf(buf, sizeof buf, "%.0fx%.0f x%.2f", size.w, size.h, static_cast<double>(u));
    return buf;
}

} // namespace

TEST_CASE("settings rail: every row keeps 44 pt and its own space, and the rail scrolls when "
          "the rows do not fit") {
    for (const Size size : kSizes) {
        for (const float scale : kScales) {
            const float u = unit_for(size, scale);
            CAPTURE(where(size, u));
            const SheetFrame sheet = sheet_for(size, scale);
            REQUIRE(sheet.h > 0.0f);
            const RailLayout rail = rail_layout(sheet.w, sheet.h, kRailPages, u);
            CHECK(rail.row_h >= tok::kHit * u - kEps);
            // Rows stack edge to edge from the top of the content: none overlaps the next.
            CHECK(rail.content_h >= rail.row_h * static_cast<float>(kRailPages) - kEps);
            // What does not fit scrolls; what fits does not.
            CHECK(rail.scrolls == (rail.content_h > rail.view_h));
            CHECK(rail.view_h <= sheet.h);
            if (!rail.wide) {
                for (int i = 0; i < kRailPages; ++i) {
                    const float y = rail.row_h * static_cast<float>(i);
                    const RailCaption c = rail_caption(y, rail.row_h, u);
                    const float icon_half = tok::kIcon * 0.9f * u * 0.5f;
                    CHECK(c.icon_cy - icon_half >= y - kEps);
                    CHECK(c.caption_y >= c.icon_cy + icon_half - kEps); // under the icon
                    CHECK(c.caption_y + c.caption_size <= y + rail.row_h + kEps);
                }
            }
        }
    }
}

TEST_CASE("settings rail at 800x480 and UI scale 1.5: rows keep their hit area and gap and the rail scrolls") {
    const Size size{800.0f, 480.0f};
    const float u = 1.5f;
    const SheetFrame sheet = sheet_for(size, u);
    const RailLayout rail = rail_layout(sheet.w, sheet.h, kRailPages, u);
    CHECK_FALSE(rail.wide);
    CHECK(rail.row_h == doctest::Approx((tok::kCell + kRailCaptionExtra) * u));
    CHECK(rail.scrolls);
}

TEST_CASE("settings rail: it scrolls only when the rows are taller than the room") {
    const float u = 1.0f;
    const float rows_h = (tok::kCell + kRailCaptionExtra) * static_cast<float>(kRailPages);
    const float pad = tok::kS2;
    // Exactly the room the rows need: no scrolling; a hair less: it scrolls, rows unchanged.
    const RailLayout fits = rail_layout(500.0f, rows_h + 2.0f * pad, kRailPages, u);
    CHECK_FALSE(fits.scrolls);
    const RailLayout short_one = rail_layout(500.0f, rows_h + 2.0f * pad - 0.01f, kRailPages, u);
    CHECK(short_one.scrolls);
    CHECK(short_one.row_h == fits.row_h);
}

TEST_CASE("the settings sheet is 1000x620 design points on the one scale, inside the window") {
    for (const Size size : kSizes) {
        for (const float scale : kScales) {
            const float u = unit_for(size, scale);
            CAPTURE(where(size, u));
            const ScreenLayout lay =
                compute_layout(size.w, size.h, kDefaultPxPerMm * scale, LookSettings{});
            const SheetFrame sheet = sheet_for(size, scale);
            const float margin = tok::kS3 * u;
            // Inside the window with a margin on each side and under it, below the top row.
            CHECK(sheet.x >= margin - 1.0f);
            CHECK(sheet.x + sheet.w <= size.w - margin + 1.0f);
            CHECK(sheet.y >= lay.top_row.bottom());
            CHECK(sheet.y + sheet.h <= size.h - margin + kEps);
            CHECK(sheet.w <= kSheetMaxW * u + kEps);
            CHECK(sheet.h <= kSheetMaxH * u + kEps);
            CHECK(sheet.h > 0.0f);
        }
    }
    // Where the window has room, the full size: 1000x620 at 1366x768, times 1.41 at 1920x1080.
    const SheetFrame mid = sheet_for({1366.0f, 768.0f}, 1.0f);
    CHECK(mid.w == doctest::Approx(1000.0f));
    CHECK(mid.h == doctest::Approx(620.0f));
    const SheetFrame big = sheet_for({1920.0f, 1080.0f}, 1.0f);
    // In whole points, so its edges are sharp: 1405.5 x 871.4 is drawn 1405 x 871.
    CHECK(big.w == std::floor(1000.0f * 1920.0f / 1366.0f));
    CHECK(big.h == std::floor(620.0f * 1920.0f / 1366.0f));
    // 800x480: the window less its margins, under the top row.
    const SheetFrame small = sheet_for({800.0f, 480.0f}, 1.0f);
    CHECK(small.w == doctest::Approx(800.0f - 2.0f * tok::kS3));
}

TEST_CASE("fit_sheet_height: the sheet is as tall as its tallest page needs, within its cap") {
    for (const Size size : {Size{1366.0f, 768.0f}, Size{1920.0f, 1080.0f}, Size{2560.0f, 1440.0f}}) {
        const SheetFrame cap = sheet_for(size, 1.0f);
        const float u = unit_for(size, 1.0f);
        const int rows = static_cast<int>(kRail.size());
        const PageArea area = page_area(cap, rail_layout(cap.w, cap.h, rows, u), u);
        CAPTURE(size.w);
        // Not measured yet: the cap.
        CHECK(fit_sheet_height(cap, 0.0f, rows, u).h == cap.h);
        CHECK(fit_sheet_height(cap, std::nanf(""), rows, u).h == cap.h);
        // A short tallest page: the body is just tall enough for it; the rest of the frame stays.
        // (Above what the rail itself needs: the sheet is never shorter than its rail.)
        const RailLayout cap_rail = rail_layout(cap.w, cap.h, rows, u);
        const float rail_body = cap_rail.content_h + 2.0f * cap_rail.pad - area.body_y - tok::kS3 * u;
        const float tallest = rail_body + (area.body_h - rail_body) * 0.4f;
        const SheetFrame fit = fit_sheet_height(cap, tallest, rows, u);
        CHECK(fit.h < cap.h);
        CHECK(fit.w == cap.w);
        CHECK(fit.x == cap.x);
        CHECK(fit.y == cap.y);
        const PageArea fitted = page_area(fit, rail_layout(fit.w, fit.h, rows, u), u);
        CHECK(fitted.body_h >= tallest - kEps);
        CHECK(fitted.body_h < tallest + 1.0f); // whole points, no more
        // The rail never starts to scroll because the sheet got shorter.
        CHECK_FALSE(rail_layout(fit.w, fit.h, rows, u).scrolls);
        CHECK_FALSE(rail_layout(cap.w, fit_sheet_height(cap, 1.0f, rows, u).h, rows, u).scrolls);
        // Taller than the body can be: the cap, and the page scrolls.
        CHECK(fit_sheet_height(cap, area.body_h * 2.0f, rows, u).h == cap.h);
        CHECK(fit_sheet_height(cap, area.body_h, rows, u).h == doctest::Approx(cap.h).epsilon(0.002));
    }
}

TEST_CASE("rail: a scrolling rail shows whole rows, centred, and rests on a row edge") {
    for (const Size size : kSizes) {
        for (const float scale : kScales) {
            const float u = unit_for(size, scale);
            CAPTURE(where(size, u));
            const SheetFrame sheet = sheet_for(size, scale);
            const RailLayout rail = rail_layout(sheet.w, sheet.h, kRailPages, u);
            if (!rail.scrolls) {
                CHECK(rail.rows_in_view == kRailPages);
                continue;
            }
            // Whole rows, inside the sheet less its pads, with the leftover split above and below.
            CHECK(rail.view_h == doctest::Approx(rail.row_h * static_cast<float>(rail.rows_in_view)));
            CHECK(rail.rows_in_view >= 1);
            CHECK(rail.rows_in_view < kRailPages);
            CHECK(2.0f * rail.lead + rail.view_h <= sheet.h - 2.0f * rail.pad + kEps);
            CHECK(sheet.h - 2.0f * rail.pad - rail.view_h < rail.row_h + kEps); // no row more fits
            // Every page's row can be brought fully into view and stays there at rest.
            const float max_scroll = rail.content_h - rail.view_h;
            for (int i = 0; i < kRailPages; ++i) {
                const float to = rail_scroll_to(i, kRailPages, rail.rows_in_view, rail.row_h);
                CHECK(to >= 0.0f);
                CHECK(to <= max_scroll + kEps);
                CHECK(rail_rest(to, rail.row_h, max_scroll) == doctest::Approx(to));
                CHECK(rail.row_h * static_cast<float>(i) >= to - kEps);
                CHECK(rail.row_h * static_cast<float>(i + 1) <= to + rail.view_h + kEps);
            }
        }
    }
}

TEST_CASE("rail_rest: the nearest row edge, kept inside the scroll range") {
    CHECK(rail_rest(0.0f, 78.0f, 156.0f) == 0.0f);
    CHECK(rail_rest(30.0f, 78.0f, 156.0f) == 0.0f);
    CHECK(rail_rest(40.0f, 78.0f, 156.0f) == 78.0f);
    CHECK(rail_rest(500.0f, 78.0f, 156.0f) == 156.0f);
    CHECK(rail_rest(-9.0f, 78.0f, 156.0f) == 0.0f);
    CHECK(rail_rest(150.0f, 78.0f, 170.0f) == 156.0f); // a last edge short of the end
    CHECK(rail_rest(5.0f, 78.0f, 0.0f) == 0.0f);
    CHECK(rail_rest(std::nanf(""), 78.0f, 156.0f) == 0.0f);
    CHECK(rail_scroll_to(0, 6, 4, 78.0f) == 0.0f);
    CHECK(rail_scroll_to(2, 6, 4, 78.0f) == 78.0f);
    CHECK(rail_scroll_to(5, 6, 4, 78.0f) == 156.0f);
    CHECK(rail_scroll_to(4, 6, 6, 78.0f) == 0.0f);
}

TEST_CASE("page heights: after a resize the sheet keeps its fitted height while the pages are measured again, never the cap") {
    constexpr std::size_t kPages = kRail.size();
    const int rows = static_cast<int>(kPages);
    PageHeights<kPages> heights;
    const auto measure_all = [&](float unit, std::size_t skip) {
        for (std::size_t n = 0; n < kPages; ++n) {
            const std::size_t i = heights.next_stale(skip);
            if (i == kPages) {
                break;
            }
            heights.set(i, (300.0f + 30.0f * static_cast<float>(i)) * unit, unit);
        }
    };
    // Not measured yet: the cap.
    const Size first{1366.0f, 768.0f};
    const float u1 = unit_for(first, 1.0f);
    heights.at(first.w, first.h, u1);
    CHECK(heights.tallest(u1) == 0.0f);
    measure_all(u1, kPages);
    const SheetFrame cap1 = sheet_for(first, 1.0f);
    const float fitted1 = fit_sheet_height(cap1, heights.tallest(u1), rows, u1).h;
    CHECK(fitted1 < cap1.h);
    // Every frame of a live drag is a new size: every page is stale, none is measured yet, and the
    // sheet keeps a fitted height from the heights it had, scaled with the unit.
    for (const Size s : {Size{1500.0f, 850.0f}, Size{1700.0f, 960.0f}, Size{1920.0f, 1080.0f},
                         Size{2560.0f, 1440.0f}}) {
        const float u = unit_for(s, 1.0f);
        heights.at(s.w, s.h, u);
        CAPTURE(s.w);
        CHECK(heights.next_stale(kPages) == 0); // all stale again
        const SheetFrame cap = sheet_for(s, 1.0f);
        const float h = fit_sheet_height(cap, heights.tallest(u), rows, u).h;
        CHECK(h < cap.h);
        CHECK(heights.tallest(u) == doctest::Approx((300.0f + 30.0f * (kPages - 1)) * u));
    }
    // The same size again changes nothing; the open page (skipped) is measured where it draws.
    const float u2 = unit_for({2560.0f, 1440.0f}, 1.0f);
    measure_all(u2, 2);
    CHECK(heights.next_stale(2) == kPages);
    CHECK(heights.next_stale(kPages) == 2);
    heights.at(2560.0f, 1440.0f, u2);
    CHECK(heights.next_stale(kPages) == 2);
    // A page that got shorter makes the sheet shorter: a new measure replaces the old.
    const float before = heights.tallest(u2);
    heights.set(kPages - 1, 100.0f * u2, u2);
    CHECK(heights.tallest(u2) < before);
    CHECK(heights.tallest(u2) == doctest::Approx((300.0f + 30.0f * (kPages - 2)) * u2));
    // A bad measure is ignored.
    heights.set(0, std::nanf(""), u2);
    heights.set(0, 50.0f, 0.0f);
    CHECK(heights.pt[0] == doctest::Approx(300.0f));
}

TEST_CASE("rail_fade: the end with rows past it fades, an end with nothing past it and a rail that fits do not") {
    const float row = 52.0f;
    const float max = 2.0f * row;
    // Bottom only: at the start of the scroll, rows lie below and none above.
    const RailFade top_rest = rail_fade(true, 0.0f, max, row, false);
    CHECK(top_rest.top == 0.0f);
    CHECK(top_rest.bottom == 1.0f);
    // Both: in the middle.
    const RailFade mid = rail_fade(true, row, max, row, false);
    CHECK(mid.top == 1.0f);
    CHECK(mid.bottom == 1.0f);
    // Top only: at the end of the scroll, rows lie above and none below.
    const RailFade end = rail_fade(true, max, max, row, false);
    CHECK(end.top == 1.0f);
    CHECK(end.bottom == 0.0f);
    // None: the rail fits, or has no range.
    const RailFade fits = rail_fade(false, 0.0f, 0.0f, row, false);
    CHECK(fits.top == 0.0f);
    CHECK(fits.bottom == 0.0f);
    CHECK(rail_fade(true, 0.0f, 0.0f, row, false).bottom == 0.0f);
    // It grows with the scroll, and is all or nothing with reduced effects.
    CHECK(rail_fade(true, row * 0.25f, max, row, false).top == doctest::Approx(0.5f));
    CHECK(rail_fade(true, row * 0.25f, max, row, true).top == 1.0f);
    CHECK(rail_fade(true, 0.0f, max, row, true).top == 0.0f);
    CHECK(rail_fade(true, std::nanf(""), max, row, false).top == 0.0f);
}
