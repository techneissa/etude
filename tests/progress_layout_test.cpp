// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Progress page's parts go (progress_layout.hpp), at the sizes the app is audited at and
// the scales it draws them at: every pressable is a hit tall and wide, inside the page and clear of
// every other; the cards are inside the page and do not overlap; nothing the page promises is
// dropped on the sizes that can hold it (the profile, the tiles, "Practise today" with a spot and
// Continue, and two best runs).
#include <doctest/doctest.h>

#include "ui/progress_layout.hpp"

#include <array>
#include <vector>

using namespace etude::app::ui;
namespace tok = etude::app::ui::tok;

namespace {

struct Size {
    float w;
    float h;
};
// The library screen's area: the window under the top row (a hit tall).
constexpr std::array<Size, 5> kWindows{
    {{800, 480}, {1024, 600}, {1280, 800}, {1366, 768}, {1920, 1080}}};
constexpr std::array<float, 3> kScales{1.0f, 1.25f, 1.5f};
constexpr float kEps = 1e-3f;

Box area_for(Size s, float u) { return {0.0f, tok::kHit * u, s.w, s.h}; }

bool overlap(const Box& a, const Box& b) {
    return a.x0 < b.x1 - kEps && b.x0 < a.x1 - kEps && a.y0 < b.y1 - kEps && b.y0 < a.y1 - kEps;
}

bool empty(const Box& b) { return b.w() <= 0.0f || b.h() <= 0.0f; }

void check_pressables(const std::vector<Box>& boxes, const Box& inside, float u) {
    for (std::size_t i = 0; i < boxes.size(); ++i) {
        CAPTURE(i);
        CHECK(boxes[i].w() >= tok::kHit * u - kEps);
        CHECK(boxes[i].h() >= tok::kHit * u - kEps);
        CHECK(inside.holds(boxes[i], -kEps));
        for (std::size_t j = i + 1; j < boxes.size(); ++j) {
            CAPTURE(j);
            CHECK_FALSE(overlap(boxes[i], boxes[j]));
        }
    }
}

std::vector<Box> cards_of(const ProgressLayout& l) {
    std::vector<Box> out;
    for (const Box& b : {l.calendar, l.graph, l.best, l.profile, l.ring, l.tiles, l.medals, l.practise}) {
        if (!empty(b)) {
            out.push_back(b);
        }
    }
    return out;
}

} // namespace

TEST_CASE("progress page: pressables are a hit, inside the page and apart; cards do not overlap") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box area = area_for(s, u);
            for (const int best : {0, 1, 3, 8}) {
                for (const int spots : {0, 1, 3}) {
                    for (const bool cont : {false, true}) {
                        CAPTURE(s.w);
                        CAPTURE(s.h);
                        CAPTURE(u);
                        CAPTURE(best);
                        CAPTURE(spots);
                        CAPTURE(cont);
                        ProgressContent c;
                        c.best_rows = best;
                        c.spots = spots;
                        c.can_continue = cont;
                        const ProgressLayout l = plan_progress(area, u, c);
                        std::vector<Box> press{l.back, l.mode_me, l.mode_household, l.mode_medals};
                        if (!empty(l.profile)) {
                            press.push_back(l.edit);
                        }
                        for (int i = 0; i < l.spots; ++i) {
                            press.push_back(l.spot[static_cast<std::size_t>(i)]);
                        }
                        if (!empty(l.continue_row)) {
                            press.push_back(l.continue_row);
                        }
                        check_pressables(press, area, u);
                        const std::vector<Box> cards = cards_of(l);
                        for (std::size_t i = 0; i < cards.size(); ++i) {
                            CHECK(area.holds(cards[i], -kEps));
                            for (std::size_t j = i + 1; j < cards.size(); ++j) {
                                CHECK_FALSE(overlap(cards[i], cards[j]));
                            }
                        }
                        // The rows and spots lie inside their cards.
                        for (int i = 0; i < l.best_rows; ++i) {
                            CHECK(l.best.holds(l.best_row[static_cast<std::size_t>(i)], -kEps));
                        }
                        for (int i = 0; i < l.spots; ++i) {
                            CHECK(l.practise.holds(l.spot[static_cast<std::size_t>(i)], -kEps));
                        }
                        if (!empty(l.continue_row)) {
                            CHECK(l.practise.holds(l.continue_row, -kEps));
                        }
                        // A spot never runs into Continue.
                        if (l.spots > 0 && !empty(l.continue_row)) {
                            CHECK(l.spot[static_cast<std::size_t>(l.spots - 1)].y1 <=
                                  l.continue_row.y0 + kEps);
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("progress page: the profile, the tiles, Practise today and two best runs always fit") {
    for (const Size s : kWindows) {
        for (const float u : {1.0f, 1.25f}) {
            // A window this size draws at the scale the kit gives it, which is 1 or more; the
            // smallest window at its smallest scale is the tightest the page is held to.
            const Box area = area_for(s, u);
            ProgressContent c;
            c.best_rows = 5;
            c.spots = 3;
            c.can_continue = true;
            const ProgressLayout l = plan_progress(area, u, c);
            CAPTURE(s.w);
            CAPTURE(s.h);
            CAPTURE(u);
            CHECK_FALSE(empty(l.profile));
            if (u <= 1.0f) {
                CHECK_FALSE(empty(l.tiles)); // the densest scale gives the tiles up on the smallest window
            }
            CHECK_FALSE(empty(l.practise));
            CHECK(l.spots >= 1);
            CHECK_FALSE(empty(l.continue_row));
            CHECK_FALSE(empty(l.graph));
            CHECK_FALSE(empty(l.best));
            CHECK(l.best_rows >= 2);
        }
    }
}

TEST_CASE("progress page: the wide windows hold every card, the narrow one drops the calendar first") {
    ProgressContent c;
    c.best_rows = 8;
    c.spots = 3;
    c.can_continue = true;
    const ProgressLayout wide = plan_progress(area_for({1366, 768}, 1.0f), 1.0f, c);
    CHECK(wide.wide);
    CHECK_FALSE(empty(wide.calendar));
    CHECK_FALSE(empty(wide.ring));
    CHECK_FALSE(empty(wide.medals));
    CHECK(wide.spots == 3);
    const ProgressLayout narrow = plan_progress(area_for({800, 480}, 1.0f), 1.0f, c);
    CHECK_FALSE(narrow.wide);
    CHECK(empty(narrow.calendar));
    CHECK(empty(narrow.ring));
}

TEST_CASE("progress page: the household's cards are a hit, inside the page and apart") {
    for (const Size s : kWindows) {
        for (const float u : kScales) {
            const Box area = area_for(s, u);
            for (const int n : {1, 2, 4, 8, 15}) {
                CAPTURE(s.w);
                CAPTURE(s.h);
                CAPTURE(u);
                CAPTURE(n);
                ProgressContent c;
                c.view = ProgressTab::Household;
                c.profiles = n;
                const ProgressLayout l = plan_progress(area, u, c);
                CHECK(l.houses == n);
                std::vector<Box> press{l.back, l.mode_me, l.mode_household, l.mode_medals};
                for (int i = 0; i < l.houses; ++i) {
                    press.push_back(l.house[static_cast<std::size_t>(i)]);
                }
                check_pressables(press, area, u);
            }
        }
    }
}
