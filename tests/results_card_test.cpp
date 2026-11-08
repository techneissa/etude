// SPDX-License-Identifier: GPL-3.0-or-later
// results_card: the first topics (split from results_card_test.cpp).
#include "etude/results_card.hpp"
#include "ui/results_card_layout.hpp"
#include "etude/look.hpp"
#include "etude/screen_layout.hpp"
#include <doctest/doctest.h>
#include <cmath>
#include <vector>

#include "results_card_test_support.hpp"

using namespace etude;
using namespace results_card_test_support;

namespace {

RunRecord run_with(std::initializer_list<RunBar> bars) {
    RunRecord r;
    r.hands = HandFilter::Both;
    r.mode = RunMode::Flow;
    r.measures = bars;
    return r;
}

} // namespace

// ---- Worst bars ---------------------------------------------------------------------------------

TEST_CASE("worst bars: the three with the most misses, most first") {
    const RunRecord r =
        run_with({{1, 8, 1, 0}, {2, 8, 4, 0}, {3, 8, 2, 0}, {4, 8, 3, 0}, {5, 8, 0, 0}});
    int n = 0;
    const auto bars = worst_bars(r, &n, 3);
    REQUIRE(n == 3);
    CHECK(bars[0].measure == 2);
    CHECK(bars[1].measure == 4);
    CHECK(bars[2].measure == 3);
    CHECK(bars[0].missed == 4);
}

TEST_CASE("worst bars: a tie goes to the earlier bar") {
    const RunRecord r = run_with({{1, 8, 2, 0}, {2, 8, 2, 0}, {3, 8, 2, 0}, {4, 8, 2, 0}});
    int n = 0;
    const auto bars = worst_bars(r, &n, 3);
    REQUIRE(n == 3);
    CHECK(bars[0].measure == 1);
    CHECK(bars[1].measure == 2);
    CHECK(bars[2].measure == 3);
}

TEST_CASE("worst bars: a clean run names none") {
    int n = -1;
    worst_bars(run_with({{1, 8, 0, 0}, {2, 8, 0, 0}}), &n);
    CHECK(n == 0);
    worst_bars(run_with({}), &n);
    CHECK(n == 0);
}

TEST_CASE("worst bars: wrong presses count half a miss, so Wait Mode names its fumbles") {
    // Bar 2: 3 wrong presses (1.5) against bar 1: one miss (1) and bar 3: two wrong (1).
    const RunRecord r = run_with({{1, 4, 1, 0}, {2, 4, 0, 3}, {3, 4, 0, 2}});
    int n = 0;
    const auto bars = worst_bars(r, &n);
    REQUIRE(n == 3);
    CHECK(bars[0].measure == 2);
    CHECK(bars[0].wrong == 3);
    CHECK(bars[1].measure == 1); // a tie at 1.0: the earlier bar
    CHECK(bars[2].measure == 3);
}

TEST_CASE("worst bars: fewer than the cap, and the cap can be lowered") {
    const RunRecord r = run_with({{1, 4, 1, 0}, {2, 4, 2, 0}});
    int n = 0;
    worst_bars(r, &n);
    CHECK(n == 2);
    const auto one = worst_bars(r, &n, 1);
    CHECK(n == 1);
    CHECK(one[0].measure == 2);
    worst_bars(r, &n, 0);
    CHECK(n == 0);
    worst_bars(r, &n, 99);
    CHECK(n == 2); // never more than kMaxCardBars
}

TEST_CASE("worst bars: up to six on a big card, the hottest first, never more") {
    RunRecord r;
    for (int i = 1; i <= 9; ++i) {
        r.measures.push_back({i, 8, i, 0}); // bar i missed i times
    }
    int n = 0;
    const auto bars = worst_bars(r, &n);
    REQUIRE(n == kMaxCardBars);
    CHECK(kMaxCardBars == 6);
    CHECK(bars[0].measure == 9);
    CHECK(bars[5].measure == 4);
    worst_bars(r, &n, 99);
    CHECK(n == kMaxCardBars);
}

TEST_CASE("worst bars: a pickup bar (0) is a bar like another") {
    int n = 0;
    const auto bars = worst_bars(run_with({{0, 2, 2, 0}, {1, 8, 1, 0}}), &n);
    REQUIRE(n == 2);
    CHECK(bars[0].measure == 0);
}

// ---- Hand choice --------------------------------------------------------------------------------

