// SPDX-License-Identifier: GPL-3.0-or-later
// Where the medal wall's parts go (medal_wall_layout.hpp), at the sizes the app is audited at and the
// scales it draws them at: every pressable a hit tall and wide, inside the body and clear of the
// others; every medal and the lines under it inside its cell; the words measured at the app's real
// font (atkinson_metrics.hpp) so a name that would be cut is found here.
#include <doctest/doctest.h>

#include "atkinson_metrics.hpp"

#include "ui/medal_wall_layout.hpp"
#include "ui/progress_layout.hpp"

#include "etude/medals.hpp"
#include "etude/screen_layout.hpp"

#include <array>
#include <string>
#include <vector>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;
using etude::BadgeWallView;

namespace {

struct Size {
    float w;
    float h;
};
constexpr std::array<Size, 5> kWindows{{{800, 480}, {1024, 600}, {1280, 800}, {1366, 768}, {1920, 1080}}};
constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

// The wall's body at a window and a scale: what the Progress page leaves under its header.
Box body_for(Size s, float u) {
    const Box area{0.0f, tok::kHit * u, s.w, s.h};
    ProgressContent c;
    c.view = ProgressTab::Medals;
    return plan_progress(area, u, c).wall;
}

bool empty_box(const Box& b) { return b.w() <= 0.0f || b.h() <= 0.0f; }

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

std::array<int, 5> counts() {
    std::array<int, 5> n{};
    for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
        ++n[static_cast<std::size_t>(etude::achievement_info(static_cast<etude::Achievement>(i)).category)];
    }
    return n;
}

// The widest name, over every achievement, in the font, in ems.
float widest_name_em(bool bold) {
    float widest = 0.0f;
    for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
        const auto& info = etude::achievement_info(static_cast<etude::Achievement>(i));
        widest = std::max(widest, etude::test::text_em(bold, info.name));
    }
    return widest;
}

} // namespace

TEST_CASE("medal wall: the five categories hold 24 achievements, six at most") {
    const std::array<int, 5> n = counts();
    int sum = 0;
    for (const int c : n) {
        CHECK(c >= 1);
        CHECK(c <= 6);
        sum += c;
    }
    CHECK(sum == static_cast<int>(etude::kAchievementCount));
}

