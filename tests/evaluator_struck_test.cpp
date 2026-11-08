// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: struck (split from evaluator_test.cpp).
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
// What the hit line shows: the note a press struck, and the wrong presses

TEST_CASE("flow: the note a press struck is known at the press, before its grade is final") {
    FlowRig rig(song_of({{1.0, 60}, {2.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    CHECK_FALSE(rig.eval.struck_start(60).has_value()); // never pressed
    rig.send(note_on(2.03, 60));
    rig.run_until(2.04);
    REQUIRE(rig.eval.struck_start(60).has_value());
    CHECK(*rig.eval.struck_start(60) == doctest::Approx(1.0));
    CHECK(rig.eval.summary().notes.empty()); // the grade comes a window later
    CHECK_FALSE(rig.eval.struck_start(64).has_value());
}

TEST_CASE("flow: a press within the window, both edges, struck the note; just outside did not") {
    FlowOptions o; // 80 ms
    {
        FlowRig rig(song_of({{1.0, 60}}), o);
        rig.play_at(1.0);
        rig.send(note_on(2.0 - 0.080, 60));
        rig.run_until(2.0);
        CHECK(rig.eval.struck_start(60).has_value());
    }
    {
        FlowRig rig(song_of({{1.0, 60}}), o);
        rig.play_at(1.0);
        rig.send(note_on(2.0 + 0.080, 60));
        rig.run_until(2.1);
        CHECK(rig.eval.struck_start(60).has_value());
    }
    {
        FlowRig rig(song_of({{1.0, 60}}), o);
        rig.play_at(1.0);
        rig.send(note_on(2.0 + 0.081, 60));
        rig.run_until(2.1);
        CHECK_FALSE(rig.eval.struck_start(60).has_value());
    }
    {
        FlowRig rig(song_of({{1.0, 60}}), o);
        rig.play_at(1.0);
        rig.send(note_on(2.0 - 0.081, 60));
        rig.run_until(2.0);
        CHECK_FALSE(rig.eval.struck_start(60).has_value());
    }
}

TEST_CASE("flow: a wrong key struck no note, and a repeated key strikes the nearest one") {
    FlowRig rig(song_of({{1.0, 60}, {1.06, 60}, {3.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.05, 61)); // a key with no note
    rig.send(note_on(2.05, 60)); // 10 ms from the second note, 50 ms from the first
    rig.run_until(2.1);
    CHECK_FALSE(rig.eval.struck_start(61).has_value());
    REQUIRE(rig.eval.struck_start(60).has_value());
    CHECK(*rig.eval.struck_start(60) == doctest::Approx(1.06));
}

TEST_CASE("flow: the next press on a key replaces what the last one struck") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(2.5);
    CHECK(*rig.eval.struck_start(60) == doctest::Approx(1.0));
    rig.send(note_on(2.5, 60)); // pressed again with no note there
    rig.run_until(2.6);
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
}

TEST_CASE("flow: a press before play struck nothing") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.send(note_on(0.5, 60));
    rig.run_until(0.6);
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
}

TEST_CASE("flow: a press is struck at the song's speed, the window being wall time") {
    FlowRig rig(song_of({{1.0, 60}}), FlowOptions{});
    rig.play_at(1.0, 50); // the note arrives at wall time 3.0
    rig.send(note_on(3.07, 60));
    rig.run_until(3.1);
    CHECK(rig.eval.struck_start(60).has_value());
}

TEST_CASE("flow: wrong presses are handed over once, with their key, when they are decided") {
    FlowRig rig(song_of({{1.0, 60}, {2.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.send(note_on(2.5, 61)); // matches nothing
    rig.send(note_on(3.0, 62));
    rig.run_until(4.0);
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong);
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].key == 61);
    CHECK(wrong[0].time_ns == sec(2.5));
    CHECK(rig.eval.summary().wrong == 1);
    wrong.clear();
    rig.eval.take_wrong_presses(wrong);
    CHECK(wrong.empty());
}

TEST_CASE("flow: a key of a note still to come is pressed early, not a wrong press") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.send(note_on(2.5, 64)); // 1.5 s early, inside the lookahead
    rig.run_until(4.0);
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong);
    CHECK(wrong.empty());
}

TEST_CASE("flow: the wrong presses kept are bounded") {
    FlowRig rig(song_of({{1.0, 60}, {60.0, 61}}), FlowOptions{});
    rig.play_at(1.0);
    for (int i = 0; i < 100; ++i) {
        rig.send(note_on(5.0 + 0.2 * i, 100)); // none matches, none is a note to come
    }
    rig.run_until(40.0);
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong);
    CHECK(wrong.size() == kMaxKeptWrongPresses);
    CHECK(rig.eval.summary().wrong == 100);
}

TEST_CASE("wait: a key of the waiting chord struck it at once, a key outside it did not") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {1.0, 67}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    CHECK(rig.transport.held());
    rig.send(note_on(2.5, 60));
    rig.send(note_on(2.55, 61)); // not in the chord
    rig.run_until(2.6);
    REQUIRE(rig.eval.struck_start(60).has_value());
    CHECK(*rig.eval.struck_start(60) == doctest::Approx(1.0));
    CHECK_FALSE(rig.eval.struck_start(61).has_value());
    CHECK_FALSE(rig.eval.struck_start(64).has_value()); // not pressed yet
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong); // known at the press, not a window later
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].key == 61);
}