TEST_CASE("hand choice: a hand is offered when it has notes; both needs both") {
    CHECK(hand_offered(kBoth, HandFilter::Left));
    CHECK(hand_offered(kBoth, HandFilter::Right));
    CHECK(hand_offered(kBoth, HandFilter::Both));
    const HandsPresent right_only{false, true};
    CHECK_FALSE(hand_offered(right_only, HandFilter::Left));
    CHECK(hand_offered(right_only, HandFilter::Right));
    CHECK_FALSE(hand_offered(right_only, HandFilter::Both));
    const HandsPresent left_only{true, false};
    CHECK(hand_offered(left_only, HandFilter::Left));
    CHECK_FALSE(hand_offered(left_only, HandFilter::Right));
    CHECK_FALSE(hand_offered(left_only, HandFilter::Both));
}

// ---- The card -----------------------------------------------------------------------------------

TEST_CASE("card: accuracy and each hand's figure when both hands were practised") {
    const ResultsCard c = make_results_card(flow_run(), first_try(), true, kBoth, false);
    CHECK(c.accuracy_pct == 90); // 43 / 48
    CHECK(c.right_pct == 89);    // 25 / 28
    CHECK(c.left_pct == 90);     // 18 / 20
    CHECK(c.counts.perfect == 30);
    CHECK(c.wrong == 4);
    CHECK(c.longest_combo == 21);
    CHECK_FALSE(c.ungraded);
}

TEST_CASE("card: one hand practised shows its accuracy and no hand split") {
    RunRecord r = flow_run();
    r.hands = HandFilter::Right;
    const ResultsCard c = make_results_card(r, first_try(), true, kBoth, false);
    CHECK(c.accuracy_pct == 89);
    CHECK(c.right_pct == -1);
    CHECK(c.left_pct == -1);
}

TEST_CASE("card: a hand that graded nothing is not shown, and no accuracy is a dash, never 100%") {
    RunRecord r = flow_run();
    r.left = {};
    const ResultsCard c = make_results_card(r, first_try(), true, kBoth, false);
    CHECK(c.right_pct == -1);
    CHECK(c.left_pct == -1);
    RunRecord none;
    none.hands = HandFilter::Both;
    CHECK(make_results_card(none, first_try(), true, kBoth, false).accuracy_pct == -1);
}

TEST_CASE("card: the comparison with the last run") {
    AttemptChange better;
    better.has_previous = true;
    better.accuracy = 0.90;
    better.previous_accuracy = 0.86;
    better.accuracy_delta = 0.04;
    better.trend = Trend::Better;
    ResultsCard c = make_results_card(flow_run(), better, true, kBoth, false);
    CHECK(c.comparison == CardComparison::Better);
    CHECK(c.delta_pct == 4);

    AttemptChange worse;
    worse.has_previous = true;
    worse.accuracy = 0.90; // the run shows 90%
    worse.previous_accuracy = 1.0;
    worse.accuracy_delta = -0.10;
    worse.trend = Trend::Worse;
    c = make_results_card(flow_run(), worse, true, kBoth, false);
    CHECK(c.comparison == CardComparison::Worse);
    CHECK(c.delta_pct == 10);

    AttemptChange tiny; // a better run by less than a point still says "up 1%"
    tiny.has_previous = true;
    tiny.accuracy = 0.904; // shows 90%, as the last run's 89.8% does
    tiny.previous_accuracy = 0.898;
    tiny.accuracy_delta = 0.006;
    tiny.trend = Trend::Better;
    CHECK(make_results_card(flow_run(), tiny, true, kBoth, false).delta_pct == 1);

    // The change is the difference of the two percentages shown: 90.4% against 85.5% is 90 against
    // 86, up 4, not the raw 4.9 rounded to 5.
    AttemptChange rounded;
    rounded.has_previous = true;
    rounded.accuracy = 0.904;
    rounded.previous_accuracy = 0.855;
    rounded.accuracy_delta = 0.049;
    rounded.trend = Trend::Better;
    RunRecord ninety = flow_run();
    ninety.all = {.graded = 1000, .hit = 904};
    const ResultsCard r = make_results_card(ninety, rounded, true, kBoth, false);
    CHECK(r.accuracy_pct == 90);
    CHECK(r.delta_pct == 4);

    AttemptChange same;
    same.has_previous = true;
    same.trend = Trend::Same;
    CHECK(make_results_card(flow_run(), same, true, kBoth, false).comparison ==
          CardComparison::Same);

    CHECK(make_results_card(flow_run(), first_try(), true, kBoth, false).comparison ==
          CardComparison::FirstTry);
}

