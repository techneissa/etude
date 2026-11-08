// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/piece_widths.hpp"

#include <cstddef>
#include <string>
#include <utility>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

// A width that tells piece and size apart: 10 per size, plus the piece's length.
float fake_width(const std::string& piece, std::size_t px) {
    return static_cast<float>(px) * 10.0f + static_cast<float>(piece.size());
}

} // namespace

TEST_CASE("piece widths: a piece keeps its index, an empty one is complete") {
    PieceWidths w;
    const auto c = w.add("C");
    CHECK(w.add("C") == c);
    CHECK(w.add("Sol") != c);
    CHECK(w.size() == 2);
    CHECK_FALSE(w.complete(c));
    const auto none = w.add("");
    CHECK(w.complete(none));
    for (const float px : w.table(none)) {
        CHECK(px == 0.0f);
    }
}

TEST_CASE("piece widths: measure with no limit fills every size from 1 px") {
    PieceWidths w;
    const std::vector<PieceWidths::Index> want{w.add("C"), w.add("Sol")};
    const bool done = w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [] { return false; });
    CHECK(done);
    CHECK(w.all_complete(want));
    CHECK(w.sizes_missing(want) == 0);
    for (const auto i : want) {
        const auto t = w.table(i);
        REQUIRE(t.size() == PieceWidths::kSizes);
        CHECK(t[0] == 0.0f); // 0 px is not a size
        for (std::size_t px = 1; px < t.size(); ++px) {
            CHECK(t[px] == fake_width(w.piece(i), px));
        }
    }
}

TEST_CASE("piece widths: a call that is out of time still measures one size, so calls finish") {
    PieceWidths w;
    const std::vector<PieceWidths::Index> want{w.add("C"), w.add("D")};
    int measured = 0;
    const auto width = [&](const std::string& p, std::size_t px) {
        ++measured;
        return fake_width(p, px);
    };
    int calls = 0;
    bool done = false;
    while (!done) {
        const int before = measured;
        done = w.measure(want, width, [] { return true; });
        ++calls;
        CHECK(measured - before == 2); // one size, both pieces
        REQUIRE(calls <= static_cast<int>(PieceWidths::kSizes));
    }
    CHECK(calls == static_cast<int>(PieceWidths::kSizes) - 1);
    CHECK(measured == 2 * (static_cast<int>(PieceWidths::kSizes) - 1));
}

TEST_CASE("piece widths: the budget is asked after each size, and ends the call there") {
    PieceWidths w;
    const std::vector<PieceWidths::Index> want{w.add("C")};
    int asked = 0;
    const bool done = w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [&] { return ++asked == 5; });
    CHECK_FALSE(done);
    CHECK(asked == 5);
    CHECK(w.sizes_missing(want) == PieceWidths::kSizes - 1 - 5);
    // The next call carries on at size 6.
    std::size_t first = 0;
    w.measure(
        want,
        [&](const std::string& p, std::size_t px) {
            if (first == 0) {
                first = px;
            }
            return fake_width(p, px);
        },
        [] { return true; });
    CHECK(first == 6);
}

TEST_CASE("piece widths: a piece added later catches up before the others go on") {
    PieceWidths w;
    const auto a = w.add("A");
    std::vector<PieceWidths::Index> want{a};
    int asked = 0;
    w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [&] { return ++asked == 3; }); // A has sizes 1..3
    const auto b = w.add("B");
    want = {a, b};
    std::vector<std::pair<std::string, std::size_t>> order;
    asked = 0;
    w.measure(
        want,
        [&](const std::string& p, std::size_t px) {
            order.emplace_back(p, px);
            return fake_width(p, px);
        },
        [&] { return ++asked == 4; });
    // B alone for sizes 1..3, then both at 4.
    REQUIRE(order.size() == 5);
    CHECK(order[0] == std::pair<std::string, std::size_t>{"B", 1});
    CHECK(order[1] == std::pair<std::string, std::size_t>{"B", 2});
    CHECK(order[2] == std::pair<std::string, std::size_t>{"B", 3});
    CHECK(order[3] == std::pair<std::string, std::size_t>{"A", 4});
    CHECK(order[4] == std::pair<std::string, std::size_t>{"B", 4});
}

TEST_CASE("piece widths: a finished set measures nothing and says so") {
    PieceWidths w;
    const std::vector<PieceWidths::Index> want{w.add("C")};
    w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [] { return false; });
    int measured = 0;
    const bool done = w.measure(
        want,
        [&](const std::string&, std::size_t) {
            ++measured;
            return 1.0f;
        },
        [] { return true; });
    CHECK(done);
    CHECK(measured == 0);
    CHECK(w.measure({}, [](const std::string&, std::size_t) { return 0.0f; }, [] { return true; }));
}

TEST_CASE("piece widths: only the asked pieces are measured") {
    PieceWidths w;
    const auto a = w.add("A");
    const auto b = w.add("B");
    const std::vector<PieceWidths::Index> want{a};
    w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [] { return false; });
    CHECK(w.complete(a));
    CHECK_FALSE(w.complete(b));
    CHECK(w.table(b)[10] == 0.0f);
}

TEST_CASE("piece widths: a table's address survives adding more pieces") {
    PieceWidths w;
    const auto first = w.add("C");
    const float* before = w.table(first).data();
    for (int i = 0; i < 500; ++i) {
        w.add("piece" + std::to_string(i));
    }
    CHECK(w.table(first).data() == before);
    CHECK(w.size() == 501);
}

TEST_CASE("piece widths: a slice that ends on the call that finishes the last size says done") {
    PieceWidths w;
    const std::vector<PieceWidths::Index> want{w.add("C")};
    int asked = 0;
    const bool done = w.measure(
        want, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [&] { return ++asked == static_cast<int>(PieceWidths::kSizes) - 1; });
    CHECK(asked == static_cast<int>(PieceWidths::kSizes) - 1); // asked after size 127, the last
    CHECK(done);
    CHECK(w.complete(want[0]));
    CHECK(w.sizes_missing(want) == 0);
}

TEST_CASE("piece widths: a slice ending on size 127 leaves a piece it was not asked for short") {
    PieceWidths w;
    const auto a = w.add("A");
    const auto b = w.add("B");
    const std::vector<PieceWidths::Index> only_a{a};
    int asked = 0;
    // A is measured up to and including size 127 in one slice; B was never asked for.
    const bool done_a = w.measure(
        only_a, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [&] { return ++asked == static_cast<int>(PieceWidths::kSizes) - 1; });
    CHECK(done_a);
    CHECK_FALSE(w.complete(b));
    // Both asked for, with B one size short: a slice that ends right after size 127 finishes both.
    const std::vector<PieceWidths::Index> both{a, b};
    asked = 0;
    w.measure(
        std::vector<PieceWidths::Index>{b},
        [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [&] { return ++asked == static_cast<int>(PieceWidths::kSizes) - 2; }); // B has 1..126
    CHECK_FALSE(w.complete(b));
    CHECK(w.sizes_missing(both) == 1);
    const bool done_both = w.measure(
        both, [](const std::string& p, std::size_t px) { return fake_width(p, px); },
        [] { return true; });
    CHECK(done_both);
    CHECK(w.table(b)[127] == fake_width("B", 127));
}
