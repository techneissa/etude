// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: wait (split from evaluator_test.cpp).
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

// ---------------------------------------------------------------------------------------------
// Wait Mode

TEST_CASE("wait: the song holds at a chord and goes on when all its keys are down at once") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.0, 67}, {2.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 1.0);
    CHECK(rig.eval.waiting_for_s() == 1.0);
    CHECK(keys_of(rig.eval.waiting_keys()) == std::vector<int>{60, 64, 67});
    rig.send(note_on(2.5, 60));
    rig.send(note_on(2.6, 64));
    rig.run_until(2.8);
    CHECK(rig.transport.held()); // two of three
    rig.send(note_on(2.8, 67));
    rig.run_until(2.9);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.transport.position_s() > 1.0);
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 3);
    CHECK(s.counts.played == 3);
    CHECK(s.notes[0].grade == NoteGrade::Played);
    CHECK(s.notes[0].arrival_ns == sec(2.0));
    CHECK(s.notes[0].error_ns == ms(800)); // how long the song waited
    CHECK(rig.eval.waiting_for_s() == 2.0);
}

TEST_CASE("wait: a chord note let go before the last one lands does not count") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.send(note_off(2.6, 60));
    rig.send(note_on(2.7, 64));
    rig.run_until(3.0);
    CHECK(rig.transport.held());
    rig.send(note_on(3.0, 60));
    rig.run_until(3.1);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.eval.summary().wrong == 0);
}

TEST_CASE("wait: a key already held does not count until it is pressed again") {
    WaitRig rig(song_of({{1.0, 60}, {2.0, 60}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60)); // plays the first chord and stays down
    rig.run_until(4.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 2.0);
    rig.send(note_off(4.5, 60));
    rig.send(note_on(4.6, 60));
    rig.run_until(4.7);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 2);
}

TEST_CASE("wait: a key pressed before play does not play the first chord") {
    WaitRig rig(song_of({{1.0, 60}, {3.0, 72}}), WaitOptions{});
    rig.send(note_on(0.5, 60));
    rig.play_at(1.0);
    rig.run_until(3.0);
    CHECK(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 0);
}

TEST_CASE("wait: a press slightly before the chord arrives counts and the song never stops") {
    WaitOptions o;
    o.early_ms = 150.0;
    WaitRig rig(song_of({{1.0, 60}, {3.0, 72}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0 - 0.100, 60));
    rig.run_until(2.7);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.transport.position_s() == doctest::Approx(1.7));
    const EvaluationSummary& s = rig.eval.summary();
    REQUIRE(s.notes.size() == 1);
    CHECK(s.notes[0].error_ns == 0);
}

TEST_CASE("wait: a press too early does not count, and is not wrong either") {
    WaitOptions o;
    o.early_ms = 150.0;
    WaitRig rig(song_of({{1.0, 60}, {3.0, 72}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.0 - 0.300, 60));
    rig.run_until(2.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 1.0);
    CHECK(rig.eval.summary().wrong == 0);
}

TEST_CASE("wait: wrong notes are counted in the waiting chord's bar and never block it") {
    WaitRig rig(song_of({{3.0, 60}, {5.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(4.5);
    rig.send(note_on(4.5, 61));
    rig.send(note_on(4.6, 60)); // 61 is still down
    rig.run_until(4.7);
    CHECK_FALSE(rig.transport.held());
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.wrong == 1);
    REQUIRE_FALSE(s.measures.empty());
    CHECK(s.measures[0].measure == 2);
    CHECK(s.measures[0].wrong == 1);
}

TEST_CASE("wait: the sustain pedal never holds a key") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(cc(2.5, kCcSustain, 127));
    rig.send(note_on(2.6, 60));
    rig.send(note_off(2.7, 60)); // the pedal keeps it sounding, not held
    rig.send(note_on(2.8, 64));
    rig.run_until(3.0);
    CHECK(rig.transport.held());
}

TEST_CASE("wait: a velocity-0 note-on is a release") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.6, 60));
    rig.send(note_on(2.7, 60, 0));
    rig.send(note_on(2.8, 64));
    rig.run_until(3.0);
    CHECK(rig.transport.held());
}

TEST_CASE("wait: All Notes Off releases every key") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.6, 60));
    MidiEvent gap = cc(2.7, kCcAllNotesOff, 0);
    gap.synthetic = true;
    rig.send(gap);
    rig.send(note_on(2.8, 64));
    rig.run_until(3.0);
    CHECK(rig.transport.held());
}

