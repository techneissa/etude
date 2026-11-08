// SPDX-License-Identifier: GPL-3.0-or-later
// The replay of a run (run_replay.hpp): the played notes laid over the song's expected ones, for
// runs at other speeds, transposed, hand-filtered, looped, stopped half way and older ones, and the
// player that walks them at any speed.
#include "alloc_count.hpp"
#include "replay_test_support.hpp"

#include "etude/run_replay.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <vector>

using namespace replay_test;

namespace {

ReplaySong song_of(const Fixture& f) { return {f.notes, f.measures}; }

ReplayTimeline timeline_of(const Fixture& f, const RunEvents& run, GhostSetup setup = {}) {
    return build_replay_timeline(run, setup, song_of(f));
}

} // namespace

TEST_CASE("replay: a press is laid over its note with time, velocity, hand, grade and offset") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({press(1, 950, -50, NoteGrade::Early, 74, Hand::Right, 91)});
    const ReplayTimeline t = timeline_of(f, run);
    REQUIRE(t.notes.size() == 1);
    const ReplayNote& n = t.notes[0];
    CHECK(n.time_ms == 950);
    CHECK(n.velocity == 91);
    CHECK(n.hand == Hand::Right);
    CHECK(n.mark == ReplayMark::Early);
    CHECK(n.key == 74);
    CHECK(n.note_index == 1);
    CHECK(n.expected_start_ms == 1000);
    CHECK(n.expected_end_ms == 1500);
    CHECK(n.offset_ms == -50);
    CHECK(n.error_ms == -50);
    CHECK(n.measure == 1);
    CHECK(n.has_note());
    CHECK(replay_length_ms(n) == 500);
}

TEST_CASE("replay: every mark has a grade colour and the word goes with it") {
    CHECK(replay_grade(ReplayMark::Perfect) == Grade::Perfect);
    CHECK(replay_grade(ReplayMark::Good) == Grade::Good);
    CHECK(replay_grade(ReplayMark::Early) == Grade::Early);
    CHECK(replay_grade(ReplayMark::Late) == Grade::Late);
    CHECK(replay_grade(ReplayMark::Missed) == Grade::Miss);
    CHECK(replay_grade(ReplayMark::Wrong) == Grade::Wrong);
    CHECK(replay_grade(ReplayMark::Played) == Grade::Perfect); // Wait Mode's OK is Perfect's green
}

TEST_CASE("replay: the grades of the events become the marks, early and late among them") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({
        press(0, 0, 0, NoteGrade::Perfect, 72, Hand::Right),
        press(1, 1000, 0, NoteGrade::Good, 74, Hand::Right),
        press(2, 40, 40, NoteGrade::Late, 48, Hand::Left),
        press(3, 1990, -10, NoteGrade::Early, 72, Hand::Right),
        press(4, 3000, 0, NoteGrade::Played, 74, Hand::Right),
        miss(5, 2000, 48, Hand::Left),
        wrong(2500, 61),
    });
    const ReplayTimeline t = timeline_of(f, run);
    REQUIRE(t.notes.size() == 7);
    std::vector<ReplayMark> marks;
    for (const ReplayNote& n : t.notes) {
        marks.push_back(n.mark);
    }
    CHECK(marks == std::vector<ReplayMark>{
                       ReplayMark::Perfect, ReplayMark::Late, ReplayMark::Good, ReplayMark::Early,
                       ReplayMark::Missed, ReplayMark::Wrong, ReplayMark::Played});
}

TEST_CASE("replay: events in any order come out in song-time order, ties by note and key") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 4), perfect(f, 0), perfect(f, 2), perfect(f, 1)});
    const ReplayTimeline t = timeline_of(f, run);
    REQUIRE(t.notes.size() == 4);
    CHECK(t.notes[0].note_index == 0); // 0 and 2 at 0 ms: the lower note first
    CHECK(t.notes[1].note_index == 2);
    CHECK(t.notes[2].note_index == 1);
    CHECK(t.notes[3].note_index == 4);
}

