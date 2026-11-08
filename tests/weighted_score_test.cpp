// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/weighted_score.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

// Four bars of 10 notes each: bars 1 and 2 easy (1 star), bars 3 and 4 hard (5 stars).
SongDifficulty easy_then_hard() {
    SongDifficulty d;
    d.bars = {1.0f, 1.0f, 5.0f, 5.0f};
    d.numbers = {1, 2, 3, 4};
    return d;
}

RunRecord run_of(std::initializer_list<RunBar> bars) {
    RunRecord r;
    r.measures = bars;
    for (const RunBar& b : bars) {
        r.all.graded += b.graded;
        r.all.hit += b.graded - b.missed;
    }
    return r;
}

} // namespace

TEST_CASE("weighted score: every bar clean is the plain score, 100%") {
    const RunRecord r = run_of({{1, 10, 0, 0}, {2, 10, 0, 0}, {3, 10, 0, 0}, {4, 10, 0, 0}});
    const auto w = weighted_accuracy(r, easy_then_hard());
    REQUIRE(w.has_value());
    CHECK(*w == doctest::Approx(1.0));
    CHECK(*w == doctest::Approx(*r.accuracy(HandFilter::Both)));
}

TEST_CASE("weighted score: misses only in easy bars score higher than the plain score") {
    const RunRecord r = run_of({{1, 10, 5, 0}, {2, 10, 5, 0}, {3, 10, 0, 0}, {4, 10, 0, 0}});
    const auto w = weighted_accuracy(r, easy_then_hard());
    REQUIRE(w.has_value());
    const double plain = *r.accuracy(HandFilter::Both); // 30 / 40
    CHECK(plain == doctest::Approx(0.75));
    CHECK(*w > plain);
    CHECK(*w == doctest::Approx(0.9167).epsilon(0.001)); // (5 + 5 + 50 + 50) / (10 + 10 + 50 + 50)
}

TEST_CASE("weighted score: misses only in hard bars score lower than the plain score") {
    const RunRecord r = run_of({{1, 10, 0, 0}, {2, 10, 0, 0}, {3, 10, 5, 0}, {4, 10, 5, 0}});
    const auto w = weighted_accuracy(r, easy_then_hard());
    REQUIRE(w.has_value());
    const double plain = *r.accuracy(HandFilter::Both);
    CHECK(*w < plain);
    CHECK(*w == doctest::Approx(0.5833).epsilon(0.001)); // (10 + 10 + 25 + 25) / (10 + 10 + 50 + 50)
}

TEST_CASE("weighted score: bars of one difficulty give exactly the plain score") {
    SongDifficulty d;
    d.bars = {3.0f, 3.0f, 3.0f};
    d.numbers = {1, 2, 3};
    const RunRecord r = run_of({{1, 8, 1, 0}, {2, 8, 3, 0}, {3, 4, 0, 0}});
    const auto w = weighted_accuracy(r, d);
    REQUIRE(w.has_value());
    CHECK(*w == doctest::Approx(*r.accuracy(HandFilter::Both)).epsilon(1e-12));
}

TEST_CASE("weighted score: a bar the rating lacks counts as the mean of the rated bars") {
    SongDifficulty d;
    d.bars = {2.0f, 4.0f};
    d.numbers = {1, 2};
    // Bar 9 is not in the rating: it weighs 3, the mean of 2 and 4.
    const RunRecord r = run_of({{1, 10, 0, 0}, {2, 10, 0, 0}, {9, 10, 10, 0}});
    const auto w = weighted_accuracy(r, d);
    REQUIRE(w.has_value());
    CHECK(*w == doctest::Approx(60.0 / 90.0));
}

TEST_CASE("weighted score: nothing graded, or no rated bar, has no weighted score") {
    CHECK_FALSE(weighted_accuracy(run_of({}), easy_then_hard()).has_value());
    CHECK_FALSE(weighted_accuracy(run_of({{1, 0, 0, 0}}), easy_then_hard()).has_value());
    CHECK_FALSE(weighted_accuracy(run_of({{1, 10, 2, 0}}), SongDifficulty{}).has_value());
    CHECK_FALSE(weighted_accuracy(run_of({{7, 10, 2, 0}}), easy_then_hard()).has_value());
}
