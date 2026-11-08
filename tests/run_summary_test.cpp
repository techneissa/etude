// SPDX-License-Identifier: GPL-3.0-or-later
// The summary of a run (run_summary.hpp): timing spread, worst bars and their practice, wrong-note
// spots and the two hands, for runs at other speeds, transposed, hand-filtered, looped, with
// repeats, stopped half way, and older ones that kept only their record.
#include "replay_test_support.hpp"

#include "etude/run_summary.hpp"
#include "etude/trouble_run.hpp"

#include <doctest/doctest.h>

#include <cstdlib>

using namespace replay_test;

namespace {

ReplaySong song_of(const Fixture& f) { return {f.notes, f.measures}; }

// A flow run of the first notes of the fixture, each with the next of `errors`, graded by size.
RunEvents presses_with_errors(const Fixture& f, std::vector<int> errors) {
    std::vector<RunEvent> events;
    int note = 0;
    for (const int err : errors) {
        const NoteGrade g = std::abs(err) <= 30 ? NoteGrade::Perfect
                            : err < 0           ? NoteGrade::Early
                                                : NoteGrade::Late;
        const Note& n = f.notes[static_cast<std::size_t>(note)];
        events.push_back(
            press(note, static_cast<int>(n.start_s * 1000.0) + err, err, g, n.pitch, n.hand));
        ++note;
    }
    return run_of(std::move(events));
}

RunSummary summary(const Fixture& f, const RunEvents& run, RunRecord record = record_of()) {
    return summarize_run(record, run, song_of(f));
}

} // namespace

TEST_CASE("summary: the timing spread has a median, a middle-half spread and the early and late "
          "sides apart") {
    const Fixture f = make_fixture();
    // Seven errors: sorted -30 -20 -10 0 10 20 40.
    const RunSummary s = summary(f, presses_with_errors(f, {20, -30, 0, 40, -10, 10, -20}));
    const TimingSpread& t = s.timing;
    CHECK(t.samples == 7);
    CHECK(t.early == 3);
    CHECK(t.late == 3); // the press at exactly 0 is neither
    CHECK(t.median_ms == 0);
    CHECK(t.spread_ms == 40); // Q1 -20 (median of -30 -20 -10), Q3 20 (of 10 20 40)
    CHECK(t.median_early_ms == -20);
    CHECK(t.median_late_ms == 20);
    CHECK(t.tendency == Tendency::Steady);
    CHECK(t.valid());
}

TEST_CASE("summary: an even count's median is the middle pair's mean, rounded away from zero") {
    const Fixture f = make_fixture();
    CHECK(summary(f, presses_with_errors(f, {-3, -2})).timing.median_ms == -3);
    CHECK(summary(f, presses_with_errors(f, {2, 3})).timing.median_ms == 3);
    CHECK(summary(f, presses_with_errors(f, {-4, 4})).timing.median_ms == 0);
    CHECK(summary(f, presses_with_errors(f, {-10, 30})).timing.spread_ms == 40); // one per half
    CHECK(summary(f, presses_with_errors(f, {7})).timing.spread_ms == 0);        // one: no spread
}

TEST_CASE("summary: a lean is a median of kTendencyMs or more, on either side, not just under") {
    const Fixture f = make_fixture();
    CHECK(summary(f, presses_with_errors(f, {-15})).timing.tendency == Tendency::Early);
    CHECK(summary(f, presses_with_errors(f, {-14})).timing.tendency == Tendency::Steady);
    CHECK(summary(f, presses_with_errors(f, {14})).timing.tendency == Tendency::Steady);
    CHECK(summary(f, presses_with_errors(f, {15})).timing.tendency == Tendency::Late);
}

TEST_CASE("summary: Wait Mode has no timing: the errors it stores are not read") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({press(0, 0, 400, NoteGrade::Played, 72, Hand::Right),
                                  press(1, 1000, 600, NoteGrade::Played, 74, Hand::Right)});
    const RunSummary s = summary(f, run, record_of(RunMode::Wait));
    CHECK_FALSE(s.timing.valid());
    CHECK(s.counts.played == 2);
    CHECK(s.hit == 2);
    CHECK(s.accuracy_pct == 100);
}