TEST_CASE("wait: a key of a later chord, pressed early, struck nothing and is not wrong") {
    WaitRig rig(song_of({{1.0, 60}, {3.0, 64}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 64)); // the chord after the one waited for
    rig.run_until(2.6);
    CHECK_FALSE(rig.eval.struck_start(64).has_value());
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong);
    CHECK(wrong.empty());
}

TEST_CASE("flow: a press while paused struck nothing and does not look wrong") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.transport.pause(sec(2.5));
    rig.send(note_on(3.0, 71)); // noodling while paused
    rig.send(note_on(3.0, 62));
    rig.run_until(3.5);
    CHECK_FALSE(rig.eval.struck_start(71).has_value());
    CHECK_FALSE(rig.eval.struck_start(62).has_value());
    CHECK_FALSE(rig.eval.press_looks_wrong(71));
}

TEST_CASE("flow: a seek while the key is held forgets what it struck") {
    FlowRig rig(song_of({{1.0, 60}, {5.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(2.1);
    REQUIRE(rig.eval.struck_start(60).has_value());
    rig.transport.seek(sec(2.1), 4.5);
    rig.flush_ops();
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
}

TEST_CASE("flow: a loop wrap while the key is held forgets what it struck") {
    FlowRig rig(song_of({{0.5, 60}, {3.0, 64}}), FlowOptions{});
    REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
    rig.transport.set_loop_enabled(0, true);
    rig.play_at(1.0);
    rig.send(note_on(1.5, 60)); // song 0.5
    rig.run_until(2.0);
    REQUIRE(rig.eval.struck_start(60).has_value());
    rig.run_until(3.2); // the loop wrapped at wall 3.0
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
}

TEST_CASE("flow: a second press inside a struck note's window is wrong and strikes nothing") {
    FlowRig rig(song_of({{1.0, 60}, {4.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60));
    rig.run_until(2.01);
    CHECK(rig.eval.struck_start(60).has_value());
    CHECK_FALSE(rig.eval.press_looks_wrong(60));
    rig.send(note_off(2.02, 60));
    rig.send(note_on(2.05, 60)); // the note is already hit
    rig.run_until(2.06);
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
    CHECK(rig.eval.press_looks_wrong(60));
    rig.run_until(4.0);
    CHECK(rig.eval.summary().wrong == 1);
    std::vector<WrongPress> wrong;
    rig.eval.take_wrong_presses(wrong);
    REQUIRE(wrong.size() == 1);
    CHECK(wrong[0].key == 60);
}

TEST_CASE("flow: a press looks wrong when it struck nothing and is not early for a note to come") {
    FlowRig rig(song_of({{1.0, 60}, {3.0, 64}}), FlowOptions{});
    rig.play_at(1.0);
    rig.send(note_on(2.0, 60)); // on time
    rig.send(note_on(2.2, 61)); // no such note
    rig.send(note_on(2.3, 64)); // early for the note at song 3.0, inside the lookahead
    rig.run_until(2.4);
    CHECK_FALSE(rig.eval.press_looks_wrong(60));
    CHECK(rig.eval.press_looks_wrong(61));
    CHECK_FALSE(rig.eval.press_looks_wrong(64));
}

TEST_CASE("wait: a seek while the key is held forgets what it struck") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.run_until(2.6);
    REQUIRE(rig.eval.struck_start(60).has_value());
    rig.transport.seek(sec(2.6), 3.5);
    rig.flush_ops();
    CHECK_FALSE(rig.eval.struck_start(60).has_value());
}

TEST_CASE("wait: pressing a chord's key again before the chord is complete strikes it again") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.send(note_off(2.6, 60));
    rig.send(note_on(2.7, 60)); // not wrong: the chord still waits for it
    rig.run_until(2.8);
    REQUIRE(rig.eval.struck_start(60).has_value());
    CHECK(*rig.eval.struck_start(60) == doctest::Approx(1.0));
    CHECK_FALSE(rig.eval.press_looks_wrong(60));
    CHECK(rig.eval.summary().wrong == 0);
}

TEST_CASE("wait: a wrong press looks wrong and strikes nothing, a right one does not look wrong") {
    WaitRig rig(song_of({{1.0, 60}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 61));
    rig.send(note_on(2.55, 60));
    rig.run_until(2.6);
    CHECK(rig.eval.press_looks_wrong(61));
    CHECK_FALSE(rig.eval.struck_start(61).has_value());
    CHECK_FALSE(rig.eval.press_looks_wrong(60));
    CHECK(rig.eval.struck_start(60).has_value());
}

TEST_CASE("wait: a press handed over and not applied yet is pending, and not wrong until it is") {
    WaitRig rig(song_of({{1.0, 60}, {4.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.eval.apply(note_on(2.5, 61)); // stamped now: it waits settle_ms before it is applied
    CHECK(rig.eval.press_pending(61));
    CHECK_FALSE(rig.eval.press_looks_wrong(61));
    rig.run_until(2.6);
    CHECK_FALSE(rig.eval.press_pending(61));
    CHECK(rig.eval.press_looks_wrong(61));
}

// The presses go straight to the evaluator, so the look-ahead set between two of them is the one
// in force when each is applied (the rig's send() would hand them over only at the next step).
TEST_CASE("set_lookahead acts in place, and a press keeps the verdict of the look-ahead it was "
          "played under, in both modes") {
    const Song flow_song = song_of({{1.0, 60}, {3.0, 64}});
    const Song wait_song = song_of({{1.0, 60}, {3.0, 64}, {5.0, 72}});
    // Flow, 4 s then 1.5 s: a key 1.8 s early was fine when it was played and stays fine.
    {
        FlowRig rig(flow_song, FlowOptions{});
        rig.play_at(1.0);
        rig.eval.apply(note_on(1.2, 64)); // 1.8 s early for the note at 3.0
        rig.eval.set_lookahead(1.5);
        CHECK(rig.eval.options().lookahead_s == 1.5);
        rig.eval.apply(note_on(1.4, 64)); // 1.6 s early, played under 1.5 s: wrong
        rig.run_until(6.0);
        CHECK(rig.eval.summary().wrong == 1);
    }
    // Flow, 1.5 s then 4 s: wrong when played, and a later widening does not excuse it.
    {
        FlowOptions o;
        o.lookahead_s = 1.5;
        FlowRig rig(flow_song, o);
        rig.play_at(1.0);
        rig.eval.apply(note_on(1.2, 64));
        rig.eval.set_lookahead(4.0);
        rig.eval.apply(note_on(1.4, 64)); // played under 4 s: fine
        rig.run_until(6.0);
        CHECK(rig.eval.summary().wrong == 1);
    }
    // Wait, 4 s then 1.5 s: a press still in the inbox keeps the reach it arrived with.
    {
        WaitRig rig(wait_song, WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(2.4); // held at the chord at 1.0
        rig.eval.apply(note_on(2.5, 64)); // 0.5 s before the chord at 3.0
        rig.eval.set_lookahead(1.5, sec(2.4));
        CHECK(rig.eval.options().lookahead_s == 1.5);
        CHECK(rig.transport.held()); // nothing was reset
        CHECK(rig.eval.waiting_for_s() == 1.0);
        CHECK(rig.eval.press_pending(64));
        rig.eval.apply(note_on(2.8, 72)); // the chord at 5.0 is 2.2 s on: past 1.5 s, wrong
        rig.run_until(3.0);
        CHECK(rig.eval.summary().wrong == 1);
    }
    // Wait, 1.5 s then 4 s.
    {
        WaitOptions o;
        o.lookahead_s = 1.5;
        WaitRig rig(wait_song, o);
        rig.play_at(1.0);
        rig.run_until(1.9);
        rig.eval.apply(note_on(1.95, 72)); // 3.05 s before the chord at 5.0: wrong under 1.5 s
        rig.eval.set_lookahead(4.0, sec(1.9));
        rig.eval.apply(note_on(1.97, 64)); // 1.03 s before 3.0: fine either way
        rig.run_until(2.2);
        CHECK(rig.eval.summary().wrong == 1);
    }
    // Out-of-range and NaN values are clamped as the option is.
    FlowEvaluator f;
    f.set_lookahead(100.0);
    CHECK(f.options().lookahead_s == 60.0);
    f.set_lookahead(std::nan(""));
    CHECK(f.options().lookahead_s == FlowOptions{}.lookahead_s);
    WaitEvaluator w;
    w.set_lookahead(-3.0, 0);
    CHECK(w.options().lookahead_s == 0.0);
    w.set_lookahead(std::nan(""), 0);
    CHECK(w.options().lookahead_s == WaitOptions{}.lookahead_s);
}
