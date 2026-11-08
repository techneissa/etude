// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/trouble_run.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

constexpr RunStep kNone{};
RunStep play(int bar) { return {RunStep::Kind::Play, bar}; }
RunStep pause(int bar) { return {RunStep::Kind::Pause, bar}; }
constexpr RunStep kEnd{RunStep::Kind::End, -1};

const RunPass kMessy{false, 8, 3, 1};
const RunPass kClean{true, 8, 0, 0};

std::vector<TroubleBar> bars_of(std::initializer_list<int> written) {
    std::vector<TroubleBar> out;
    for (const int w : written) {
        TroubleBar b;
        b.measure = w;
        b.written = w;
        b.before_pct = 50;
        out.push_back(b);
    }
    return out;
}

} // namespace

TEST_CASE("run: starts on the first bar, and no bars is no run") {
    TroubleRun run;
    CHECK(run.phase() == RunPhase::Idle);
    CHECK(run.start({}) == kNone);
    CHECK(run.phase() == RunPhase::Idle);
    const auto bars = bars_of({3, 5, 9});
    CHECK(run.start(bars) == play(0));
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.count() == 3);
    CHECK(run.current() == 0);
    CHECK(run.next_bar() == 1);
    CHECK(run.progress(0).end == BarEnd::Playing);
    CHECK(run.progress(1).end == BarEnd::Waiting);
}

TEST_CASE("run: a pass that is not clean only counts, a clean one clears the bar and waits") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    CHECK(run.on_pass(kMessy) == kNone);
    CHECK(run.on_pass(kMessy) == kNone);
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.on_pass(kClean) == pause(0));
    CHECK(run.phase() == RunPhase::CheckIn);
    const BarProgress& p = run.progress(0);
    CHECK(p.passes == 3);
    CHECK(p.clean_passes == 1);
    CHECK(p.graded == 24);
    CHECK(p.missed == 6);
    CHECK(p.wrong == 2);
    CHECK(p.end == BarEnd::Cleared);
    // 24 graded, 6 missed and 2 wrong (worth one miss): 17 of 24 is 71%.
    CHECK(p.accuracy_pct() == 71);
}

TEST_CASE("run: a pass at the check-in changes nothing") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kClean);
    CHECK(run.on_pass(kMessy) == kNone);
    CHECK(run.progress(0).passes == 1);
}

TEST_CASE("run: Next bar plays the following bar, and after the last the run is finished") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kClean);
    CHECK(run.command(RunCommand::Next) == play(1));
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 1);
    CHECK(run.next_bar() == -1);
    CHECK(run.on_pass(kClean) == pause(1));
    CHECK(run.command(RunCommand::Next) == kEnd);
    CHECK(run.phase() == RunPhase::Done);
    CHECK(run.finished());
    CHECK_FALSE(run.ended());
    CHECK(run.cleared() == 2);
    CHECK(run.skipped() == 0);
}

TEST_CASE("run: Again plays the same bar once more and keeps adding to its numbers") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kMessy);
    run.on_pass(kClean);
    CHECK(run.command(RunCommand::Again) == play(0));
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.current() == 0);
    CHECK(run.progress(0).end == BarEnd::Playing);
    CHECK(run.on_pass(kClean) == pause(0));
    CHECK(run.progress(0).passes == 3);
    CHECK(run.progress(0).clean_passes == 2);
}

TEST_CASE("run: Skip at the check-in passes over the next bar and plays the one after") {
    TroubleRun run;
    run.start(bars_of({3, 5, 9}));
    run.on_pass(kClean);
    CHECK(run.command(RunCommand::Skip) == play(2));
    CHECK(run.progress(1).end == BarEnd::Skipped);
    CHECK(run.progress(1).passes == 0);
    CHECK(run.current() == 2);
    CHECK(run.skipped() == 1);
}

