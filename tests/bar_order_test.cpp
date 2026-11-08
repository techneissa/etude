// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/bar_order.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

// A card naming bars 9, 3, 12 and 5, hottest first, as a run names them.
ResultsCard card_of(std::initializer_list<int> measures) {
    ResultsCard c;
    for (const int m : measures) {
        CardBar& b = c.bars[static_cast<std::size_t>(c.bar_count++)];
        b.measure = m;
        b.written = m;
    }
    c.start_bars = card_all_bars(c.bar_count);
    return c;
}

std::vector<int> measures_of(const ResultsCard& c, const BarOrder& o) {
    std::vector<int> out;
    for (int i = 0; i < o.count; ++i) {
        out.push_back(c.bars[o.slot[static_cast<std::size_t>(i)]].measure);
    }
    return out;
}

} // namespace

TEST_CASE("order: the default is song order, whatever order the card names the bars in") {
    const ResultsCard c = card_of({9, 3, 12, 5});
    CHECK(measures_of(c, song_order(c, c.start_bars)) == std::vector<int>{3, 5, 9, 12});
    // Only the chosen ones: bit 1 (bar 3) is left out.
    CHECK(measures_of(c, song_order(c, c.start_bars & ~2u)) == std::vector<int>{5, 9, 12});
    CHECK(song_order(c, 0).count == 0);
}

TEST_CASE("order: a bar moves to a place and the others shift by one") {
    const ResultsCard c = card_of({1, 2, 3, 4});
    BarOrder o = song_order(c, c.start_bars);
    CHECK(order_move(o, 0, 2));
    CHECK(measures_of(c, o) == std::vector<int>{2, 3, 1, 4});
    CHECK(order_move(o, 3, 0));
    CHECK(measures_of(c, o) == std::vector<int>{4, 2, 3, 1});
    CHECK(order_up(o, 3));
    CHECK(measures_of(c, o) == std::vector<int>{4, 2, 1, 3});
    CHECK(order_down(o, 0));
    CHECK(measures_of(c, o) == std::vector<int>{2, 4, 1, 3});
}

TEST_CASE("order: the first cannot go up, the last cannot go down, and nothing outside moves") {
    const ResultsCard c = card_of({1, 2, 3});
    BarOrder o = song_order(c, c.start_bars);
    const BarOrder before = o;
    CHECK_FALSE(order_up(o, 0));
    CHECK_FALSE(order_down(o, 2));
    CHECK_FALSE(order_move(o, 1, 1));
    CHECK_FALSE(order_move(o, -1, 1));
    CHECK_FALSE(order_move(o, 1, 3));
    CHECK_FALSE(order_move(o, 5, 0));
    CHECK(o == before);
}

namespace {
const ReorderDrag::Rows kRows{100.0f, 44.0f,  4,   1,
                              0.0f,   300.0f, 0.0f}; // places at 100, 144, 188, 232
constexpr float kSlop = 9.0f;
} // namespace

TEST_CASE("place at: the row under the pointer, clamped to the list") {
    CHECK(place_at(kRows, 10.0f, 100.0f) == 0);
    CHECK(place_at(kRows, 10.0f, 143.9f) == 0);
    CHECK(place_at(kRows, 10.0f, 144.0f) == 1);
    CHECK(place_at(kRows, 10.0f, 275.0f) == 3);
    CHECK(place_at(kRows, 10.0f, 9999.0f) == 3);
    CHECK(place_at(kRows, 10.0f, -50.0f) == 0);
    CHECK(place_at({0.0f, 44.0f, 0}, 5.0f, 10.0f) == 0);
}

TEST_CASE("drag: a press that stays within the threshold is a tap and moves nothing") {
    ReorderDrag d;
    d.press(1, 20.0f, 160.0f);
    CHECK(d.active());
    CHECK_FALSE(d.move(20.0f, 160.0f + kSlop, kRows, kSlop).has_value());
    CHECK_FALSE(d.move(20.0f, 160.0f - kSlop, kRows, kSlop).has_value());
    CHECK_FALSE(d.dragging());
    CHECK_FALSE(d.release());
    CHECK_FALSE(d.active());
}