TEST_CASE("summary: a run at another speed keeps its setup and reads the errors as wall time") {
    const Fixture f = make_fixture();
    const RunSummary s = summary(f, presses_with_errors(f, {-40, -40, -40}),
                                 record_of(RunMode::Flow, HandFilter::Both, 50));
    CHECK(s.setup.speed_percent == 50);
    CHECK(s.timing.median_ms == -40); // not halved: the grade was decided on wall time
    CHECK(s.timing.tendency == Tendency::Early);
}

TEST_CASE("summary: the counts, the accuracy and the bars come from the played notes") {
    const Fixture f = make_fixture();
    // Bar 1: all three right. Bar 2: one missed. Bar 3: two missed and a wrong press.
    const RunEvents run = run_of(
        {perfect(f, 0), perfect(f, 1), perfect(f, 2), perfect(f, 3), miss(4, 3000, 74, Hand::Right),
         perfect(f, 5), miss(6, 4000, 72, Hand::Right), miss(7, 5000, 74, Hand::Right),
         perfect(f, 8), wrong(4500, 61)});
    const RunSummary s = summary(f, run);
    CHECK(s.detail == SummaryDetail::Played);
    CHECK(s.graded == 9);
    CHECK(s.hit == 6);
    CHECK(s.missed == 3);
    CHECK(s.wrong == 1);
    CHECK(s.counts.perfect == 6);
    CHECK(s.counts.missed == 3);
    CHECK(s.accuracy_pct == 67);
    REQUIRE(s.bar_count == 2);
    CHECK(s.bars[0].measure == 3); // two missed and half a wrong press
    CHECK(s.bars[0].missed == 2);
    CHECK(s.bars[0].wrong == 1);
    CHECK(s.bars[0].graded == 3);
    CHECK(s.bars[1].measure == 2);
    CHECK(s.bars[1].missed == 1);
}

TEST_CASE("summary: a clean run says 100; one miss in 200 is 99, never rounded up to 100") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 12; ++i) {
        events.push_back(perfect(f, i));
    }
    const RunSummary clean = summary(f, run_of(events, 12, true));
    CHECK(clean.accuracy_pct == 100);
    CHECK(clean.bar_count == 0);
    CHECK(clean.complete);
    CHECK(clean.coverage_pct == 100);
    std::vector<RunEvent> many;
    for (int i = 0; i < 199; ++i) {
        many.push_back(perfect(f, 0));
    }
    many.push_back(miss(1, 1000, 74, Hand::Right));
    CHECK(summary(f, run_of(many)).accuracy_pct == 99);
}

TEST_CASE("summary: bars that tie go to the earlier one, and two wrong presses are one miss") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({wrong(100, 60), wrong(200, 61), miss(3, 2000, 72, Hand::Right),
                                  miss(9, 6000, 72, Hand::Right)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.bar_count == 3);
    CHECK(s.bars[0].measure == 1);
    CHECK(s.bars[1].measure == 2);
    CHECK(s.bars[2].measure == 4);
}

TEST_CASE("summary: each trouble bar links to practising it") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({miss(0, 0, 72, Hand::Right), miss(1, 1000, 74, Hand::Right),
                                  miss(3, 2000, 72, Hand::Right), miss(9, 6000, 72, Hand::Right),
                                  miss(10, 7000, 74, Hand::Right)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.bar_count == 3);
    const BarPractice one =
        plan_summary_practice(s, f.measures, f.song.duration_s, BarPracticeKind::OneByOne);
    REQUIRE(one.count == 3);
    CHECK(one.loops[0].first_measure == 1); // in song order
    CHECK(one.loops[0].start_s == 0.0);
    CHECK(one.loops[0].end_s == 2.0);
    CHECK(one.loops[1].first_measure == 2);
    CHECK(one.loops[2].first_measure == 4);
    CHECK(one.loops[2].end_s == 8.0);
    const BarPractice first =
        plan_summary_practice(s, f.measures, f.song.duration_s, BarPracticeKind::OneByOne, 1);
    REQUIRE(first.count == 1);
    CHECK(first.loops[0].first_measure == 1); // the worst bar: two misses, the earlier of the tie
    const BarPractice together =
        plan_summary_practice(s, f.measures, f.song.duration_s, BarPracticeKind::Together);
    REQUIRE(together.count == 2); // bars 1 and 2 join, bar 4 stands alone
    CHECK(together.loops[0].last_measure == 2);
    CHECK(plan_summary_practice(s, f.measures, f.song.duration_s, BarPracticeKind::OneByOne, 0)
              .count == 0);
}