TEST_CASE("run: Skip when the next bar is the last, or there is none, ends the run") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kClean);
    CHECK(run.command(RunCommand::Skip) == kEnd);
    CHECK(run.finished());
    CHECK(run.progress(1).end == BarEnd::Skipped);

    run.start(bars_of({3, 5}));
    run.command(RunCommand::Next);                // skip bar 3 while it plays
    run.on_pass(kClean);                          // bar 5
    // No bar after the last: Skip has nothing to pass over, the check-in stays (as the disabled
    // button does), and Next finishes the run.
    CHECK(run.command(RunCommand::Skip) == kNone);
    CHECK(run.phase() == RunPhase::CheckIn);
    CHECK(run.command(RunCommand::Next) == kEnd);
    CHECK(run.finished());
    CHECK(run.progress(1).end == BarEnd::Cleared);
}

TEST_CASE("run: the grade counts of every pass add up, the combo is the best of any pass") {
    TroubleRun run;
    run.start(bars_of({3}));
    // clean, graded, missed, wrong, perfect, good, early, late, played, combo
    run.on_pass({false, 8, 3, 1, 3, 1, 1, 0, 0, 2});
    run.on_pass({true, 8, 0, 0, 6, 1, 0, 1, 0, 8});
    const BarProgress& p = run.progress(0);
    CHECK(p.perfect == 9);
    CHECK(p.good == 2);
    CHECK(p.early == 1);
    CHECK(p.late == 1);
    CHECK(p.missed == 3);
    CHECK(p.wrong == 1);
    CHECK(p.combo == 8);
    CHECK(p.played == 0);
    // Wait Mode counts the notes played instead of the grades.
    run.start(bars_of({3}));
    run.on_pass({true, 4, 0, 2, 0, 0, 0, 0, 4, 4});
    CHECK(run.progress(0).played == 4);
    CHECK(run.progress(0).wrong == 2);
}

TEST_CASE("run: Skip or Next while a bar plays gives it up and plays the next") {
    TroubleRun run;
    run.start(bars_of({3, 5, 9}));
    run.on_pass(kMessy);
    CHECK(run.command(RunCommand::Skip) == play(1));
    CHECK(run.progress(0).end == BarEnd::Skipped);
    CHECK(run.progress(0).passes == 1); // what was played still counts
    CHECK(run.command(RunCommand::Next) == play(2));
    CHECK(run.progress(1).end == BarEnd::Skipped);
    // Skipping the last bar while it plays finishes the run.
    CHECK(run.command(RunCommand::Skip) == kEnd);
    CHECK(run.finished());
    CHECK(run.cleared() == 0);
    CHECK(run.skipped() == 3);
}

TEST_CASE("run: Again while a bar plays starts it over without touching its numbers") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kMessy);
    CHECK(run.command(RunCommand::Again) == play(0));
    CHECK(run.phase() == RunPhase::Playing);
    CHECK(run.progress(0).passes == 1);
}

TEST_CASE("run: End ends it from a bar or the check-in, and the rest stay unplayed") {
    TroubleRun run;
    run.start(bars_of({3, 5, 9}));
    run.on_pass(kMessy);
    CHECK(run.command(RunCommand::End) == kEnd);
    CHECK(run.phase() == RunPhase::Done);
    CHECK(run.ended());
    CHECK_FALSE(run.finished());
    CHECK(run.progress(0).end == BarEnd::Ended);
    CHECK(run.progress(1).end == BarEnd::Waiting);
    CHECK(run.progress(2).end == BarEnd::Waiting);

    run.start(bars_of({3, 5, 9}));
    run.on_pass(kClean);
    CHECK(run.command(RunCommand::End) == kEnd);
    CHECK(run.ended());
    CHECK(run.progress(0).end == BarEnd::Cleared); // a cleared bar stays cleared
    CHECK(run.cleared() == 1);
}

TEST_CASE("run: a finished or ended run, or none, takes no command and no pass") {
    TroubleRun run;
    CHECK(run.command(RunCommand::Next) == kNone);
    CHECK(run.on_pass(kClean) == kNone);
    run.start(bars_of({3}));
    run.command(RunCommand::End);
    for (const RunCommand c :
         {RunCommand::Next, RunCommand::Again, RunCommand::Skip, RunCommand::End}) {
        CHECK(run.command(c) == kNone);
    }
    CHECK(run.on_pass(kClean) == kNone);
    CHECK(run.phase() == RunPhase::Done); // the summary stays until reset
    run.reset();
    CHECK(run.phase() == RunPhase::Idle);
    CHECK(run.count() == 0);
}

