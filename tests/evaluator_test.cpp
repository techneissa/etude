// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: the first topics (split from evaluator_test.cpp).
#include "etude/evaluator.hpp"
#include "alloc_count.hpp"
#include "etude/keyboard.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <random>
#include <span>
#include <thread>
#include <vector>

#include "evaluator_test_support.hpp"

using namespace etude;
using OpKind = TransportOp::Kind;
using namespace evaluator_test_support;

namespace {

const NoteResult* result_for(const EvaluationSummary& s, std::uint32_t note, int pass = -1) {
    for (const NoteResult& r : s.notes) {
        if (r.note_index == note && (pass < 0 || r.pass == pass)) {
            return &r;
        }
    }
    return nullptr;
}

} // namespace

// ---------------------------------------------------------------------------------------------
// TransportTimeline

TEST_CASE("timeline: a press is placed in the song through the moves, at any speed") {
    TransportTimeline tl;
    TransportOp move;
    move.kind = OpKind::Move;
    move.at_ns = sec(10);
    move.end_ns = sec(12);
    move.from_s = 0.0;
    move.to_s = 1.0;
    move.speed_percent = 50;
    tl.apply(move);
    TransportOp hold;
    hold.kind = OpKind::Hold;
    hold.at_ns = sec(12);
    hold.to_s = 1.0;
    tl.apply(hold);
    CHECK(tl.song_at(sec(11)) == doctest::Approx(0.5));
    CHECK(tl.song_at(sec(15)) == doctest::Approx(1.0)); // held
    CHECK(tl.song_at(sec(5)) == doctest::Approx(0.0));  // before the first op
    CHECK(tl.rate_at(sec(11)) == doctest::Approx(0.5));
}

// ---------------------------------------------------------------------------------------------
// Flow Mode: grades

TEST_CASE("flow: a press on time is Perfect and the combo counts it") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 1);
    CHECK(s.notes[0].grade == NoteGrade::Perfect);
    CHECK(s.notes[0].arrival_ns == sec(2.0));
    CHECK(s.counts.perfect == 1);
    CHECK(s.combo == 1);
    CHECK(s.longest_combo == 1);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: grades follow the window: Perfect, Good, Early, Late and Missed") {
    // Window 80 ms: Perfect within 28 ms, Good within 56 ms, Early/Late within 80 ms.
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}, {3.0, 64}, {4.0, 65}, {5.0, 67}, {6.0, 69}}),
                FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0 + 0.020, 60)); // Perfect
    rig.send(note_on(3.0 - 0.050, 62)); // Good (early side)
    rig.send(note_on(4.0 - 0.070, 64)); // Early
    rig.send(note_on(5.0 + 0.070, 65)); // Late
    rig.send(note_on(6.0 + 0.200, 67)); // too late: Missed, and a wrong press
    rig.run_until(9.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 6);
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 1)->grade == NoteGrade::Good);
    CHECK(result_for(s, 2)->grade == NoteGrade::Early);
    CHECK(result_for(s, 3)->grade == NoteGrade::Late);
    CHECK(result_for(s, 3)->error_ns == ms(70));
    CHECK(result_for(s, 4)->grade == NoteGrade::Missed);
    CHECK(result_for(s, 5)->grade == NoteGrade::Missed);
    CHECK(s.wrong == 1);
    CHECK(s.counts.missed == 2);
    CHECK(s.longest_combo == 4);
    CHECK(s.combo == 0);
}

TEST_CASE("flow: the window edge is inside, one nanosecond past it is outside") {
    FlowOptions o;
    o.window_ms = 80.0;
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0 + 0.080, 60));
    MidiEvent past = note_on(3.0, 62);
    past.time_ns = sec(3.0) - ms(80) - 1;
    rig.send(past);
    MidiEvent late = note_on(2.0, 60);
    late.time_ns = sec(2.0) + ms(80) + 1; // one past the late edge of the first note: wrong
    rig.send(late);
    rig.run_until(5.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Late);
    CHECK(result_for(s, 1)->grade == NoteGrade::Missed);
    CHECK(s.wrong == 1); // the early 62 is pressed early for its note, never wrong (F5)
}

TEST_CASE("flow: the window is adjustable and the thresholds scale with it") {
    FlowOptions o;
    o.window_ms = 200.0; // Perfect within 70 ms, Good within 140 ms
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0 + 0.060, 60));
    rig.send(note_on(3.0 - 0.150, 62));
    rig.run_until(5.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 1)->grade == NoteGrade::Early);
}