TEST_CASE("medal wall: the category view's tabs are a hit, in the body, in a row and apart") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box body = body_for(s, u);
            for (int chosen = 0; chosen < 5; ++chosen) {
                CAPTURE(s.w);
                CAPTURE(u);
                CAPTURE(chosen);
                const WallLayout l = plan_wall(body, u, BadgeWallView::Category, counts(), chosen, 3);
                for (std::size_t i = 0; i < 5; ++i) {
                    CHECK(l.tab[i].w() >= tok::kCell * u - kEps); // a hit area and the dead gap
                    CHECK(l.tab[i].h() >= tok::kHit * u - kEps);
                    CHECK(body.holds(l.tab[i], -kEps));
                    for (std::size_t j = i + 1; j < 5; ++j) {
                        CHECK_FALSE(overlap(l.tab[i], l.tab[j]));
                    }
                }
                // Every tab that shows a name has the room for the widest, with its icon and padding.
                const float name_em = etude::test::text_em(true, "Practice time");
                for (std::size_t i = 0; i < 5; ++i) {
                    if (l.tab_names || static_cast<int>(i) == chosen) {
                        const float need = tok::kIconSm + tok::kS1 + name_em * tok::kLabel + 2.0f * tok::kS2;
                        const std::string name{etude::medal_category_name(static_cast<etude::MedalCategory>(i))};
                        const float own = tok::kIconSm + tok::kS1 + etude::test::text_em(true, name) * tok::kLabel +
                                          2.0f * tok::kS2;
                        CHECK(l.tab[i].w() / u + kEps >= own);
                        if (l.tab_names) {
                            CHECK(l.tab[i].w() / u + kEps >= need);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("medal wall: every large medal and the lines under it are inside its cell, never overlapping") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box body = body_for(s, u);
            for (int chosen = 0; chosen < 5; ++chosen) {
                CAPTURE(s.w);
                CAPTURE(s.h);
                CAPTURE(u);
                CAPTURE(chosen);
                const WallLayout l = plan_wall(body, u, BadgeWallView::Category, counts(), chosen, 3);
                CHECK(l.slots == counts()[static_cast<std::size_t>(chosen)]);
                for (int i = 0; i < l.slots; ++i) {
                    const WallSlot& m = l.slot[static_cast<std::size_t>(i)];
                    CHECK(m.d >= wall_tok::kLargeMin * u - kEps);
                    CHECK(body.holds(m.cell, -kEps));
                    // The medal's frame (ribbon above, shadow below) and the two lines are in the cell.
                    CHECK(m.cy - m.d * wall_tok::kLargeAbove >= m.cell.y0 - kEps);
                    CHECK(m.cx - m.d * wall_tok::kLargeW * 0.5f >= m.cell.x0 - kEps);
                    CHECK(m.cx + m.d * wall_tok::kLargeW * 0.5f <= m.cell.x1 + kEps);
                    CHECK(m.name.y0 >= m.cy - m.d * wall_tok::kLargeAbove + m.d * wall_tok::kLargeH - kEps);
                    CHECK(m.caption.y1 <= m.cell.y1 + kEps);
                    CHECK(m.cell.h() >= tok::kHit * u - kEps);
                    CHECK(m.cell.w() >= tok::kCell * u - kEps);
                    for (int j = i + 1; j < l.slots; ++j) {
                        CHECK_FALSE(overlap(m.cell, l.slot[static_cast<std::size_t>(j)].cell));
                    }
                    // The tabs and the strip are clear of the medals.
                    for (const Box& tab : l.tab) {
                        CHECK_FALSE(overlap(tab, m.cell));
                    }
                    if (l.nears > 0) {
                        CHECK_FALSE(overlap(l.strip, m.cell));
                    }
                }
                // The strip's cards are in its box, each a readable size.
                for (int i = 0; i < l.nears; ++i) {
                    CHECK(l.strip.holds(l.near[static_cast<std::size_t>(i)], -kEps));
                }
            }
        }
    }
}

TEST_CASE("medal wall: the strip of the closest medals is given up before the medals get small") {
    const Box roomy = body_for({1366, 768}, 1.0f);
    CHECK(plan_wall(roomy, 1.0f, BadgeWallView::Category, counts(), 1, 3).nears == 3);
    const WallLayout tight = plan_wall(body_for({800, 480}, 1.5f), 1.5f, BadgeWallView::Category, counts(), 1, 3);
    CHECK(tight.nears == 0);
    CHECK(tight.slot[0].d >= wall_tok::kLargeMin * 1.5f - kEps);
}

TEST_CASE("medal wall: the names under the large medals fit their cells at the real font") {
    // The widest name, bold at caption size, is inside the narrowest cell the category view gives (six
    // medals across the narrowest body, 800 wide at the largest scale).
    const Box body = body_for({800, 480}, 1.5f);
    const WallLayout l = plan_wall(body, 1.5f, BadgeWallView::Category, counts(), 1, 0);
    const float cell_design = l.slot[0].cell.w() / 1.5f;
    // (At body size where it fits, else at caption size: the wall picks per name.)
    CHECK(widest_name_em(true) * tok::kCaption <= cell_design - tok::kS1);
}

TEST_CASE("medal wall: the rows view gives five rows, each medal and its name inside its cell") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const Box body = body_for(s, u);
            const WallLayout l = plan_wall(body, u, BadgeWallView::Rows, counts(), 0, 0);
            if (!wall_rows_fit(body, u)) { // too short for five rows a hit tall: the category view stands in
                CHECK(l.rows == 0);
                CHECK(l.slots > 0);
                continue;
            }
            CHECK(l.rows == 5);
            for (int r = 0; r < l.rows; ++r) {
                const WallLayout::Row& row = l.row[static_cast<std::size_t>(r)];
                CHECK(body.holds(row.row, -kEps));
                CHECK(row.row.h() >= tok::kHit * u - kEps);
                CHECK(row.slots == counts()[static_cast<std::size_t>(r)]);
                if (r + 1 < l.rows) {
                    CHECK_FALSE(overlap(row.row, l.row[static_cast<std::size_t>(r + 1)].row));
                }
                for (int i = 0; i < row.slots; ++i) {
                    const WallSlot& m = row.slot[static_cast<std::size_t>(i)];
                    CHECK(m.d >= wall_tok::kSmallMin * u - kEps);
                    CHECK(m.name.y1 <= m.cell.y1 + kEps);
                    CHECK(row.row.holds(m.cell, -kEps));
                    CHECK(m.cy - m.d * wall_tok::kSmallFrame * 0.5f >= m.cell.y0 - kEps);
                    CHECK(m.name.y1 <= m.cell.y1 + kEps);
                    CHECK_FALSE(overlap(row.head, m.cell));
                    CHECK(m.cell.h() >= tok::kHit * u - kEps);
                    CHECK(m.cell.w() >= tok::kCell * u - kEps);
                }
                // The head names its category on one line at least: icon, a gap and the name; a narrow
                // body has the icon alone.
                const std::string name{etude::medal_category_name(static_cast<etude::MedalCategory>(r))};
                const float need = tok::kS2 + tok::kIcon + tok::kS1 + etude::test::text_em(true, name) * tok::kLabel;
                if (l.row_head_icon) {
                    CHECK(row.head.w() / u + kEps >= tok::kS2 + tok::kIcon);
                } else {
                    CHECK(row.head.w() / u + kEps >= need);
                }
            }
            // The names under the small medals (caption size, regular) fit the narrowest cell.
            float widest = 0.0f;
            for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
                const auto& info = etude::achievement_info(static_cast<etude::Achievement>(i));
                widest = std::max(widest, etude::test::text_em(false, info.name));
            }
            if (l.row_names) {
                CHECK(widest * tok::kCaption <= l.row[1].slot[0].cell.w() / u - tok::kS1);
            }
        }
    }
}