TEST_CASE("wait: out-of-order stamps across ports: a release stamped before a press counts") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.run_until(2.6);
    // Port B's press of 64 (stamped 2.605) is delivered before port A's release of 60 (stamped
    // 2.600): the two keys were never down together.
    rig.eval.apply(note_on(2.605, 64));
    rig.eval.apply(note_off(2.600, 60));
    rig.run_until(2.8);
    CHECK(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 0);
}

TEST_CASE("wait: both hands on one key are one press") {
    WaitRig rig(song_of({{1.0, 60, Hand::Left}, {1.0, 60, Hand::Right}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    CHECK(keys_of(rig.eval.waiting_keys()) == std::vector<int>{60});
    rig.send(note_on(2.5, 60));
    rig.run_until(2.6);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 2);
}

TEST_CASE("wait: only the practised hand is waited for") {
    WaitOptions o;
    o.hands = HandFilter::Left;
    WaitRig rig(song_of({{1.0, 72, Hand::Right}, {2.0, 48, Hand::Left}, {4.0, 50, Hand::Left}}), o);
    rig.play_at(1.0);
    rig.run_until(3.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 2.0);
}

TEST_CASE("wait: with hold_full_length, letting go early holds the song until pressed again") {
    WaitOptions o;
    o.hold_full_length = true;
    WaitRig rig(song_of({{1.0, 60, Hand::Right, 1.0}, {4.0, 72}}), o);
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.send(note_off(2.8, 60)); // song 1.3, the note ends at 2.0
    rig.run_until(3.0);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == doctest::Approx(1.3).epsilon(0.02));
    REQUIRE(rig.eval.summary().notes.size() == 1);
    CHECK(rig.eval.summary().notes[0].released_early);
    rig.send(note_on(3.0, 60));
    rig.run_until(3.2);
    CHECK_FALSE(rig.transport.held());
    rig.send(note_off(3.0 + 0.7 - 0.05, 60)); // within release_slack_ms of the end: fine
    rig.run_until(4.5);
    CHECK_FALSE(rig.transport.held());
}

TEST_CASE("wait: without hold_full_length a short press is enough") {
    WaitRig rig(song_of({{1.0, 60, Hand::Right, 1.0}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.send(note_off(2.6, 60));
    rig.run_until(4.0);
    CHECK_FALSE(rig.transport.held());
    CHECK_FALSE(rig.eval.summary().notes[0].released_early);
}

TEST_CASE("wait: a seek waits for the first chord after it and keys down must be pressed again") {
    WaitRig rig(song_of({{1.0, 60}, {2.0, 64}, {3.0, 60}, {5.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60)); // plays bar 1's chord, stays down
    rig.run_until(2.6);
    rig.transport.seek(sec(2.6), 2.5);
    rig.run_until(4.0);
    CHECK(rig.eval.waiting_for_s() == 3.0);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 3.0);
    rig.send(note_off(4.0, 60));
    rig.send(note_on(4.1, 60));
    rig.run_until(4.2);
    CHECK_FALSE(rig.transport.held());
}

TEST_CASE("wait: each loop pass waits again from the loop start and reports clean passes") {
    WaitRig rig(song_of({{0.0, 60}, {1.0, 62}, {3.0, 64}}), WaitOptions{});
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.play_at(1.0);
    rig.run_until(1.2);
    CHECK(rig.transport.held()); // the chord at the loop start holds at once
    rig.send(note_on(1.2, 60));
    rig.send(note_off(1.3, 60));
    rig.run_until(2.5);
    rig.send(note_on(2.5, 62));
    rig.send(note_off(2.6, 62));
    rig.run_until(3.7); // played at 2.515, so the wrap came at 3.515
    CHECK(rig.transport.loop_count() == 1);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == doctest::Approx(0.0).epsilon(0.02));
    rig.send(note_on(3.7, 61)); // wrong in pass 1
    rig.send(note_on(3.8, 60));
    rig.send(note_off(3.9, 60));
    rig.run_until(4.9);
    rig.send(note_on(5.0, 62));
    rig.send(note_off(5.1, 62));
    rig.run_until(6.3);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(passes.size() == 2);
    CHECK(passes[0].clean);
    CHECK(passes[0].graded == 2);
    CHECK_FALSE(passes[1].clean);
    CHECK(passes[1].wrong == 1);
}