TEST_CASE("replay: a wrong press has no note, no hand, a default length and the bar at its time") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, run_of({wrong(4300, 61, 55)}));
    REQUIRE(t.notes.size() == 1);
    const ReplayNote& n = t.notes[0];
    CHECK(n.mark == ReplayMark::Wrong);
    CHECK(n.note_index == -1);
    CHECK(n.hand == Hand::Unknown);
    CHECK_FALSE(n.has_note());
    CHECK(n.velocity == 55);
    CHECK(n.measure == 3); // 4.3 s is in the third bar
    CHECK(replay_length_ms(n) == kReplayWrongLengthMs);
    CHECK(t.unmatched == 0); // a wrong press names no note, so none is missing
}

TEST_CASE("replay: a missed note sits at the note's own start with no velocity") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, run_of({miss(5, 2000, 48, Hand::Left)}));
    REQUIRE(t.notes.size() == 1);
    CHECK(t.notes[0].mark == ReplayMark::Missed);
    CHECK(t.notes[0].velocity == 0);
    CHECK(t.notes[0].time_ms == 2000);
    CHECK(t.notes[0].offset_ms == 0);
    CHECK(t.notes[0].expected_end_ms == 3000);
    CHECK(replay_length_ms(t.notes[0]) == 1000);
}

TEST_CASE("replay: a run at another speed lines up on the song clock; the error stays wall time") {
    const Fixture f = make_fixture();
    // At 50% a press 40 ms early in wall time is 20 ms early on the song clock.
    const RunEvents run = run_of({press(1, 980, -40, NoteGrade::Good, 74, Hand::Right)});
    const ReplayTimeline t = timeline_of(f, run, {RunMode::Flow, HandFilter::Both, 50, 0});
    REQUIRE(t.notes.size() == 1);
    CHECK(t.notes[0].offset_ms == -20);
    CHECK(t.notes[0].error_ms == -40);
    CHECK(t.setup.speed_percent == 50);
    ReplayPlayer player;
    player.open(t);
    CHECK(player.speed_percent() == 50); // opens at the run's own speed
}

TEST_CASE("replay: a transposed run keeps the keys it was played on, not the file's pitches") {
    const Fixture f = make_fixture();
    // Transposed up two semitones: the right hand's 72 is played on 74.
    const RunEvents run = run_of({press(0, 0, 0, NoteGrade::Perfect, 74, Hand::Right),
                                  wrong(300, 75)});
    const ReplayTimeline t = timeline_of(f, run, {RunMode::Flow, HandFilter::Both, 100, 2});
    REQUIRE(t.notes.size() == 2);
    CHECK(t.notes[0].key == 74);
    CHECK(f.notes[0].pitch == 72);
    CHECK(t.notes[1].key == 75);
    CHECK(t.setup.transpose_semitones == 2);
}

TEST_CASE("replay: a hand-filtered run holds only the practised hand's notes") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 2), perfect(f, 5), miss(8, 4000, 48, Hand::Left)}, 4);
    const ReplayTimeline t = timeline_of(f, run, {RunMode::Flow, HandFilter::Left, 100, 0});
    CHECK(t.setup.hands == HandFilter::Left);
    REQUIRE(t.notes.size() == 3);
    for (const ReplayNote& n : t.notes) {
        CHECK(n.hand == Hand::Left);
    }
    CHECK(t.expected_notes == 4);
    CHECK(t.graded_notes == 3);
}

TEST_CASE("replay: a loop judges a note more than once; the passes are merged on the song clock") {
    const Fixture f = make_fixture();
    const RunEvents run = run_of({perfect(f, 0), perfect(f, 0, 12), miss(0, 0, 72, Hand::Right)}, 3);
    const ReplayTimeline t = timeline_of(f, run);
    CHECK(t.looped);
    CHECK_FALSE(t.complete);
    CHECK_FALSE(t.stopped_early); // a loop is not a stopped run
    CHECK(t.notes.size() == 3);
    CHECK(t.graded_notes == 1);
}