TEST_CASE("summary: a bar too short to loop is dropped from the practice, and counted") {
    Fixture f = make_fixture();
    Measure tiny;
    tiny.number = 5;
    tiny.start_s = 8.0;
    f.measures.push_back(tiny);
    const RunEvents run = run_of({miss(99, 8010, 72, Hand::Right)}); // bar 5, by its time
    const RunSummary s = summary(f, run);
    REQUIRE(s.bar_count == 1);
    CHECK(s.bars[0].measure == 5);
    const BarPractice plan = plan_summary_practice(s, f.measures, 8.05, BarPracticeKind::OneByOne);
    CHECK(plan.count == 0);
    CHECK(plan.dropped == 1);
}

TEST_CASE("summary: a wrong note is placed in its bar with the key it was probably meant for") {
    const Fixture f = make_fixture();
    // Bar 2 (2000..4000): F#4 pressed at 3050 where the note at 3000 wants G4 (74 here, 73 pressed);
    // a second wrong press in the same bar; one in bar 4 far from any note.
    const RunEvents run = run_of({perfect(f, 4), wrong(3050, 73), wrong(3600, 60),
                                  perfect(f, 10), wrong(6700, 59)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.wrong_spots.size() == 2);
    const WrongSpot& busy = s.wrong_spots[0];
    CHECK(busy.measure == 2);
    CHECK(busy.count == 2);
    CHECK(busy.first_ms == 3050);
    CHECK(busy.key == 73);
    CHECK(busy.expected_key == 74);
    const WrongSpot& far = s.wrong_spots[1];
    CHECK(far.measure == 4);
    CHECK(far.count == 1);
    CHECK(far.expected_key == 74); // the note at 7000 is 300 ms off, which is still within reach
}

TEST_CASE("summary: a wrong note farther than the reach from every note names no key") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 4), wrong(3000 + kWrongSpotReachMs, 61),
                                  wrong(3000 - kWrongSpotReachMs - 1, 62)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.wrong_spots.size() == 1);
    CHECK(s.wrong_spots[0].count == 2);
    CHECK(s.wrong_spots[0].key == 62); // the first in time names the spot
    CHECK(s.wrong_spots[0].expected_key == -1);
    // Exactly at the reach is near.
    const RunEvents at = run_of({perfect(f, 4), wrong(3000 + kWrongSpotReachMs, 61)});
    CHECK(summary(f, at).wrong_spots[0].expected_key == 74);
}

TEST_CASE("summary: the busiest wrong-note bars come first, at most kMaxWrongSpots of them") {
    Fixture f = make_fixture(10);
    std::vector<RunEvent> events;
    for (int bar = 0; bar < 8; ++bar) {
        for (int k = 0; k <= bar; ++k) {
            events.push_back(wrong(2000 * bar + 10 * k, 60));
        }
    }
    const RunSummary s = summary(f, run_of(events));
    REQUIRE(s.wrong_spots.size() == static_cast<std::size_t>(kMaxWrongSpots));
    CHECK(s.wrong_spots[0].measure == 8); // 8 wrong presses
    CHECK(s.wrong_spots[0].count == 8);
    CHECK(s.wrong_spots.back().count == 8 - kMaxWrongSpots + 1);
}