TEST_CASE("card: while the history loads nothing is compared, not even a best") {
    AttemptChange best;
    best.has_previous = true;
    best.trend = Trend::Better;
    best.accuracy_delta = 0.1;
    best.new_best = true;
    const ResultsCard c = make_results_card(flow_run(), best, false, kBoth, false);
    CHECK(c.comparison == CardComparison::Unknown);
    CHECK_FALSE(c.new_best);
    CHECK(make_results_card(flow_run(), best, true, kBoth, false).new_best);
}

TEST_CASE("card: the named bars carry the written bar number") {
    std::vector<Measure> bars(4);
    for (int i = 0; i < 4; ++i) {
        bars[static_cast<std::size_t>(i)].number = i + 1;
    }
    bars[1].source_number = 2;
    bars[3].source_number = 2; // a repeat: played bar 4 is written bar 2
    const ResultsCard c = make_results_card(flow_run(), first_try(), true, kBoth, false, bars);
    REQUIRE(c.bar_count == 3);
    CHECK(c.bars[0].measure == 2);
    CHECK(c.bars[0].written == 2);
    CHECK(c.bars[1].measure == 1);
    CHECK(c.bars[1].written == 1);
    CHECK(c.bars[2].measure == 4);
    CHECK(c.bars[2].written == 2);
    // Without the song's bars the played number stands in.
    const ResultsCard bare = make_results_card(flow_run(), first_try(), true, kBoth, false);
    CHECK(bare.bars[2].written == 4);
}

namespace {

// A song of eight bars rated by hand: bar 5 the hardest, then 3, then 7 and 8 alike.
SongDifficulty rated_eight() {
    SongDifficulty d;
    d.bars = {1.0f, 1.5f, 3.5f, 2.0f, 4.5f, 1.0f, 3.0f, 3.0f};
    d.numbers = {1, 2, 3, 4, 5, 6, 7, 8};
    return d;
}

RunRecord run_over_eight() {
    RunRecord r = flow_run();
    // Bar 2 had the only trouble; every other bar was played clean.
    r.measures = {{1, 6, 0, 0}, {2, 6, 2, 0}, {3, 6, 0, 0}, {4, 6, 0, 0},
                  {5, 6, 0, 0}, {6, 6, 0, 0}, {7, 6, 0, 0}, {8, 6, 0, 0}};
    return r;
}

} // namespace

TEST_CASE("card: the weighted score is carried only when asked for and the song is rated") {
    const SongDifficulty d = rated_eight();
    // Bar 2 (1.5 stars) lost 2 of 6 notes; the other seven bars were clean.
    const RunRecord r = run_over_eight();
    CHECK(make_results_card(r, first_try(), true, kBoth, false, {}, &d).weighted_pct == -1);
    const ResultsCard on = make_results_card(r, first_try(), true, kBoth, false, {}, &d, true);
    // (sum of weights - 1.5 * 2) / sum of weights, sum = 19.5 stars of 6 notes.
    CHECK(on.weighted_pct == 97); // 1 - 3 / 117
    // The plain score is not touched by the setting.
    CHECK(on.accuracy_pct == make_results_card(r, first_try(), true, kBoth, false).accuracy_pct);
    // No rating: no weighted score, the card is the plain one.
    CHECK(make_results_card(r, first_try(), true, kBoth, false, {}, nullptr, true).weighted_pct ==
          -1);
    const SongDifficulty none;
    CHECK(make_results_card(r, first_try(), true, kBoth, false, {}, &none, true).weighted_pct == -1);
}