namespace {

// `laps` laps over notes 0..2 (bar 1), the last lap reaching only `last_lap` notes.
RunEvents loop_laps(const Fixture& f, int laps, int last_lap) {
    std::vector<RunEvent> events;
    for (int lap = 0; lap < laps; ++lap) {
        for (int i = 0; i < (lap == laps - 1 ? last_lap : 3); ++i) {
            events.push_back(perfect(f, i, lap));
        }
    }
    return run_of(events, 3);
}

} // namespace

TEST_CASE("replay: a loop of three whole laps is looped and not stopped") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, loop_laps(f, 3, 3));
    CHECK(t.looped);
    CHECK_FALSE(t.stopped_early);
    CHECK(t.notes.size() == 9);
    CHECK(t.graded_notes == 3);
}

TEST_CASE("replay: a loop stopped mid-lap is flagged, at any point inside the last lap") {
    const Fixture f = make_fixture();
    for (const int reached : {1, 2}) {
        CAPTURE(reached);
        const ReplayTimeline t = timeline_of(f, loop_laps(f, 3, reached));
        CHECK(t.looped);
        CHECK(t.stopped_early);
    }
    // A stop exactly at a lap's end leaves nothing to tell it from a finished loop.
    CHECK_FALSE(timeline_of(f, loop_laps(f, 3, 0)).stopped_early);
    // One note into a second lap is a loop cut short too.
    CHECK(timeline_of(f, loop_laps(f, 2, 1)).stopped_early);
    // A complete run is never flagged, whatever its counts.
    RunEvents whole = loop_laps(f, 3, 1);
    whole.complete = true;
    CHECK_FALSE(timeline_of(f, whole).stopped_early);
}

TEST_CASE("replay: a run stopped half way ends where it stopped") {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 6; ++i) { // bars 1 and 2 of the four
        events.push_back(perfect(f, i));
    }
    const ReplayTimeline stopped = timeline_of(f, run_of(events, 12));
    CHECK(stopped.stopped_early);
    CHECK_FALSE(stopped.complete);
    CHECK(stopped.graded_notes == 6);
    CHECK(stopped.reached_ms == 3000);        // the last press: note 4 (bar 2, key 74) at 3000 ms
    CHECK(stopped.duration_ms == 3500);       // its block ends half a second later
    const ReplayTimeline whole = timeline_of(f, run_of(events, 6, true));
    CHECK_FALSE(whole.stopped_early);
    CHECK(whole.complete);
}

TEST_CASE("replay: an older run with fewer fields still plays: no expected count, a note beyond the "
          "song") {
    const Fixture f = make_fixture();
    // Nothing stored of how many notes it was to grade: it cannot be called stopped.
    const ReplayTimeline none = timeline_of(f, run_of({perfect(f, 0)}, 0));
    CHECK_FALSE(none.stopped_early);
    CHECK(none.expected_notes == 0);
    // A press that names a note this parse of the song does not have: kept, without a note, in the
    // bar of its time.
    const RunEvents run = run_of({press(99, 5100, 10, NoteGrade::Good, 60, Hand::Left)});
    const ReplayTimeline t = timeline_of(f, run);
    REQUIRE(t.notes.size() == 1);
    CHECK(t.unmatched == 1);
    CHECK_FALSE(t.notes[0].has_note());
    CHECK(t.notes[0].offset_ms == 0);
    CHECK(t.notes[0].measure == 3);
    CHECK(t.notes[0].hand == Hand::Left); // the event's own
    // A run with no events at all (pruned, or from before they were stored) is an empty replay.
    const ReplayTimeline empty = timeline_of(f, RunEvents{});
    CHECK(empty.empty());
    CHECK(empty.duration_ms == 0);
}

TEST_CASE("replay: events out of range are forced into it, never read past the song") {
    const Fixture f = make_fixture();
    RunEvent bad = press(-5, -100, 0, static_cast<NoteGrade>(99), 200, Hand::Right, 0);
    const ReplayTimeline t = timeline_of(f, run_of({bad}));
    REQUIRE(t.notes.size() == 1);
    CHECK(t.notes[0].time_ms == 0);
    CHECK(t.notes[0].key <= 127);
    CHECK(t.notes[0].velocity >= 1);
}

