// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: transport (split from evaluator_test.cpp).
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
// Flow and Wait edge cases: song changes mid-play, lost key-ups, early presses, tied strikes,
// huge or invalid options, and passes dropped on loops and seeks.

TEST_CASE("flow: set_song while the transport keeps playing grades the next notes (F1)") {
    FlowRig rig(song_of({{3.0, 60}}), FlowOptions{});
    rig.play_at(1.0);
    rig.run_until(3.0); // song 2.0, still playing
    rig.eval.set_song(rig.song, FlowOptions{});
    rig.send(note_on(4.0, 60)); // on time
    rig.run_until(6.0);
    const EvaluationSummary& s = rig.eval.summary();
    CHECK(hits(s) == 1);
    CHECK(s.counts.missed == 0);
    CHECK(s.wrong == 0);
}

TEST_CASE("wait: set_song while the transport keeps playing arms the next hold (F1)") {
    WaitRig rig(song_of({{2.0, 60}, {4.0, 62}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.0); // song 1.0
    rig.eval.set_song(rig.song, WaitOptions{});
    rig.run_until(3.5);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 2.0);
    CHECK(rig.eval.waiting_for_s() == 2.0);
    rig.send(note_on(3.5, 60));
    rig.run_until(3.6);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 1);
}

TEST_CASE("wait: set_song while held keeps the song held at the chord (F1)") {
    WaitRig rig(song_of({{1.0, 60}, {3.0, 62}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5); // held at 1.0
    REQUIRE(rig.transport.held());
    rig.eval.set_song(rig.song, WaitOptions{});
    rig.run_until(2.7);
    CHECK(rig.transport.held());
    CHECK(rig.transport.position_s() == 1.0);
    rig.send(note_on(2.7, 60));
    rig.run_until(2.8);
    CHECK_FALSE(rig.transport.held());
}

TEST_CASE("flow and wait: a song change through the Transport grades from the next play (F1)") {
    const Song next = song_of({{1.0, 64}, {3.0, 65}});
    {
        FlowRig rig(song_of({{1.0, 60}, {5.0, 62}}), FlowOptions{});
        rig.play_at(1.0);
        rig.run_until(2.5);
        rig.transport.set_song(next);
        rig.song = next;
        rig.eval.set_song(next, FlowOptions{});
        rig.run_until(3.0);
        CHECK_FALSE(rig.transport.playing());
        rig.play_at(3.0);
        rig.send(note_on(4.0, 64));
        rig.run_until(7.0);
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(hits(s) == 1);
        CHECK(s.wrong == 0);
    }
    {
        WaitRig rig(song_of({{1.0, 60}, {5.0, 62}}), WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(2.5); // held at 1.0
        rig.transport.set_song(next);
        rig.song = next;
        rig.eval.set_song(next, WaitOptions{});
        rig.run_until(3.0);
        rig.play_at(3.0);
        rig.run_until(4.5);
        CHECK(rig.transport.held());
        CHECK(rig.transport.position_s() == 1.0);
        CHECK(rig.eval.waiting_for_s() == 1.0);
        rig.send(note_on(4.5, 64));
        rig.run_until(4.6);
        CHECK_FALSE(rig.transport.held());
        CHECK(rig.eval.summary().counts.played == 1);
    }
}

TEST_CASE("wait: a lost key-up does not swallow the next strike of that key (F2)") {
    WaitRig rig(song_of({{0.5, 60}, {1.0, 60}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.send(note_on(1.5, 60)); // plays the chord at 0.5; its note-off never comes
    rig.run_until(2.5);
    CHECK(rig.transport.held());
    CHECK(rig.eval.waiting_for_s() == 1.0);
    rig.send(note_on(2.5, 60)); // a note-on for a key already down is a new strike
    rig.run_until(2.6);
    CHECK_FALSE(rig.transport.held());
    CHECK(rig.eval.summary().counts.played == 2);
    CHECK(rig.eval.summary().wrong == 0);
}

TEST_CASE("wait: release_all lets go of every key, for a lost connection (F2)") {
    WaitRig rig(song_of({{1.0, 60}, {1.0, 64}, {3.0, 72}}), WaitOptions{});
    rig.play_at(1.0);
    rig.run_until(2.5);
    rig.send(note_on(2.5, 60));
    rig.run_until(2.6);
    rig.eval.release_all(sec(2.6)); // the keyboard was unplugged with 60 down
    rig.send(note_on(2.7, 64));
    rig.run_until(2.8);
    CHECK(rig.transport.held()); // 60 is no longer down
    rig.send(note_on(2.8, 60));
    rig.run_until(2.9);
    CHECK_FALSE(rig.transport.held());
}

TEST_CASE("wait: a press while paused or before play never plays a chord (F3)") {
    {
        WaitRig rig(song_of({{1.0, 60}, {3.0, 72}}), WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(1.9); // song 0.9
        rig.transport.pause(sec(1.9));
        rig.send(note_on(1.91, 60));
        rig.run_until(2.5);
        rig.play_at(2.5);
        rig.run_until(3.0);
        CHECK(rig.transport.held());
        CHECK(rig.transport.position_s() == 1.0);
        CHECK(rig.eval.waiting_for_s() == 1.0);
        CHECK(rig.eval.summary().counts.played == 0);
    }
    {
        WaitRig rig(song_of({{0.0, 60}, {2.0, 72}}), WaitOptions{});
        rig.send(note_on(0.5, 60)); // no play yet
        rig.run_until(0.9);
        rig.play_at(1.0);
        rig.run_until(1.5);
        CHECK(rig.transport.held()); // within a frame of the start
        CHECK(rig.eval.waiting_for_s() == 0.0);
        CHECK(rig.eval.summary().counts.played == 0);
    }
}

TEST_CASE("flow and wait agree on one key struck for two notes 21 ms apart (F4)") {
    const Song close = song_of({{1.000, 60, Hand::Right}, {1.021, 60, Hand::Left}, {3.0, 72}});
    {
        FlowRig rig(close, FlowOptions{});
        rig.play_at(1.0);
        rig.send(note_on(2.0105, 60));
        rig.send(note_on(4.0, 72));
        rig.run_until(6.0);
        CHECK(hits(rig.eval.summary()) == 3);
        CHECK(rig.eval.summary().counts.missed == 0);
    }
    {
        WaitRig rig(close, WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(2.5);
        rig.send(note_on(2.5, 60));
        rig.run_until(3.0);
        CHECK(rig.eval.summary().counts.played == 2);
        CHECK(rig.eval.waiting_for_s() == 3.0);
    }
    CHECK(kUnisonSeconds == kChordSeconds);
}

TEST_CASE("flow and wait agree that one key 40 ms apart needs two strikes (F4)") {
    const Song apart = song_of({{1.000, 60}, {1.040, 60}, {3.0, 72}});
    {
        FlowRig rig(apart, FlowOptions{});
        rig.play_at(1.0);
        rig.send(note_on(2.02, 60));
        rig.run_until(6.0);
        CHECK(hits(rig.eval.summary()) == 1);
        CHECK(rig.eval.summary().counts.missed == 2);
    }
    {
        WaitRig rig(apart, WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(2.5);
        rig.send(note_on(2.5, 60));
        rig.run_until(3.0);
        CHECK(rig.eval.summary().counts.played == 1);
        CHECK(rig.eval.waiting_for_s() == doctest::Approx(1.040));
    }
}

TEST_CASE("wait: a key of a later chord pressed early is not wrong (F5)") {
    const Song song = song_of({{1.0, 60}, {3.0, 64}, {5.0, 72}});
    {
        WaitRig rig(song, WaitOptions{});
        rig.play_at(1.0);
        rig.run_until(2.5); // held at 1.0
        rig.send(note_on(2.5, 64));
        rig.send(note_off(2.6, 64));
        rig.run_until(2.7);
        CHECK(rig.eval.summary().wrong == 0);
        CHECK(rig.transport.held()); // and it plays nothing
        CHECK(rig.eval.waiting_for_s() == 1.0);
    }
    {
        WaitOptions o;
        o.lookahead_s = 1.5; // the later chord is past the lookahead: a wrong key
        WaitRig rig(song, o);
        rig.play_at(1.0);
        rig.run_until(2.5);
        rig.send(note_on(2.5, 64));
        rig.run_until(2.7);
        CHECK(rig.eval.summary().wrong == 1);
    }
}

TEST_CASE("flow: a key of a later note pressed early is not wrong (F5)") {
    const Song song = song_of({{1.0, 60}, {3.0, 64}});
    {
        FlowRig rig(song, FlowOptions{});
        rig.play_at(1.0);
        rig.send(note_on(2.0, 60));
        rig.send(note_on(2.0, 64)); // 2 s early for the note at 3.0
        rig.send(note_on(4.0, 64));
        rig.run_until(6.0);
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(hits(s) == 2);
        CHECK(s.wrong == 0);
    }
    {
        FlowOptions o;
        o.lookahead_s = 1.5;
        FlowRig rig(song, o);
        rig.play_at(1.0);
        rig.send(note_on(2.0, 64));
        rig.run_until(6.0);
        CHECK(rig.eval.summary().wrong == 1);
    }
}

TEST_CASE("flow: a long run of one key is decided in bounded work (F9)") {
    const int n = 2500;
    std::vector<N> list;
    for (int i = 0; i < n; ++i) {
        list.push_back({0.1 + i * 0.040, 60, Hand::Right, 0.02});
    }
    FlowRig rig(song_of(list), FlowOptions{});
    rig.play_at(1.0);
    rig.transport.update(sec(1.0 + rig.song.duration_s));
    rig.flush_ops();
    for (int i = 0; i < n; ++i) {
        rig.eval.apply(note_on(1.0 + 0.1 + i * 0.040, 60));
    }
    rig.eval.update(sec(2.0 + rig.song.duration_s));
    CHECK(rig.eval.summary().counts.perfect == n);
    CHECK(rig.eval.pending_notes() == 0);
    // Counted in table cells, not read off a clock, so a busy machine can't fail it. The window is
    // 80 ms and the presses 40 ms apart: a note has five presses within reach, so a row holds
    // about six cells. The full table of this run holds n * n = 6.25 million cells.
    const std::uint64_t cells = rig.eval.matching_cells();
    CAPTURE(cells);
    CHECK(cells <= static_cast<std::uint64_t>(n) * 8);
    CHECK(cells >= static_cast<std::uint64_t>(n)); // and the work was done
}