TEST_CASE("card: the hardest bars are suggested after the trouble bars, and not chosen") {
    const SongDifficulty d = rated_eight();
    const ResultsCard c =
        make_results_card(run_over_eight(), first_try(), true, kBoth, false, {}, &d);
    REQUIRE(c.bar_count == 3); // bar 2, then two suggestions
    CHECK_FALSE(c.bars[0].suggested);
    CHECK(c.bars[0].measure == 2);
    CHECK(c.bars[0].half_stars == 0);
    CHECK(c.bars[1].suggested);
    CHECK(c.bars[1].measure == 5);
    CHECK(c.bars[1].half_stars == 9); // 4.5 stars
    CHECK(c.bars[1].graded == 6);
    CHECK(c.bars[2].suggested);
    CHECK(c.bars[2].measure == 3);
    CHECK(c.bars[2].half_stars == 7); // 3.5 stars
    // Only the trouble bar is chosen when the card appears.
    CHECK(c.start_bars == 0b001u);
    // A suggestion names no miss and no wrong press.
    CHECK(c.bars[1].missed == 0);
    CHECK(c.bars[1].wrong == 0);
}

TEST_CASE("card: a clean run suggests the hardest bars alone") {
    const SongDifficulty d = rated_eight();
    RunRecord r = run_over_eight();
    r.measures[1] = {2, 6, 0, 0};
    const ResultsCard c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    REQUIRE(c.bar_count == 2);
    CHECK(c.bars[0].measure == 5);
    CHECK(c.bars[1].measure == 3);
    CHECK(c.start_bars == 0u);
    CHECK(card_all_bars(c.bar_count) == 0b11u);
}

TEST_CASE("card: suggestions fill only the room the trouble bars leave") {
    const SongDifficulty d = rated_eight();
    // Five trouble bars leave room for one of the two.
    RunRecord r = run_over_eight();
    for (int bar : {1, 2, 3, 4, 6}) {
        r.measures[static_cast<std::size_t>(bar - 1)].missed = 1;
    }
    ResultsCard c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    CHECK(c.bar_count == 6);
    CHECK(c.bars[5].suggested);
    CHECK(c.bars[5].measure == 5);
    // Six leave none.
    r.measures[6].missed = 1;
    c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    CHECK(c.bar_count == 6);
    for (int i = 0; i < c.bar_count; ++i) {
        CHECK_FALSE(c.bars[static_cast<std::size_t>(i)].suggested);
    }
}

TEST_CASE("card: a hard bar already named as trouble, or not played in the run, is not suggested") {
    const SongDifficulty d = rated_eight();
    RunRecord r = run_over_eight();
    r.measures[4].missed = 2; // bar 5, the hardest, went badly: it is trouble, not a suggestion
    ResultsCard c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    int trouble = 0;
    for (int i = 0; i < c.bar_count; ++i) {
        const CardBar& b = c.bars[static_cast<std::size_t>(i)];
        trouble += b.suggested ? 0 : 1;
        CHECK((b.measure != 5 || !b.suggested));
    }
    CHECK(trouble == 2);
    // A loop run over bars 1 to 4 never suggests a bar outside it.
    r = run_over_eight();
    r.measures.resize(4);
    c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    for (int i = 0; i < c.bar_count; ++i) {
        CHECK(c.bars[static_cast<std::size_t>(i)].measure <= 4);
    }
    CHECK(c.bars[1].measure == 3);
    // A bar with nothing graded in it is not suggested either.
    r = run_over_eight();
    r.measures[4].graded = 0;
    c = make_results_card(r, first_try(), true, kBoth, false, {}, &d);
    for (int i = 0; i < c.bar_count; ++i) {
        CHECK(c.bars[static_cast<std::size_t>(i)].measure != 5);
    }
}

TEST_CASE("suggested bars: a bar with a miss or a wrong press is never suggested, whoever asks") {
    const SongDifficulty d = rated_eight(); // within a star of the peak (4.5): bars 5 and 3
    RunRecord r = run_over_eight();
    int n = 0;
    auto out = suggested_bars(r, d, {}, &n);
    REQUIRE(n == 2);
    CHECK(out[0].measure == 5);
    CHECK(out[1].measure == 3);
    r.measures[4].missed = 1; // bar 5, the hardest, with a miss
    out = suggested_bars(r, d, {}, &n);
    REQUIRE(n == 1);
    CHECK(out[0].measure == 3);
    r.measures[2].wrong = 1; // bar 3 with a wrong press
    suggested_bars(r, d, {}, &n);
    CHECK(n == 0);
}

TEST_CASE("card: no suggestions without ratings, in a practice list or for an easy song") {
    const SongDifficulty d = rated_eight();
    CHECK(make_results_card(run_over_eight(), first_try(), true, kBoth, false).bar_count == 1);
    CHECK(make_results_card(run_over_eight(), first_try(), true, kBoth, true, {}, &d).bar_count ==
          0);
    SongDifficulty easy;
    easy.bars.assign(8, 1.2f);
    easy.numbers = d.numbers;
    CHECK(
        make_results_card(run_over_eight(), first_try(), true, kBoth, false, {}, &easy).bar_count ==
        1);
}