TEST_CASE("flow: latency correction is subtracted from each press before grading") {
    FlowOptions o;
    o.latency_ms = 60.0;
    FlowRig rig(song_of({{1.0, 60}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0 + 0.060, 60)); // the player's lag: really on time
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->error_ns == 0);
}

// ---------------------------------------------------------------------------------------------
// Flow Mode: matching

TEST_CASE("flow: the assignment maximises matches where nearest-first would lose one") {
    // Same key at song 1.0 and 1.1. Presses 70 ms and 170 ms after the first: nearest-first
    // gives the first press to the second note and leaves the first note missed.
    FlowRig rig(song_of({{1.0, 60}, {1.1, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.070, 60));
    rig.send(note_on(2.170, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Late);
    CHECK(result_for(s, 1)->grade == NoteGrade::Late);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: an extra press near a note never steals it, the closer press grades it") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0 - 0.070, 60)); // extra, early
    rig.send(note_on(2.0 + 0.005, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->error_ns == ms(5));
    CHECK(s.wrong == 0); // the extra press is the right key, early: not wrong (F5)
    CHECK(s.longest_combo == 1);
}

TEST_CASE("flow: an extra press after a note is wrong and breaks the combo") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0 + 0.005, 60));
    rig.send(note_on(2.0 + 0.070, 60)); // extra, late
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(s.wrong == 1); // after the note made the combo 1
    CHECK(s.longest_combo == 1);
    CHECK(s.combo == 0);
}

TEST_CASE("flow: a Miss and a wrong press each record the combo they lost") {
    // Hits at 1, 2 and 3 s (combo 3), a Miss at 4 s (loses 3), a hit at 5 s (combo 1), then a
    // stray press (loses 1). A hit decided after the Miss must not hide the lost combo.
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}, {3.0, 64}, {4.0, 65}, {5.0, 67}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.send(note_on(3.0, 62));
    rig.send(note_on(4.0, 64));
    rig.send(note_on(6.0, 67));
    rig.send(note_on(6.3, 72)); // matches nothing
    rig.run_until(9.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.combo_losses.size() == 2);
    CHECK(s.combo_losses[0] == 3);
    CHECK(s.combo_losses[1] == 1);
    CHECK(s.combo == 0);
}