TEST_CASE("summary: a wrong note in a transposed run reports the keys it was played on") {
    const Fixture f = make_fixture();
    // Up a tone: note 0 (72) is played on 74; a wrong 75 beside it.
    const RunEvents run = run_of({press(0, 0, 0, NoteGrade::Perfect, 74, Hand::Right),
                                  wrong(40, 75)});
    const RunSummary s = summary(f, run, record_of(RunMode::Flow, HandFilter::Both, 100, 2));
    REQUIRE(s.wrong_spots.size() == 1);
    CHECK(s.wrong_spots[0].key == 75);
    CHECK(s.wrong_spots[0].expected_key == 74);
    CHECK(s.setup.transpose_semitones == 2);
}

TEST_CASE("summary: the hands side by side, the weaker one named when they differ by ten points") {
    const Fixture f = make_fixture();
    // Right: 8 notes, 8 hit. Left: 4 notes, 3 hit (75%).
    std::vector<RunEvent> events;
    for (int bar = 0; bar < 4; ++bar) {
        events.push_back(perfect(f, 3 * bar));
        events.push_back(perfect(f, 3 * bar + 1));
        events.push_back(bar == 0 ? miss(2, 0, 48, Hand::Left) : perfect(f, 3 * bar + 2));
    }
    const RunSummary s = summary(f, run_of(events));
    CHECK(s.hand(Hand::Right).practised);
    CHECK(s.hand(Hand::Right).graded == 8);
    CHECK(s.hand(Hand::Right).accuracy_pct == 100);
    CHECK(s.hand(Hand::Left).graded == 4);
    CHECK(s.hand(Hand::Left).hit == 3);
    CHECK(s.hand(Hand::Left).missed == 1);
    CHECK(s.hand(Hand::Left).accuracy_pct == 75);
    CHECK(s.weaker_hand == Hand::Left);
}

TEST_CASE("summary: the gap between the hands is named at exactly ten points, not at nine") {
    const Fixture f = make_fixture();
    const auto weaker = [&](RunHand left, RunHand right) {
        RunRecord record = record_of();
        record.left = left;
        record.right = right;
        record.all = {left.graded + right.graded, left.hit + right.hit};
        return summary(f, RunEvents{}, record).weaker_hand;
    };
    CHECK(weaker({10, 9}, {10, 10}) == Hand::Left);     // 90% against 100%: ten points
    CHECK(weaker({10, 10}, {10, 9}) == Hand::Right);
    CHECK(weaker({11, 10}, {10, 10}) == Hand::Unknown); // 91% against 100%: nine
    CHECK(weaker({10, 10}, {10, 10}) == Hand::Unknown);
}

TEST_CASE("summary: a hand-filtered run has one hand practised and the other none, not 0%") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 2), perfect(f, 5), miss(8, 4000, 48, Hand::Left)});
    const RunSummary s = summary(f, run, record_of(RunMode::Flow, HandFilter::Left));
    CHECK(s.setup.hands == HandFilter::Left);
    CHECK(s.hand(Hand::Left).practised);
    CHECK_FALSE(s.hand(Hand::Right).practised);
    CHECK(s.hand(Hand::Right).accuracy_pct == -1);
    CHECK(s.weaker_hand == Hand::Unknown);
    CHECK(s.accuracy_pct == 67);
}

TEST_CASE("summary: too few notes in a hand name no weaker one") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 0), perfect(f, 1), perfect(f, 3), perfect(f, 4),
                                  miss(2, 0, 48, Hand::Left), miss(5, 2000, 48, Hand::Left)});
    CHECK(summary(f, run).weaker_hand == Hand::Unknown); // the left hand graded 2 notes
}

TEST_CASE("summary: each hand's own timing lean is its median error") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    events.push_back(perfect(f, 0, -25));
    events.push_back(perfect(f, 1, -15));
    events.push_back(perfect(f, 2, 20));
    const RunSummary s = summary(f, run_of(events));
    CHECK(s.hand(Hand::Right).median_ms == -20);
    CHECK(s.hand(Hand::Right).timing_samples == 2);
    CHECK(s.hand(Hand::Left).median_ms == 20);
}