TEST_CASE("replay: measure_number_at finds the bar of a time, before, on and after its edges") {
    const Fixture f = make_fixture();
    CHECK(measure_number_at(f.measures, -1.0) == 1);
    CHECK(measure_number_at(f.measures, 0.0) == 1);
    CHECK(measure_number_at(f.measures, 1.999) == 1);
    CHECK(measure_number_at(f.measures, 2.0) == 2); // exactly on a bar's start belongs to it
    CHECK(measure_number_at(f.measures, 7.5) == 4);
    CHECK(measure_number_at(f.measures, 99.0) == 4);
    CHECK(measure_number_at({}, 3.0) == 1);
    std::vector<Measure> pickup = f.measures; // a pickup bar is numbered 0
    pickup.front().number = 0;
    CHECK(measure_number_at(pickup, 0.5) == 0);
}

TEST_CASE("replay: a window holds the notes from its start up to, not including, its end") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, run_of({perfect(f, 0), perfect(f, 1), perfect(f, 3)}));
    CHECK(replay_window(t, 0, 1000).size() == 1);    // 1000 is out
    CHECK(replay_window(t, 0, 1001).size() == 2);
    CHECK(replay_window(t, 1000, 2000).size() == 1); // 1000 is in
    CHECK(replay_window(t, 1001, 1999).empty());
    CHECK(replay_window(t, 5, 5).empty());
    CHECK(replay_window(t, 900, 100).empty());
    CHECK(replay_window(ReplayTimeline{}, 0, 1000).empty());
}

namespace {

// Ten presses a second apart, from 1000 ms.
ReplayTimeline ten_seconds() {
    const Fixture f = make_fixture();
    std::vector<RunEvent> events;
    for (int i = 0; i < 10; ++i) {
        events.push_back(press(0, 1000 * (i + 1), 0, NoteGrade::Perfect, 72, Hand::Right));
    }
    return timeline_of(f, run_of(events));
}

int total_passed(const ReplayTimeline& t, int speed, double dt_s) {
    ReplayPlayer player;
    player.open(t);
    player.set_speed_percent(speed);
    player.play(t);
    int passed = 0;
    for (int guard = 0; guard < 100000 && player.playing(); ++guard) {
        passed += static_cast<int>(player.advance(t, dt_s).size());
    }
    return passed;
}

} // namespace

TEST_CASE("replay player: every note is handed over once, whatever the step and the speed") {
    const ReplayTimeline t = ten_seconds();
    REQUIRE(t.notes.size() == 10);
    for (const int speed : {25, 50, 100, 150, 200}) {
        for (const double dt : {0.001, 0.0167, 0.1, 0.77, 5.0}) {
            CAPTURE(speed);
            CAPTURE(dt);
            CHECK(total_passed(t, speed, dt) == 10);
        }
    }
}

TEST_CASE("replay player: a step passes the notes between the old and the new position") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.play(t);
    CHECK(p.advance(t, 0.5).empty());     // 0 .. 500
    CHECK(p.advance(t, 0.5).empty());     // 500 .. 1000: 1000 is not yet passed
    CHECK(p.advance(t, 0.001).size() == 1); // 1000 .. 1001
    CHECK(p.advance(t, 1.0).size() == 1); // 1001 .. 2001 holds 2000
    CHECK(p.position_ms() == 2001);
}

TEST_CASE("replay player: speed scales the song time a step covers") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.set_speed_percent(200);
    p.play(t);
    p.advance(t, 1.0);
    CHECK(p.position_ms() == 2000);
    p.set_speed_percent(25);
    p.advance(t, 1.0);
    CHECK(p.position_ms() == 2250);
    p.set_speed_percent(5); // clamped to the song's own range
    CHECK(p.speed_percent() == kMinReplaySpeedPercent);
    p.set_speed_percent(900);
    CHECK(p.speed_percent() == kMaxReplaySpeedPercent);
}