TEST_CASE("card: suggested bars are practised like any other bar") {
    const SongDifficulty d = rated_eight();
    const ResultsCard c =
        make_results_card(run_over_eight(), first_try(), true, kBoth, false, {}, &d);
    REQUIRE(c.bar_count == 3);
    // Choosing the suggestion as well as the trouble bar offers both actions.
    const CardChoice choice = {CardAction::Together, HandFilter::Both, 0b011u};
    CHECK(card_offers(c, choice));
    CHECK(card_offers(c, {CardAction::OneByOne, HandFilter::Both, 0b010u}));
    CHECK_FALSE(card_offers(c, {CardAction::OneByOne, HandFilter::Both, 0b1000u})); // none there
}

TEST_CASE("card: Wait Mode keeps its stats and names its fumbles") {
    RunRecord r = flow_run();
    r.mode = RunMode::Wait;
    r.counts = {.played = 48};
    r.wrong = 6;
    r.measures = {{1, 6, 0, 1}, {2, 6, 0, 4}};
    const ResultsCard c = make_results_card(r, first_try(), true, kBoth, false);
    CHECK(c.mode == RunMode::Wait);
    CHECK(c.counts.played == 48);
    CHECK(c.wrong == 6);
    REQUIRE(c.bar_count == 2);
    CHECK(c.bars[0].measure == 2);
}

namespace {

CardChoice hands_choice(HandFilter hands) { return {CardAction::Hands, hands, 0}; }

CardChoice bars_choice(CardAction action, std::uint32_t bars) {
    return {action, HandFilter::Both, bars};
}

} // namespace

TEST_CASE("hand keys: start an offered hand, hint a grey one, pass when the card has no choice") {
    const ResultsCard both = make_results_card(flow_run(), first_try(), true, kBoth, false);
    const ResultsCard right_only =
        make_results_card(flow_run(), first_try(), true, {false, true}, false);
    const ResultsCard in_plan = make_results_card(flow_run(), first_try(), true, kBoth, true);
    for (const HandFilter h : {HandFilter::Left, HandFilter::Right, HandFilter::Both}) {
        CHECK(card_hand_key(&both, h) == HandKey::Start);
        CHECK(card_hand_key(&in_plan, h) == HandKey::Pass); // the list sets the hands
        CHECK(card_hand_key(nullptr, h) == HandKey::Pass);  // no card: the key works as usual
    }
    CHECK(card_hand_key(&right_only, HandFilter::Right) == HandKey::Start);
    CHECK(card_hand_key(&right_only, HandFilter::Left) == HandKey::Hint);
    CHECK(card_hand_key(&right_only, HandFilter::Both) == HandKey::Hint);
    CHECK(std::string(hand_hint(HandFilter::Left)) == "No left-hand notes here");
    CHECK(std::string(hand_hint(HandFilter::Right)) == "No right-hand notes here");
    CHECK(std::string(hand_hint(HandFilter::Both)) == "Only one hand has notes here");
}

TEST_CASE("card: which buttons apply") {
    ResultsCard c = make_results_card(flow_run(), first_try(), true, kBoth, false);
    CHECK(card_offers(c, {CardAction::Replay}));
    CHECK(card_offers(c, {CardAction::Done}));
    CHECK(c.hand_choice);
    CHECK(c.present == kBoth);
    for (const HandFilter h : {HandFilter::Left, HandFilter::Right, HandFilter::Both}) {
        CHECK(card_offers(c, hands_choice(h)));
    }
    CHECK(card_offers(c, bars_choice(CardAction::OneByOne, 0b001)));
    CHECK(card_offers(c, bars_choice(CardAction::Together, card_all_bars(c.bar_count))));
    CHECK_FALSE(card_offers(c, {CardAction::None}));

    // The bars are the chosen ones: none chosen, or only bits past the card's bars, offers nothing.
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::OneByOne, 0)));
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::Together, 0b1000)));

    // One hand in the song: that hand is offered, the other and both are grey.
    c = make_results_card(flow_run(), first_try(), true, {false, true}, false);
    CHECK_FALSE(card_offers(c, hands_choice(HandFilter::Left)));
    CHECK(card_offers(c, hands_choice(HandFilter::Right)));
    CHECK_FALSE(card_offers(c, hands_choice(HandFilter::Both)));
    CHECK(card_offers(c, {CardAction::Replay}));

    // A clean run: no bar to practise.
    RunRecord clean = flow_run();
    clean.measures = {{1, 6, 0, 0}};
    c = make_results_card(clean, first_try(), true, kBoth, false);
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::OneByOne, 0b1)));
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::Together, 0b1)));
}