TEST_CASE("medal wall: the ladder card is in the body, eight rungs apart, the close button a hit") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const Box body = body_for(s, u);
            const LadderLayout l = plan_ladder(body, u);
            CHECK(body.holds(l.card, -kEps));
            CHECK(l.close.w() >= tok::kHit * u - kEps);
            CHECK(l.close.h() >= tok::kHit * u - kEps);
            CHECK(l.card.holds(l.close, -kEps));
            CHECK_FALSE(overlap(l.close, l.title));
            CHECK(l.one_line == empty_box(l.rule));
            for (std::size_t i = 0; i < 8; ++i) {
                const WallSlot& r = l.rung[i];
                CHECK(l.card.holds(r.cell, -kEps));
                CHECK(r.d >= wall_tok::kRungMin * u - kEps);
                CHECK(r.caption.y1 <= r.cell.y1 + kEps);
                for (std::size_t j = i + 1; j < 8; ++j) {
                    CHECK_FALSE(overlap(r.cell, l.rung[j].cell));
                }
                if (!l.one_line) {
                    CHECK_FALSE(overlap(r.cell, l.rule));
                }
                CHECK_FALSE(overlap(r.cell, l.title));
            }
            // Eight across from 76 design points a rung, else two rows of four.
            CHECK(l.two_rows == ((l.card.w() / u - 2.0f * wall_tok::kPad) / 8.0f < wall_tok::kLadderRungMinW - kEps));
        }
    }
}

