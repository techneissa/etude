// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/top_menu_layout.hpp"

#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kEps = 1e-3f;

struct Size {
    float w;
    float h;
};
constexpr Size kSizes[] = {{1366.0f, 768.0f}, {800.0f, 480.0f}};
// The UI scales: 1.0, and the taste scales 1.25 and 1.5 on top.
constexpr float kScales[] = {1.0f, 1.25f, 1.5f};

// About the widest of the entries' words ("Library") at the body size.
constexpr float kWordW = 60.0f;

TopMenuLayout layout_at(Size s, float unit, int rows) {
    const ScreenLayout lay = compute_layout(s.w, s.h, kDefaultPxPerMm * unit, LookSettings{});
    return top_menu_layout(s.w, s.h, lay.top_row.bottom() + tok::kS1 * unit, 0.0f, rows,
                           kWordW * unit, unit);
}

bool overlaps(const Box& a, const Box& b) {
    return a.x0 < b.x1 && b.x0 < a.x1 && a.y0 < b.y1 && b.y0 < a.y1;
}

} // namespace

TEST_CASE("the menu lists Library, Learn and Profile in that order, whichever the app has") {
    const TopMenuPlan all = top_menu_plan(true, true, true);
    REQUIRE(all.count == 3);
    CHECK(all.entry[0] == TopMenuEntry::Library);
    CHECK(all.entry[1] == TopMenuEntry::Learn);
    CHECK(all.entry[2] == TopMenuEntry::Profile);
    const TopMenuPlan no_learn = top_menu_plan(true, false, true);
    REQUIRE(no_learn.count == 2);
    CHECK(no_learn.entry[0] == TopMenuEntry::Library);
    CHECK(no_learn.entry[1] == TopMenuEntry::Profile);
    CHECK(top_menu_plan(false, false, false).count == 0);
}

TEST_CASE("the rows are the entries' hit areas, top to bottom, one for each entry the plan lists") {
    const TopMenuPlan plan = top_menu_plan(true, true, true);
    for (const Size s : kSizes) {
        for (const float unit : kScales) {
            const TopMenuLayout lay = layout_at(s, unit, plan.count);
            INFO(s.w << "x" << s.h << " at " << unit);
            REQUIRE(lay.rows == plan.count);
            for (int i = 1; i < plan.count; ++i) {
                CHECK(lay.row[static_cast<std::size_t>(i)].y0 >=
                      lay.row[static_cast<std::size_t>(i - 1)].y1);
            }
        }
    }
}

TEST_CASE("every row is a 12 mm hit, two rows are 2 mm apart, and the card holds them on screen") {
    for (const Size s : kSizes) {
        for (const float unit : kScales) {
            for (int rows = 1; rows <= top_menu_tok::kMaxRows; ++rows) {
                const TopMenuLayout lay = layout_at(s, unit, rows);
                INFO(s.w << "x" << s.h << " at " << unit << ", " << rows << " rows");
                REQUIRE(lay.rows == rows);
                for (int i = 0; i < rows; ++i) {
                    const Box& b = lay.row[static_cast<std::size_t>(i)];
                    CHECK(b.w() >= tok::kHit * unit - kEps);
                    CHECK(b.h() >= tok::kHit * unit - kEps);
                    CHECK(lay.card.holds(b));
                    if (i > 0) {
                        const Box& above = lay.row[static_cast<std::size_t>(i - 1)];
                        CHECK_FALSE(overlaps(above, b));
                        CHECK(b.y0 - above.y1 >= tok::kHitGap * unit - kEps);
                    }
                }
                // On the window, whole, with the edge margin.
                CHECK(lay.card.x0 >= tok::kS2 * unit - kEps);
                CHECK(lay.card.x1 <= s.w - tok::kS2 * unit + kEps);
                CHECK(lay.card.y0 >= 0.0f);
                CHECK(lay.card.y1 <= s.h - tok::kS2 * unit + kEps);
            }
        }
    }
}

TEST_CASE("the card hangs a dead gap below the menu button's hit area") {
    for (const Size s : kSizes) {
        for (const float unit : kScales) {
            const ScreenLayout row =
                compute_layout(s.w, s.h, kDefaultPxPerMm * unit, LookSettings{});
            // The button's hit area is the row's share of 12.2 mm, centred in the row (a wide row's
            // first button stands where the menu would).
            const RectF& button = row.menu.empty() ? row.library : row.menu;
            const float button_bottom = button.y + (button.h + button.h * kTopRowHitShare) * 0.5f;
            const TopMenuLayout lay = layout_at(s, unit, 3);
            INFO(s.w << "x" << s.h << " at " << unit);
            CHECK(lay.row[0].y0 - button_bottom >= tok::kHitGap * unit - kEps);
        }
    }
}

TEST_CASE("a card too tall for the room moves up to stay on the window, its rows still a hit") {
    const float unit = 1.0f;
    const TopMenuLayout lay = top_menu_layout(400.0f, 200.0f, 150.0f, 0.0f, 3, 60.0f, unit);
    CHECK(lay.card.y1 <= 200.0f - tok::kS2 * unit + kEps);
    CHECK(lay.card.y0 < 150.0f); // it moved up from under `top`
    CHECK(lay.card.y0 >= tok::kS2 * unit - kEps);
    for (int i = 0; i < lay.rows; ++i) {
        CHECK(lay.row[static_cast<std::size_t>(i)].h() >= tok::kHit * unit - kEps);
    }
}