TEST_CASE("flow: a wrong key is counted apart and resets the combo") {
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}, {3.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.send(note_on(2.5, 61)); // matches nothing
    rig.send(note_on(3.0, 62));
    rig.send(note_on(4.0, 64));
    rig.run_until(6.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(hits(s) == 3);
    CHECK(s.wrong == 1);
    CHECK(s.longest_combo == 2);
    CHECK(s.combo == 2);
}

TEST_CASE("flow: a chord rolled inside the window is all hits") {
    FlowRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.0, 67}, {1.0, 72}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0 - 0.060, 72)); // rolled top down
    rig.send(note_on(2.0 - 0.020, 67));
    rig.send(note_on(2.0 + 0.025, 64));
    rig.send(note_on(2.0 + 0.075, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(hits(s) == 4);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
    CHECK(result_for(s, 0)->grade == NoteGrade::Late);  // 60, +75 ms
    CHECK(result_for(s, 3)->grade == NoteGrade::Early); // 72, -60 ms
}

TEST_CASE("flow: a fast trill of repeated keys at 150% speed matches every note") {
    // 60 and 62 alternating every 100 ms of song: at 150% each key repeats every 133 ms of wall
    // time, under two windows apart, so every press could reach two notes of its key.
    FlowRig rig(song_of({{1.0, 60},
                         {1.1, 62},
                         {1.2, 60},
                         {1.3, 62},
                         {1.4, 60},
                         {1.5, 62},
                         {1.6, 60},
                         {1.7, 62}}),
                FlowOptions{});
    rig.play_at(1.0, 150);
    const double jitter[] = {0.030, -0.035, 0.040, -0.030, 0.035, -0.040, 0.030, -0.025};
    const double starts[] = {1.0, 1.1, 1.2, 1.3, 1.4, 1.5, 1.6, 1.7};
    for (int i = 0; i < 8; ++i) {
        rig.send(note_on(1.0 + starts[i] / 1.5 + jitter[i], i % 2 == 0 ? 60 : 62));
    }
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(hits(s) == 8);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
    for (std::uint32_t i = 0; i < 8; ++i) {
        CHECK(std::llabs(result_for(s, i)->error_ns - ms(jitter[i] * 1000.0)) <= 2);
    }
}

TEST_CASE("flow: both hands on one key at once take one press and both are hits") {
    FlowRig rig(song_of({{1.0, 60, Hand::Left}, {1.0, 60, Hand::Right}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.010, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 2);
    CHECK(s.notes[0].grade == NoteGrade::Perfect);
    CHECK(s.notes[1].grade == NoteGrade::Perfect);
    CHECK(s.left.hit == 1);
    CHECK(s.right.hit == 1);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: only the practised hand is graded; the piano's notes never are") {
    FlowOptions o;
    o.hands = HandFilter::Right;
    FlowRig rig(song_of({{1.0, 60, Hand::Right}, {1.0, 48, Hand::Left}, {2.0, 50, Hand::Unknown}}),
                o);
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(5.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 1);
    CHECK(s.notes[0].note_index == 1); // the right-hand C4 (sorted after the left-hand C3)
    CHECK(s.counts.missed == 0);
    CHECK(s.left.graded == 0);
    CHECK(s.right.accuracy().value() == 1.0);
}

TEST_CASE("flow: drum notes are never graded") {
    Song song = song_of({{1.0, 60}, {1.5, 38}});
    song.notes[1].drum = true;
    FlowRig rig(song, FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(4.0);
    CHECK(rig.eval.summary().notes.size() == 1);
    CHECK(rig.eval.summary().counts.missed == 0);
}

TEST_CASE("flow: the expected key comes from the transposition") {
    FlowOptions o;
    o.transposition = Transposition(2, kRange61);
    FlowRig rig(song_of({{1.0, 60}, {2.0, 95}}), o); // 95 + 2 = 97 folds down to 85
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60)); // the untransposed key is wrong
    rig.send(note_on(2.0, 62));
    rig.send(note_on(3.0, 85));
    rig.run_until(5.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 0)->expected_pitch == 62);
    CHECK(result_for(s, 1)->grade == NoteGrade::Perfect);
    CHECK(result_for(s, 1)->expected_pitch == 85);
    CHECK(s.wrong == 1);
}

TEST_CASE("flow: an unplayable note is neither hit nor missed, and is counted") {
    FlowOptions o;
    o.transposition = Transposition(0, KeyRange{60, 64}); // narrower than an octave
    FlowRig rig(song_of({{1.0, 60}, {2.0, 66}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(5.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.notes.size() == 1);
    CHECK(s.counts.missed == 0);
    CHECK(s.unplayable == 1);
}

TEST_CASE("flow: a speed change mid-song keeps arrival times exact") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(3.0); // song 2.0
    rig.transport.set_speed_percent(sec(3.0), 50);
    // Song 3.0 is one song second later at half speed: wall 5.0.
    rig.send(note_on(5.0 + 0.010, 62));
    rig.run_until(7.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 1)->arrival_ns == sec(5.0));
    CHECK(result_for(s, 1)->grade == NoteGrade::Perfect);
    CHECK(s.counts.missed == 0);
}

TEST_CASE("flow: events stamped out of order across ports by a few ms grade the same") {
    FlowRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.05, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.run_until(2.06);
    // Port B's 64 (stamped 2.004) and second 60 (stamped 2.052) are delivered after port A's
    // later stamps.
    rig.eval.apply(note_on(2.001, 60));
    rig.eval.apply(note_on(2.055, 60));
    rig.eval.apply(note_on(2.004, 64));
    rig.eval.apply(note_on(2.052, 60));
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 1);
    CHECK(result_for(s, 0)->error_ns == ms(1));
    CHECK(result_for(s, 2)->error_ns == ms(2)); // the 2.052 press, not the later 2.055
}

TEST_CASE("flow: releases, the pedal, velocity-0 note-ons and synthetic events are not presses") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60, 0));
    rig.send(note_off(2.01, 60));
    rig.send(cc(2.02, kCcSustain, 127));
    MidiEvent synth = cc(2.03, kCcAllNotesOff, 0);
    synth.synthetic = true;
    rig.send(synth);
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.counts.missed == 1);
    CHECK(s.wrong == 0);
}

TEST_CASE("flow: presses while paused or before play are ignored") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 62}}), FlowOptions{});
    rig.send(note_on(0.5, 70)); // before play
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(2.5);
    rig.transport.pause(sec(2.5));
    rig.send(note_on(3.0, 71)); // noodling while paused
    rig.run_until(4.0);
    rig.play_at(4.0);
    rig.send(note_on(5.5, 62)); // song 3.0 at wall 4 + 1.5
    rig.run_until(7.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.wrong == 0);
    CHECK(hits(s) == 2);
}