TEST_CASE("medal wall: what the ladder says under a rung fits the rung at the real font") {
    // The tier's material and what it asks (caption size), in the narrowest rung: two rows of four on
    // the narrowest card (800 wide at the largest scale).
    const Box body = body_for({800, 480}, 1.5f);
    const LadderLayout l = plan_ladder(body, 1.5f);
    const float rung = l.rung[0].cell.w() / 1.5f;
    for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
        const auto& info = etude::achievement_info(static_cast<etude::Achievement>(i));
        for (int t = 1; t <= etude::kTierCount; ++t) {
            const std::string asks = etude::threshold_words(info, info.tiers[static_cast<std::size_t>(t - 1)]);
            CAPTURE(info.name);
            CAPTURE(asks);
            CHECK(etude::test::text_em(false, asks) * tok::kCaption <= rung - tok::kS1);
        }
    }
    for (int t = 1; t <= etude::kTierCount; ++t) {
        const std::string material{etude::tier_material_name(t)};
        CHECK(etude::test::text_em(true, material) * tok::kCaption <= rung - tok::kS1);
    }
}

TEST_CASE("medal wall: the rows view is there from 800x480 at the base scale, the toggle only where it is") {
    CHECK(wall_rows_fit(body_for({800, 480}, 1.0f), 1.0f));
    CHECK(wall_rows_fit(body_for({1366, 768}, 1.0f), 1.0f));
    CHECK(wall_rows_fit(body_for({1920, 1080}, 1.5f), 1.5f));
    CHECK_FALSE(wall_rows_fit(body_for({800, 480}, 1.5f), 1.5f));
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            const Box area{0.0f, tok::kHit * u, s.w, s.h};
            ProgressContent c;
            c.view = ProgressTab::Medals;
            const ProgressLayout l = plan_progress(area, u, c);
            const bool fits = wall_rows_fit(l.wall, u);
            CHECK(fits == !empty_box(l.toggle));
            std::vector<Box> press{l.back, l.mode_me, l.mode_household, l.mode_medals};
            if (fits) {
                press.push_back(l.toggle);
            }
            for (std::size_t i = 0; i < press.size(); ++i) {
                CHECK(press[i].w() >= tok::kHit * u - kEps);
                CHECK(press[i].h() >= tok::kHit * u - kEps);
                CHECK(area.holds(press[i], -kEps));
                for (std::size_t j = i + 1; j < press.size(); ++j) {
                    CHECK_FALSE(overlap(press[i], press[j]));
                }
                CHECK_FALSE(overlap(press[i], l.title));
            }
            CHECK(l.title.w() > 0.0f);
        }
    }
}

TEST_CASE("medal wall: the rows view needs each medal a hit wide as well as each row a hit tall") {
    // A body tall enough for five rows but too narrow for six medals a hit wide (with the dead gap).
    const Box tall_and_narrow{0.0f, 0.0f, 320.0f, 900.0f};
    CHECK_FALSE(wall_rows_fit(tall_and_narrow, 1.0f));
    const WallLayout fallback = plan_wall(tall_and_narrow, 1.0f, BadgeWallView::Rows, counts(), 0, 0);
    CHECK(fallback.rows == 0); // the category view stands in
    // Wherever it does fit, every medal's hit area (the cell less the dead gap) is at least a hit wide.
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box body = body_for(s, u);
            if (!wall_rows_fit(body, u)) {
                continue;
            }
            const WallLayout l = plan_wall(body, u, BadgeWallView::Rows, counts(), 0, 0);
            for (int r = 0; r < l.rows; ++r) {
                CHECK(l.row[static_cast<std::size_t>(r)].slot[0].cell.w() - tok::kHitGap * u >= tok::kHit * u - kEps);
            }
        }
    }
}