TEST_CASE("run: abandoning it from outside is an end, and does nothing when none runs") {
    TroubleRun run;
    CHECK(run.abandon() == kNone);
    run.start(bars_of({3, 5}));
    CHECK(run.abandon() == kEnd);
    CHECK(run.ended());
    CHECK(run.abandon() == kNone);
}

TEST_CASE("run: starting again forgets the last run") {
    TroubleRun run;
    run.start(bars_of({3, 5}));
    run.on_pass(kClean);
    run.command(RunCommand::End);
    CHECK(run.start(bars_of({7})) == play(0));
    CHECK(run.count() == 1);
    CHECK(run.progress(0).passes == 0);
    CHECK_FALSE(run.ended());
    CHECK(run.bar(0).written == 7);
}

TEST_CASE("run: at most six bars are taken") {
    TroubleRun run;
    run.start(bars_of({1, 2, 3, 4, 5, 6, 7, 8}));
    CHECK(run.count() == kMaxRunBars);
}

TEST_CASE("run: a full run with a reorder, a skip and an end") {
    // The card named bars 12, 3 and 8; the player put 8 first.
    TroubleRun run;
    const auto bars = bars_of({8, 3, 12});
    REQUIRE(run.start(bars) == play(0));
    CHECK(run.bar(0).written == 8);
    run.on_pass(kMessy);
    CHECK(run.on_pass(kClean) == pause(0));          // bar 8 cleared on its second pass
    CHECK(run.command(RunCommand::Next) == play(1)); // bar 3
    CHECK(run.command(RunCommand::Skip) == play(2)); // gave bar 3 up
    CHECK(run.command(RunCommand::End) == kEnd);     // stopped on bar 12
    CHECK(run.ended());
    CHECK(run.progress(0).end == BarEnd::Cleared);
    CHECK(run.progress(1).end == BarEnd::Skipped);
    CHECK(run.progress(2).end == BarEnd::Ended);
    CHECK(run.progress(0).passes == 2);
}

TEST_CASE("bar accuracy: errors cost, a wrong press half a miss, never 100 with an error") {
    CHECK(bar_accuracy_pct(0, 0, 0) == -1);
    CHECK(bar_accuracy_pct(8, 0, 0) == 100);
    CHECK(bar_accuracy_pct(8, 2, 0) == 75);
    CHECK(bar_accuracy_pct(8, 0, 2) == 88); // one miss's worth: 7 of 8 is 87.5
    CHECK(bar_accuracy_pct(8, 8, 4) == 0);
    CHECK(bar_accuracy_pct(1000, 1, 0) == 99); // 99.9% shows as 99, not 100
}

TEST_CASE("trouble bars: the loops in their order, with the card's words and the before figure") {
    ResultsCard c;
    c.bars[0] = {9, 9, 2, 4, 0, 8};  // bar 9, the 2nd time through, 4 of 8 missed
    c.bars[1] = {3, 3, 0, 1, 2, 10}; // 1 missed and 2 wrong of 10
    c.bar_count = 2;
    BarPractice p;
    p.count = 3;
    p.loops[0] = {2.0, 4.0, 3, 3};
    p.loops[1] = {6.0, 8.0, 9, 9};
    p.loops[2] = {10.0, 12.0, 14, 14}; // not on the card
    const TroubleBars t = trouble_bars(c, p);
    REQUIRE(t.count == 3);
    CHECK(t.bar[0].measure == 3);
    CHECK(t.bar[0].before_pct == 80); // 10 graded, 1 + 1 errors
    CHECK(t.bar[1].measure == 9);
    CHECK(t.bar[1].pass == 2);
    CHECK(t.bar[1].before_pct == 50);
    CHECK(t.bar[2].written == 14);
    CHECK(t.bar[2].before_pct == -1);
}