TEST_CASE("replay player: it opens paused at the start at the run's speed, clamped") {
    ReplayTimeline t = ten_seconds();
    t.setup.speed_percent = 10;
    ReplayPlayer p;
    p.open(t);
    CHECK_FALSE(p.playing());
    CHECK(p.position_ms() == 0);
    CHECK(p.speed_percent() == kMinReplaySpeedPercent);
    CHECK(p.advance(t, 1.0).empty()); // paused: nothing moves
    CHECK(p.position_ms() == 0);
}

TEST_CASE("replay player: a seek moves the position and the notes it jumps over are not handed "
          "over") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.take_seeked();
    p.play(t);
    p.seek_ms(t, 5500);
    CHECK(p.take_seeked());
    CHECK_FALSE(p.take_seeked()); // told once
    CHECK(p.position_ms() == 5500);
    CHECK(p.advance(t, 1.0).size() == 1); // 6000 only; 1000..5000 were jumped
    p.seek_ms(t, 1500); // back: the notes after it come again
    CHECK(p.advance(t, 1.0).size() == 1); // 2000
}

TEST_CASE("replay player: seeks are clamped to the replay and a fraction maps onto it") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.seek_ms(t, -50);
    CHECK(p.position_ms() == 0);
    p.seek_ms(t, 99'999'999);
    CHECK(p.position_ms() == t.duration_ms);
    p.seek_fraction(t, 0.5);
    CHECK(p.position_ms() == t.duration_ms / 2);
    CHECK(p.fraction(t) == doctest::Approx(0.5).epsilon(0.001));
    p.seek_fraction(t, 7.0);
    CHECK(p.fraction(t) == 1.0);
    p.seek_fraction(t, std::nan(""));
    CHECK(p.position_ms() == 0);
    CHECK(ReplayPlayer{}.fraction(ReplayTimeline{}) == 0.0);
}

TEST_CASE("replay player: the end stops it, and play from the end starts over") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.play(t);
    p.advance(t, 1000.0);
    CHECK_FALSE(p.playing());
    CHECK(p.finished());
    CHECK(p.position_ms() == t.duration_ms);
    CHECK(p.advance(t, 1.0).empty());
    p.take_seeked();
    p.play(t);
    CHECK(p.playing());
    CHECK_FALSE(p.finished());
    CHECK(p.position_ms() == 0);
    CHECK(p.take_seeked()); // starting over is a jump: the sounds of the end are cancelled
}

TEST_CASE("replay player: a bad step moves nothing, and an empty replay cannot play") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.play(t);
    CHECK(p.advance(t, -1.0).empty());
    CHECK(p.advance(t, std::nan("")).empty());
    CHECK(p.advance(t, 0.0).empty());
    CHECK(p.position_ms() == 0);
    ReplayPlayer q;
    q.open(ReplayTimeline{});
    q.play(ReplayTimeline{});
    CHECK_FALSE(q.playing());
}

TEST_CASE("replay player: stepping a replay allocates nothing") {
    const ReplayTimeline t = ten_seconds();
    ReplayPlayer p;
    p.open(t);
    p.play(t);
    etude::test::start_counting_allocations();
    for (int i = 0; i < 200; ++i) {
        p.advance(t, 0.0167);
    }
    p.seek_ms(t, 3000);
    p.advance(t, 0.5);
    CHECK(etude::test::stop_counting_allocations() == 0);
}

TEST_CASE("replay: a jump to the next mistake lands a lead before it, and repeated jumps walk on") {
    const Fixture f = make_fixture();
    // Mistakes at 3000 (a miss) and 7000 (a wrong key); good presses around them.
    const ReplayTimeline t = timeline_of(
        f, run_of({perfect(f, 0), perfect(f, 1), miss(4, 3000, 74, Hand::Right), perfect(f, 6),
                   wrong(7000, 61), perfect(f, 9)}));
    CHECK(mistake_position(t, 0, 1) == 3000 - kMistakeLeadMs);
    CHECK(mistake_position(t, 3000 - kMistakeLeadMs, 1) == 7000 - kMistakeLeadMs); // on to the next
    CHECK(mistake_position(t, 3000 - kMistakeLeadMs - 1, 1) == 3000 - kMistakeLeadMs); // not yet there
    CHECK_FALSE(mistake_position(t, 7000 - kMistakeLeadMs, 1).has_value()); // none after the last
}