TEST_CASE("summary: a run stopped half way covers what it played and says how much") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 6; ++i) {
        events.push_back(i == 4 ? miss(4, 3000, 74, Hand::Right) : perfect(f, i));
    }
    const RunSummary s = summary(f, run_of(events, 12));
    CHECK(s.stopped_early);
    CHECK_FALSE(s.complete);
    CHECK(s.coverage_pct == 50);
    CHECK(s.graded == 6);          // the six it reached, not the twelve it was to grade
    CHECK(s.accuracy_pct == 83);
}

TEST_CASE("summary: a loop sums its passes into the bars and is not called stopped") {
    const Fixture f = make_fixture();
    // Bar 2 looped three times: a miss in two of the passes.
    std::vector<RunEvent> events;
    for (int pass = 0; pass < 3; ++pass) {
        events.push_back(pass == 1 ? perfect(f, 3) : miss(3, 2000, 72, Hand::Right));
    }
    const RunSummary s = summary(f, run_of(events, 12));
    CHECK(s.looped);
    CHECK_FALSE(s.stopped_early);
    REQUIRE(s.bar_count == 1);
    CHECK(s.bars[0].measure == 2);
    CHECK(s.bars[0].graded == 3);
    CHECK(s.bars[0].missed == 2);
}

TEST_CASE("summary: a song with repeats names a bar by its written number and which time through") {
    Fixture f = make_fixture();
    f.measures[2].source_number = 1; // bars 3 and 4 play the written bars 1 and 2 again
    f.measures[3].source_number = 2;
    const RunEvents run = run_of({miss(0, 0, 72, Hand::Right), miss(6, 4000, 72, Hand::Right),
                                  miss(6, 4000, 72, Hand::Right)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.bar_count == 2);
    CHECK(s.bars[0].measure == 3);
    CHECK(s.bars[0].written == 1);
    CHECK(s.bars[0].pass == 2); // the second time through written bar 1
    CHECK(s.bars[1].measure == 1);
    CHECK(s.bars[1].written == 1);
    CHECK(s.bars[1].pass == 1);
}

TEST_CASE("summary: an older run with no played notes still has its record's summary") {
    const Fixture f = make_fixture();
    RunRecord record = record_of();
    record.counts = {.perfect = 5, .good = 2, .early = 0, .late = 1, .missed = 4, .played = 0};
    record.wrong = 3;
    record.left = {.graded = 6, .hit = 5};
    record.right = {.graded = 6, .hit = 4};
    record.all = {.graded = 12, .hit = 9};
    record.measures = {{1, 4, 0, 0}, {2, 4, 3, 1}, {3, 4, 1, 2}};
    const RunSummary s = summary(f, RunEvents{}, record);
    CHECK(s.detail == SummaryDetail::RecordOnly);
    CHECK(s.graded == 12);
    CHECK(s.hit == 9);
    CHECK(s.missed == 4);
    CHECK(s.wrong == 3);
    CHECK(s.accuracy_pct == 75);
    CHECK_FALSE(s.timing.valid());
    CHECK(s.wrong_spots.empty());
    CHECK(s.coverage_pct == -1);
    REQUIRE(s.bar_count == 2);
    CHECK(s.bars[0].measure == 2); // 3 missed + half a wrong press
    CHECK(s.bars[1].measure == 3);
    CHECK(s.hand(Hand::Left).accuracy_pct == 83);
    CHECK(s.hand(Hand::Right).accuracy_pct == 67);
    CHECK(s.hand(Hand::Right).missed == 2);
    CHECK(s.weaker_hand == Hand::Right);
}

TEST_CASE("summary: a run that graded nothing is Nothing, with no figures") {
    const Fixture f = make_fixture();
    const RunSummary s = summary(f, RunEvents{}, record_of());
    CHECK(s.detail == SummaryDetail::Nothing);
    CHECK(s.accuracy_pct == -1);
    CHECK(s.bar_count == 0);
    CHECK(s.hand(Hand::Left).accuracy_pct == -1);
}

TEST_CASE("summary: events the recorder could not hold are reported, and the rest is summed") {
    const Fixture f = make_fixture();
    RunEvents run = run_of({perfect(f, 0), perfect(f, 1)}, 12);
    run.dropped = 40;
    const RunSummary s = summary(f, run);
    CHECK(s.dropped_events == 40);
    CHECK(s.graded == 2);
    CHECK_FALSE(s.complete);
}

TEST_CASE("summary: a note the song does not have still counts, in the bar of its time") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({miss(500, 4100, 60, Hand::Left)});
    const RunSummary s = summary(f, run);
    CHECK(s.graded == 1);
    REQUIRE(s.bar_count == 1);
    CHECK(s.bars[0].measure == 3);
    CHECK(s.hand(Hand::Left).missed == 1);
}