TEST_CASE("flow: a seek drops undecided notes and presses, neither hit nor missed") {
    FlowRig rig(song_of({{1.0, 60}, {5.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    rig.run_until(2.02); // note 0 arrived at 2.0, its window still open
    rig.transport.seek(sec(2.02), 4.5);
    rig.send(note_on(2.03, 60)); // stamped after the seek: the old note is gone, so wrong
    rig.send(note_on(2.52, 62)); // song 5.0 arrives at 2.02 + 0.5
    rig.run_until(4.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(result_for(s, 0) == nullptr);
    CHECK(result_for(s, 1)->grade == NoteGrade::Perfect);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 1);
}

TEST_CASE("flow: per-hand accuracy, grade counts and the per-bar miss map") {
    // Bars are 2 s: notes at 1.0 and 1.5 are bar 1, 3.0 is bar 2, 5.0 is bar 3.
    FlowRig rig(song_of({{1.0, 60, Hand::Right},
                         {1.5, 48, Hand::Left},
                         {3.0, 62, Hand::Right},
                         {5.0, 64, Hand::Right}}),
                FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.send(note_on(2.5, 48));
    rig.send(note_on(4.5, 70)); // wrong, song 3.5: bar 2
    rig.send(note_on(6.0, 64));
    rig.run_until(8.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.right.graded == 3);
    CHECK(s.right.hit == 2);
    CHECK(s.right.accuracy().value() == doctest::Approx(2.0 / 3.0));
    CHECK(s.left.accuracy().value() == 1.0);
    CHECK(s.all.graded == 4);
    CHECK(s.counts.perfect == 3);
    CHECK(s.counts.missed == 1);
    REQUIRE(s.measures.size() == 3);
    CHECK(s.measures[0].measure == 1);
    CHECK(s.measures[0].graded == 2);
    CHECK(s.measures[0].missed == 0);
    CHECK(s.measures[1].measure == 2);
    CHECK(s.measures[1].missed == 1);
    CHECK(s.measures[1].wrong == 1);
    CHECK(s.measures[2].missed == 0);
}

TEST_CASE("accuracy is empty when nothing was graded, never 100%") {
    HandStats none;
    CHECK_FALSE(none.accuracy().has_value());
    HandStats some;
    some.graded = 4;
    some.hit = 3;
    REQUIRE(some.accuracy().has_value());
    CHECK(*some.accuracy() == doctest::Approx(0.75));
}

TEST_CASE("flow: loop wraps start a new pass; a clean pass is reported for auto speed-up") {
    FlowRig rig(song_of({{0.5, 60}, {1.5, 62}, {3.0, 64}}), FlowOptions{});
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.play_at(1.0);
    // Pass 0: wall 1..3, both notes. Pass 1: wall 3..5, misses the second note. Pass 2: 5..7.
    rig.send(note_on(1.5, 60));
    rig.send(note_on(2.5, 62));
    rig.send(note_on(3.5, 60));
    rig.send(note_on(5.5, 60));
    rig.send(note_on(6.5 + 0.075, 62)); // late, still a hit
    rig.run_until(7.3);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(passes.size() == 3);
    CHECK(passes[0].pass == 0);
    CHECK(passes[0].clean);
    CHECK(passes[0].graded == 2);
    CHECK(passes[1].pass == 1);
    CHECK_FALSE(passes[1].clean);
    CHECK(passes[1].missed == 1);
    CHECK(passes[2].clean);
    CHECK(passes[2].start_s == 0.0);
    CHECK(passes[2].end_s == 2.0);
    CHECK(result_for(rig.eval.summary(), 1, 2)->grade == NoteGrade::Late);
    passes.clear();
    rig.eval.take_passes(passes);
    CHECK(passes.empty()); // each pass is handed out once
}

TEST_CASE("flow: a pass that began inside the loop is never clean") {
    FlowRig rig(song_of({{0.5, 60}, {1.5, 62}}), FlowOptions{});
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.transport.seek(0, 1.0);
    rig.play_at(1.0);
    rig.send(note_on(1.5, 62));
    rig.run_until(2.5);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(passes.size() == 1);
    CHECK(passes[0].missed == 0);
    CHECK_FALSE(passes[0].clean);
}

TEST_CASE("flow: finish decides everything still open") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.run_until(2.01);
    CHECK(rig.eval.pending_notes() == 1);
    rig.eval.finish();
    CHECK(rig.eval.summary().counts.missed == 1);
    CHECK(rig.eval.pending_notes() == 0);
}