TEST_CASE("replay: a jump back finds the mistake before, never the one it stands at") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({miss(4, 3000, 74, Hand::Right), wrong(7000, 61)}));
    CHECK(mistake_position(t, 9000, -1) == 7000 - kMistakeLeadMs);
    CHECK(mistake_position(t, 7000 - kMistakeLeadMs, -1) == 3000 - kMistakeLeadMs);
    CHECK_FALSE(mistake_position(t, 3000 - kMistakeLeadMs, -1).has_value());
}

TEST_CASE("replay: a mistake in the first lead is reached itself, and going back lands at the start") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(f, run_of({miss(0, 500, 72, Hand::Right)}));
    CHECK(mistake_position(t, 0, 1) == 500); // its lead would be before the start: the mistake itself
    CHECK_FALSE(mistake_position(t, 500, 1).has_value());
    CHECK(mistake_position(t, 600, -1) == 0);
    CHECK(mistake_position(t, 500, -1) == 0);
    CHECK_FALSE(mistake_position(t, 0, -1).has_value());
    CHECK(mistake_position(ReplayTimeline{}, 0, 1) == std::nullopt);
}

TEST_CASE("replay: a playhead inside a mistake's lead reaches the mistake, the next jump goes on") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({miss(4, 3000, 74, Hand::Right), wrong(3800, 61), wrong(9000, 62)}));
    CHECK(t.mistakes == std::vector<std::int32_t>{3000, 3800, 9000});
    CHECK(mistake_position(t, 0, 1) == 1000);    // the lead of 3000
    CHECK(mistake_position(t, 1500, 1) == 3000); // inside its lead: the mistake itself
    CHECK(mistake_position(t, 2999, 1) == 3000);
    CHECK(mistake_position(t, 3000, 1) == 3800); // its lead point is behind: itself
    CHECK(mistake_position(t, 3800, 1) == 9000 - kMistakeLeadMs);
    // Standing exactly on a lead point has reached that mistake: on to the one after.
    CHECK(mistake_position(t, 1000, 1) == 1800);
    CHECK_FALSE(mistake_position(t, 9000 - kMistakeLeadMs, 1).has_value());
    CHECK_FALSE(mistake_position(t, 9000, 1).has_value());
    // Going back: the lead point behind the playhead, the start for an early mistake.
    CHECK(mistake_position(t, 3800, -1) == 1800);
}

TEST_CASE("replay: the mistakes are the times of the misses and wrong keys, ascending") {
    const Fixture f = make_fixture();
    const ReplayTimeline t = timeline_of(
        f, run_of({perfect(f, 0), wrong(900, 61), miss(4, 3000, 74, Hand::Right), perfect(f, 6)}));
    REQUIRE(t.mistakes.size() == 2);
    CHECK(std::is_sorted(t.mistakes.begin(), t.mistakes.end()));
    CHECK(t.mistakes[0] == 900);
    CHECK(t.mistakes[1] == 3000);
    CHECK(timeline_of(f, run_of({perfect(f, 0)})).mistakes.empty());
}

TEST_CASE("replay player: a tap on the speed goes up through the presets and the run's own speed") {
    CHECK(next_replay_speed(100, 100) == 150);
    CHECK(next_replay_speed(150, 100) == 200);
    CHECK(next_replay_speed(200, 100) == 25); // from the top back to the lowest
    CHECK(next_replay_speed(25, 100) == 50);
    // The run's own speed is one of the stops: 80%, between 75 and 100.
    CHECK(next_replay_speed(75, 80) == 80);
    CHECK(next_replay_speed(80, 80) == 100);
    // A speed that is not a stop (typed elsewhere) moves to the next stop above it.
    CHECK(next_replay_speed(60, 100) == 75);
    // An own speed out of the replay's range is held in it.
    CHECK(next_replay_speed(25, 10) == 50);
    CHECK(next_replay_speed(200, 400) == 25);
}