namespace {

// The widest a line is, at the font it is drawn with, at a whole size.
float words_w(bool bold, const std::string& text, float pt) { return etude::test::text_em(bold, text) * pt; }

// The panels and windows the wall is judged at: 800x480 on the 7 inch panel (154 mm) and at 96 dpi, and 1366x768.
struct Screen {
    Size s;
    float px_per_mm;
};
constexpr std::array<Screen, 3> kScreens{{{{800, 480}, 800.0f / 154.0f},
                                          {{800, 480}, etude::kDefaultPxPerMm},
                                          {{1366, 768}, etude::kDefaultPxPerMm}}};

// What the wall's cell room is for the category with the most medals, at a screen and a scale.
struct Room {
    float u;
    float room;
};
Room room_at(const Screen& c, float scale, int chosen) {
    const float u = etude::ui_scale(c.s.w, c.s.h, c.px_per_mm * scale);
    const Box body = body_for(c.s, u);
    return {u, wall_text_room(wall_cell_w(body, counts()[static_cast<std::size_t>(chosen)]), u)};
}

} // namespace

TEST_CASE("medal wall: a label's drawn size is a whole point under 40 pt, and a size of 0 stays 0") {
    CHECK(wall_drawn_pt(14.6f) == doctest::Approx(15.0f));
    CHECK(wall_drawn_pt(14.4f) == doctest::Approx(14.0f));
    CHECK(wall_drawn_pt(0.2f) == doctest::Approx(1.0f));
    CHECK(wall_drawn_pt(40.0f) == doctest::Approx(40.0f));
    CHECK(wall_drawn_pt(41.5f) == doctest::Approx(41.5f));
    CHECK(wall_drawn_pt(0.0f) == doctest::Approx(0.0f));
    CHECK(wall_drawn_pt(-3.0f) == doctest::Approx(-3.0f));
}

TEST_CASE("medal wall: a line is measured at the whole size it is drawn at, then shrinks, then wraps") {
    const auto width = [](const std::string& text, float pt) { return 0.5f * pt * static_cast<float>(text.size()); };
    // "aaaa bbbb" at 14.6 pt is drawn at 15: 9 chars * 7.5 = 67.5 wide.
    CHECK(wall_caption("aaaa bbbb", 14.6f, 68.0f, 9.0f, width).pt == doctest::Approx(15.0f));
    CHECK(wall_caption("aaaa bbbb", 14.6f, 67.0f, 9.0f, width).pt == doctest::Approx(14.0f)); // 63 wide
    // Too wide even at the floor (9 pt: 40.5): two lines at the floor, never cut.
    const WallText two = wall_caption("aaaa bbbb", 14.6f, 30.0f, 9.0f, width);
    CHECK(two.lines == 2);
    CHECK(two.pt == doctest::Approx(9.0f));
    CHECK(two.line[0] == "aaaa");
    CHECK(two.line[1] == "bbbb");
    // A word wider than the room cannot wrap: one line, for the caller to cut.
    CHECK(wall_caption("aaaabbbb", 14.6f, 20.0f, 9.0f, width).lines == 1);
    // A name wraps at body size before it shrinks.
    const WallText name = wall_name("aaaa bbbb", 16.0f, 13.0f, 40.0f, 9.0f, width);
    CHECK(name.lines == 2);
    CHECK(name.pt == doctest::Approx(16.0f));
    // With one line allowed nothing wraps: the line shrinks to the floor and is left to cut.
    const WallText one = wall_caption("aaaa bbbb", 14.6f, 30.0f, 9.0f, width, 1);
    CHECK(one.lines == 1);
    CHECK(one.pt == doctest::Approx(9.0f));
    // A name of one long word shrinks.
    const WallText word = wall_name("aaaabbbb", 16.0f, 13.0f, 40.0f, 9.0f, width);
    CHECK(word.lines == 1);
    CHECK(word.pt == doctest::Approx(10.0f));
}