TEST_CASE("drag: past the threshold the row follows the pointer, place by place") {
    ReorderDrag d;
    d.press(0, 20.0f, 110.0f);
    // Past the threshold but still over its own row: dragging, nothing to move.
    CHECK_FALSE(d.move(20.0f, 110.0f + kSlop + 1.0f, kRows, kSlop).has_value());
    CHECK(d.dragging());
    CHECK(d.place() == 0);
    const auto to1 = d.move(20.0f, 150.0f, kRows, kSlop);
    REQUIRE(to1.has_value());
    CHECK(*to1 == 1);
    CHECK(d.place() == 1);
    // The same height again changes nothing.
    CHECK_FALSE(d.move(20.0f, 151.0f, kRows, kSlop).has_value());
    // Straight past the end: clamped to the last place, jumping over the ones between.
    const auto to3 = d.move(20.0f, 900.0f, kRows, kSlop);
    REQUIRE(to3.has_value());
    CHECK(*to3 == 3);
    // And back up to the top.
    const auto to0 = d.move(20.0f, -10.0f, kRows, kSlop);
    REQUIRE(to0.has_value());
    CHECK(*to0 == 0);
    CHECK(d.release());
}

TEST_CASE("drag: applying each move to an order gives the order the player dropped it in") {
    const ResultsCard c = card_of({1, 2, 3, 4});
    BarOrder o = song_order(c, c.start_bars);
    ReorderDrag d;
    d.press(0, 20.0f, 110.0f); // bar 1 picked up
    for (const float y : {125.0f, 150.0f, 195.0f, 240.0f}) {
        const int from = d.place();
        if (const auto to = d.move(20.0f, y, kRows, kSlop)) {
            CHECK(order_move(o, from, *to));
        }
    }
    CHECK(measures_of(c, o) == std::vector<int>{2, 3, 4, 1});
    CHECK(d.release());
}

TEST_CASE("drag: a cancelled touch keeps what was done but reports no drop") {
    ReorderDrag d;
    d.press(2, 20.0f, 200.0f);
    d.move(20.0f, 120.0f, kRows, kSlop);
    CHECK_FALSE(d.release(true));
    CHECK_FALSE(d.active());
    // A move with nothing held does nothing.
    CHECK_FALSE(d.move(20.0f, 100.0f, kRows, kSlop).has_value());
}

TEST_CASE("drag: a pointer moved sideways past the threshold is a drag too") {
    ReorderDrag d;
    d.press(0, 20.0f, 110.0f);
    CHECK_FALSE(d.move(20.0f + kSlop - 1.0f, 110.0f, kRows, kSlop).has_value());
    CHECK_FALSE(d.dragging());
    d.move(20.0f + kSlop + 1.0f, 110.0f, kRows, kSlop);
    CHECK(d.dragging());
}

namespace {
// Six places in two columns of three: places 0..2 down the left, 3..5 down the right.
const ReorderDrag::Rows kTwoColumns{100.0f, 44.0f, 6, 2, 10.0f, 200.0f, 20.0f};
} // namespace

TEST_CASE("place at: two columns fill the first before the second, the pointer's column decides") {
    CHECK(place_at(kTwoColumns, 50.0f, 110.0f) == 0);
    CHECK(place_at(kTwoColumns, 50.0f, 200.0f) == 2);
    CHECK(place_at(kTwoColumns, 250.0f, 110.0f) == 3);
    CHECK(place_at(kTwoColumns, 250.0f, 200.0f) == 5);
    // Past the end of a column: its last row. Past the left edge: the left column.
    CHECK(place_at(kTwoColumns, 50.0f, 900.0f) == 2);
    CHECK(place_at(kTwoColumns, 250.0f, 900.0f) == 5);
    CHECK(place_at(kTwoColumns, -40.0f, 110.0f) == 0);
    // The gap between the columns goes to the nearer one.
    CHECK(place_at(kTwoColumns, 215.0f, 110.0f) == 0);
    CHECK(place_at(kTwoColumns, 225.0f, 110.0f) == 3);
    // An odd count: the left column has the extra row.
    const ReorderDrag::Rows five{100.0f, 44.0f, 5, 2, 10.0f, 200.0f, 20.0f};
    CHECK(place_at(five, 250.0f, 110.0f) == 3);
    CHECK(place_at(five, 250.0f, 900.0f) == 4);
}

TEST_CASE("drag: across to the other column moves the row to that place") {
    ReorderDrag d;
    d.press(0, 50.0f, 110.0f);
    d.move(60.0f, 112.0f, kTwoColumns, kSlop);
    const auto to = d.move(260.0f, 112.0f, kTwoColumns, kSlop);
    REQUIRE(to.has_value());
    CHECK(*to == 3);
}