TEST_CASE("summary: build_run_replay gives the timeline and the summary of the same run") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 0), miss(1, 1000, 74, Hand::Right), wrong(300, 61)}, 12);
    const RunReplay replay = build_run_replay(record_of(), run, song_of(f));
    CHECK(replay.timeline.notes.size() == 3);
    CHECK(replay.summary.graded == 2);
    CHECK(replay.summary.wrong == 1);
    CHECK(replay.summary.stopped_early);
    CHECK(replay.timeline.stopped_early);
}

TEST_CASE("summary: the span overload ranks bars as the record's does") {
    const std::vector<RunBar> bars = {{1, 6, 2, 0}, {2, 6, 3, 2}, {4, 8, 1, 0}};
    RunRecord record;
    record.measures = bars;
    int a = 0;
    int b = 0;
    const auto from_record = worst_bars(record, &a);
    const auto from_span = worst_bars(std::span<const RunBar>(bars), &b);
    CHECK(a == b);
    CHECK(from_record == from_span);
    CHECK(from_span[0].measure == 2);
}

TEST_CASE("summary: every bar played is listed in song order, for the run bar by bar") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 6), miss(0, 0, 72, Hand::Right), wrong(2500, 61)});
    const RunSummary s = summary(f, run);
    REQUIRE(s.played_bars.size() == 3);
    CHECK(s.played_bars[0].measure == 1);
    CHECK(s.played_bars[0].missed == 1);
    CHECK(s.played_bars[1].measure == 2);
    CHECK(s.played_bars[1].wrong == 1);
    CHECK(s.played_bars[1].graded == 0);
    CHECK(s.played_bars[2].measure == 3);
    CHECK(s.played_bars[2].graded == 1);
    // A record-only summary lists the record's bars.
    RunRecord record = record_of();
    record.all = {4, 3};
    record.measures = {{1, 2, 0, 0}, {2, 2, 1, 0}};
    CHECK(summary(f, RunEvents{}, record).played_bars.size() == 2);
}

TEST_CASE("summary: the trouble bars of a plan are named from the summary's own bars") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({miss(3, 2000, 72, Hand::Right), miss(4, 3000, 74, Hand::Right),
                                  wrong(2100, 61)});
    const RunSummary s = summary(f, run);
    const BarPractice plan =
        plan_summary_practice(s, f.measures, f.song.duration_s, BarPracticeKind::OneByOne);
    const TroubleBars named = trouble_bars(
        std::span<const CardBar>(s.bars.data(), static_cast<std::size_t>(s.bar_count)), plan);
    REQUIRE(named.count == 1);
    CHECK(named.bar[0].measure == 2);
    CHECK(named.bar[0].written == 2);
    CHECK(named.bar[0].before_pct == bar_accuracy_pct(2, 2, 1));
    // A loop of a bar the summary does not name has its number and nothing else.
    const BarPractice other =
        plan_bar_practice(std::vector<int>{4}, f.measures, f.song.duration_s, BarPracticeKind::OneByOne);
    const TroubleBars bare = trouble_bars(
        std::span<const CardBar>(s.bars.data(), static_cast<std::size_t>(s.bar_count)), other);
    REQUIRE(bare.count == 1);
    CHECK(bare.bar[0].measure == 4);
    CHECK(bare.bar[0].before_pct == -1);
}