TEST_CASE("medal wall: the captions fit their medal's hit area in one or two lines, never cut, nor under 9 pt") {
    // What a medal's caption says: the material and what the rule counted; "Silver · 930 points" is a value held at a tier.
    std::vector<std::string> captions{"Silver \xC2\xB7 930 points"};
    for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
        const auto& info = etude::achievement_info(static_cast<etude::Achievement>(i));
        for (int t = 1; t <= etude::kTierCount; ++t) {
            captions.push_back(std::string(etude::tier_material_name(t)) + " \xC2\xB7 " +
                               etude::threshold_words(info, info.tiers[static_cast<std::size_t>(t - 1)]));
        }
    }
    for (const Screen& c : kScreens) {
        for (const float scale : kScales) {
            for (int chosen = 0; chosen < 5; ++chosen) {
                const Room r = room_at(c, scale, chosen);
                for (const std::string& text : captions) {
                    CAPTURE(c.s.w);
                    CAPTURE(r.u);
                    CAPTURE(r.room);
                    CAPTURE(text);
                    const WallText t = wall_caption(text, tok::kCaption * r.u, r.room, wall_tok::kCaptionMin,
                                                    [](const std::string& s, float pt) { return words_w(false, s, pt); });
                    CHECK(t.pt == std::round(t.pt)); // a whole point: what the kit draws
                    CHECK(t.pt >= 9.0f);
                    CHECK(t.lines >= 1);
                    CHECK(t.lines <= 2);
                    for (int i = 0; i < t.lines; ++i) {
                        CHECK(words_w(false, t.line[static_cast<std::size_t>(i)], t.pt) <= r.room + kEps);
                    }
                }
            }
        }
    }
}

TEST_CASE("medal wall: every name fits its medal's hit area in one or two lines, never cut, nor under 9 pt") {
    for (const Screen& c : kScreens) {
        for (const float scale : kScales) {
            for (std::size_t i = 0; i < etude::kAchievementCount; ++i) {
                const auto& info = etude::achievement_info(static_cast<etude::Achievement>(i));
                const Room r = room_at(c, scale, static_cast<int>(info.category));
                const WallText t = wall_name(std::string(info.name), tok::kBody * r.u, tok::kCaption * r.u, r.room,
                                             wall_tok::kCaptionMin,
                                             [](const std::string& s, float pt) { return words_w(true, s, pt); });
                CAPTURE(c.s.w);
                CAPTURE(r.u);
                CAPTURE(r.room);
                CAPTURE(info.name);
                CHECK(t.pt >= 9.0f);
                CHECK(t.lines <= 2);
                for (int j = 0; j < t.lines; ++j) {
                    CHECK(words_w(true, t.line[static_cast<std::size_t>(j)], t.pt) <= r.room + kEps);
                }
            }
        }
    }
}

TEST_CASE("medal wall: extra lines under the medals are kept only while the medal keeps its least size") {
    for (const Screen& c : kScreens) {
        for (const float scale : kScales) {
            const float u = etude::ui_scale(c.s.w, c.s.h, c.px_per_mm * scale);
            const Box body = body_for(c.s, u);
            const WallLayout l = plan_wall(body, u, BadgeWallView::Category, counts(), 1, 3, {2, 2});
            CAPTURE(c.s.w);
            CAPTURE(u);
            REQUIRE(l.slots > 0);
            const WallSlot& m = l.slot[0];
            if (l.under == WallLines{2, 2}) {
                CHECK(m.d >= wall_tok::kLargeMin * u - kEps);
                CHECK(m.caption.y1 <= m.cell.y1 + kEps);
                CHECK(m.name.y0 >= m.cy - m.d * wall_tok::kLargeAbove + m.d * wall_tok::kLargeH - kEps);
                CHECK(m.name.h() == doctest::Approx(2.0f * wall_tok::kNameH * u));
                CHECK(m.caption.h() == doctest::Approx(2.0f * wall_tok::kCaptionH * u));
            } else {
                CHECK(l.under == WallLines{1, 1}); // too little height: one line each, and the medal is what the one-line layout gave
                CHECK(m.name.h() == doctest::Approx(wall_tok::kNameH * u));
            }
        }
    }
}