TEST_CASE("card: in a practice list there is no hand choice and no trouble bar, and the result is "
          "marked") {
    const ResultsCard c = make_results_card(flow_run(), first_try(), true, kBoth, true);
    CHECK(c.in_plan);
    CHECK_FALSE(c.hand_choice);
    CHECK_FALSE(card_offers(c, hands_choice(HandFilter::Left))); // the list sets the hands
    CHECK(c.bar_count == 0);
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::OneByOne, 0b1)));
    CHECK(card_offers(c, {CardAction::Replay}));
    CHECK(card_offers(c, {CardAction::Done}));
}

TEST_CASE("card: every named bar starts chosen") {
    CHECK(card_all_bars(0) == 0u);
    CHECK(card_all_bars(1) == 0b1u);
    CHECK(card_all_bars(3) == 0b111u);
    CHECK(card_all_bars(6) == 0b111111u);
}

TEST_CASE("card: a repeated passage says which time through it a bar was") {
    // Written bars 1 2 3 2 3: played bars 1..5, written 1, 2, 3, 2, 3.
    std::vector<Measure> bars(5);
    const int written[] = {1, 2, 3, 2, 3};
    for (int i = 0; i < 5; ++i) {
        bars[static_cast<std::size_t>(i)].number = i + 1;
        bars[static_cast<std::size_t>(i)].source_number = written[i];
    }
    RunRecord r = run_with({{1, 4, 1, 0}, {2, 4, 3, 0}, {3, 4, 1, 0}, {4, 4, 2, 0}, {5, 4, 1, 0}});
    const ResultsCard c = make_results_card(r, first_try(), true, kBoth, false, bars);
    REQUIRE(c.bar_count == 5);
    CHECK(c.bars[0].measure == 2); // 3 misses: written bar 2, the first time
    CHECK(c.bars[0].written == 2);
    CHECK(c.bars[0].pass == 1);
    CHECK(c.bars[1].measure == 4); // 2 misses: written bar 2 again
    CHECK(c.bars[1].written == 2);
    CHECK(c.bars[1].pass == 2);
    CHECK(c.bars[2].measure == 1); // a bar played once has no pass
    CHECK(c.bars[2].pass == 0);
    // Without the song's bars nothing is repeated.
    CHECK(make_results_card(r, first_try(), true, kBoth, false).bars[0].pass == 0);
}

TEST_CASE("ordinal suffixes: st, nd, rd, th, and th for the teens") {
    CHECK(std::string(ordinal_suffix(1)) == "st");
    CHECK(std::string(ordinal_suffix(2)) == "nd");
    CHECK(std::string(ordinal_suffix(3)) == "rd");
    CHECK(std::string(ordinal_suffix(4)) == "th");
    CHECK(std::string(ordinal_suffix(11)) == "th");
    CHECK(std::string(ordinal_suffix(12)) == "th");
    CHECK(std::string(ordinal_suffix(13)) == "th");
    CHECK(std::string(ordinal_suffix(21)) == "st");
    CHECK(std::string(ordinal_suffix(22)) == "nd");
    CHECK(std::string(ordinal_suffix(111)) == "th");
}

TEST_CASE("card: a run that graded nothing has no figures, the hand choice, Replay and Done") {
    const ResultsCard c = make_ungraded_card(RunMode::Flow, HandFilter::Left, kBoth, false);
    CHECK(c.ungraded);
    CHECK(c.accuracy_pct == -1);
    CHECK(c.bar_count == 0);
    CHECK(card_offers(c, hands_choice(HandFilter::Both)));
    CHECK_FALSE(card_offers(c, bars_choice(CardAction::OneByOne, 0b1)));
    CHECK_FALSE(card_offers(make_ungraded_card(RunMode::Flow, HandFilter::Left, kBoth, true),
                            hands_choice(HandFilter::Both)));
}
