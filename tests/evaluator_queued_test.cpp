// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: queued (split from evaluator_test.cpp).
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
// Wait Mode: a press still in the inbox when the state it was played under changes

TEST_CASE("wait: a press still queued when the transposition changes is not marked wrong") {
    // C4 at song 3.0 arrives at wall 4.0. C4 is pressed 10 ms early, the player taps transpose +2
    // 5 ms later and the press is still inside the settle window.
    auto run = [](bool transpose) {
        WaitRig rig(song_of({{3.0, 60}}), WaitOptions{});
        rig.play_at(1.0);
        rig.send(note_on(3.99, 60));
        rig.run_until(3.99);
        if (transpose) {
            rig.eval.set_transposition(Transposition(2, kRange61), sec(3.995));
        }
        rig.run_until(4.00);
        rig.run_until(4.01);
        rig.run_until(4.1);
        return rig.eval.summary();
    };
    const EvaluationSummary plain = run(false);
    CHECK(plain.counts.played == 1);
    CHECK(plain.wrong == 0);
    const EvaluationSummary changed = run(true);
    CHECK(changed.counts.played == 1);
    CHECK(changed.wrong == 0);
}

TEST_CASE("wait: a press stamped after the transposition change is judged by the new mapping") {
    WaitRig rig(song_of({{3.0, 60}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(3.9);
    rig.eval.set_transposition(Transposition(2, kRange61), sec(3.9));
    rig.send(note_on(3.99, 60)); // the old key: the chord is on D4 now
    rig.run_until(4.1);
    CHECK(rig.eval.summary().wrong == 1);
    CHECK(rig.eval.summary().counts.played == 0);
    rig.send(note_on(4.2, 62));
    rig.run_until(4.4);
    CHECK(rig.eval.summary().counts.played == 1);
}

TEST_CASE("wait: a press still queued when a seek lands is not marked wrong") {
    WaitRig rig(song_of({{3.0, 60}, {8.0, 64}}), WaitOptions{});
    rig.play_at(1.0);
    rig.send(note_on(3.99, 60));
    rig.run_until(3.99);
    rig.transport.seek(sec(3.995), 6.0); // the chord the press was for is now behind the position
    rig.run_until(4.00);
    rig.run_until(4.01);
    rig.run_until(4.1);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.wrong == 0);
    CHECK(s.counts.played == 1);
    CHECK(rig.eval.waiting_for_s() == 8.0);
}

TEST_CASE("wait: a press still queued at a loop wrap belongs to the lap it was played in") {
    // Chords at 0 and 1 s, loop 0..2 s. A wrong key is pressed in the last frame before the wrap:
    // lap one's, not the next lap's, so lap two can still be clean.
    auto make = [](WaitRig& rig) {
        REQUIRE(rig.transport.set_loop(0, 0.0, 2.0));
        rig.transport.set_loop_enabled(0, true);
        rig.play_at(1.0);
        rig.run_until(1.1);
        rig.send(note_on(1.1, 60));
        rig.send(note_off(1.2, 60));
        rig.run_until(2.2);
        rig.send(note_on(2.2, 62));
        rig.send(note_off(2.3, 62));
        rig.run_until(2.4);
    };
    int steps = 0;
    {
        WaitRig probe(song_of({{0.0, 60}, {1.0, 62}}), WaitOptions{});
        make(probe);
        while (probe.transport.loop_count() == 0) {
            probe.step(probe.now + probe.frame);
            ++steps;
        }
    }
    REQUIRE(steps > 1);
    WaitRig rig(song_of({{0.0, 60}, {1.0, 62}}), WaitOptions{});
    make(rig);
    for (int i = 0; i < steps - 1; ++i) {
        rig.step(rig.now + rig.frame);
    }
    REQUIRE(rig.transport.loop_count() == 0);
    MidiEvent stray = note_on(0.0, 61); // wrong, stamped at this frame, before the wrap
    stray.time_ns = rig.now;
    rig.eval.apply(stray);
    rig.step(rig.now + rig.frame); // the wrap lands with the stray press still queued
    REQUIRE(rig.transport.loop_count() == 1);
    for (int i = 0; i < 6; ++i) {
        rig.step(rig.now + rig.frame);
    }
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    REQUIRE(!passes.empty());
    CHECK(passes[0].wrong == 1);
    CHECK_FALSE(passes[0].clean);
    CHECK(rig.eval.summary().wrong == 1);
    CHECK(rig.eval.waiting_for_s() == 0.0); // lap two waits from the loop start again
}

TEST_CASE("wait: a press still queued when the song switches is not judged against the new song") {
    const Song other = song_of({{3.0, 64}, {6.0, 65}});
    WaitRig rig(song_of({{3.0, 60}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(3.99);
    rig.eval.apply(note_on(3.99, 60)); // right for the old song, still inside the settle window
    CHECK(rig.eval.press_pending(60));
    rig.song = other;
    rig.eval.set_song(other, WaitOptions{});
    CHECK_FALSE(rig.eval.press_pending(60));
    rig.run_until(4.00);
    rig.run_until(4.1);
    CHECK(rig.eval.summary().wrong == 0);
    CHECK(rig.eval.press_looks_wrong(60) == false);
}

TEST_CASE("wait: a followed transposition change settles the whole batch stamped before it") {
    // The tap at 3.995 s is made before the frame's presses are handed over, one after the other
    // (before the frame's clock ops, as the host does): C4 at 3.981 s and E4 at 3.990 s, both for
    // the chord at song 3.0 s, both played under the old mapping. The second must not be judged
    // by the new one (D4 and F#4) because the first one's insert found the change.
    TranspositionSource source;
    WaitRig rig(song_of({{3.0, 60}, {3.0, 64}}), WaitOptions{});
    rig.eval.follow(&source);
    rig.play_at(1.0);
    rig.run_until(3.98);
    source.set(Transposition(2, kRange61), sec(3.995));
    rig.eval.apply(note_on(3.981, 60));
    rig.eval.apply(note_on(3.990, 64));
    rig.run_until(4.0);
    rig.run_until(4.1);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(s.wrong == 0);
    CHECK(s.counts.played == 2);
}

TEST_CASE("wait: a press queued before set_lookahead is judged by the look-ahead it was played under") {
    const Song song = song_of({{1.0, 60}, {3.0, 64}, {5.0, 72}});
    // 4 s while it was played: the key of the chord at 3.0, 0.6 s early, is not wrong. The press
    // is stamped before the change but not yet settled when the Notes-ahead slider narrows it.
    WaitRig rig(song, WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.4); // held at the chord at 1.0
    rig.eval.apply(note_on(2.39, 64));
    CHECK(rig.eval.press_pending(64));
    rig.eval.set_lookahead(0.5, sec(2.4)); // the chord at 3.0 is now past the reach
    CHECK_FALSE(rig.eval.press_pending(64)); // judged now, under the old value
    rig.run_until(2.7);
    CHECK(rig.eval.summary().wrong == 0);
    // And a press played after the change is judged by the new one.
    rig.eval.apply(note_on(2.75, 64));
    rig.run_until(3.0);
    CHECK(rig.eval.summary().wrong == 1);
}
