// SPDX-License-Identifier: GPL-3.0-or-later
// evaluator: alloc (split from evaluator_test.cpp).
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

TEST_CASE("flow: grading while a song plays allocates nothing (steady state)") {
    PlayedSong played = played_song();
    Transport transport;
    FlowEvaluator eval;
    transport.set_song(played.song);
    eval.set_song(played.song, FlowOptions{});
    std::vector<TransportOp> ops;
    ops.reserve(64);
    transport.set_speed_percent(sec(1.0), 100);
    transport.play(sec(1.0));
    std::size_t next = 0;
    std::int64_t now = sec(1.0);
    const auto frame = [&] {
        now += 16'666'667;
        transport.update(now);
        ops.clear();
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
        while (next < played.presses.size() && played.presses[next].time_ns <= now) {
            eval.apply(played.presses[next++]);
        }
        eval.update(now);
    };
    for (int i = 0; i < 180; ++i) { // the first frames grow the buffers
        frame();
    }
    etude::test::start_counting_allocations();
    const std::int64_t end = sec(1.0 + played.song.duration_s);
    int frames = 0;
    while (now < end) {
        frame();
        ++frames;
    }
    const int allocations = etude::test::stop_counting_allocations();
    CHECK(frames > 1000);
    CHECK(allocations == 0);
    eval.finish();
    // And it graded: most notes were hit, the missed ones are missed, the wrong keys counted.
    CHECK(hits(eval.summary()) > static_cast<int>(played.song.notes.size()) * 8 / 10);
    CHECK(eval.summary().counts.missed > 0);
    CHECK(eval.summary().wrong > 0);
}

TEST_CASE("wait: grading while a song plays allocates nothing (steady state)") {
    // The player is a little early on every note, so the song never has to wait for a late chord
    // and runs to its end; the evaluator grades it all and asks the heap for nothing.
    PlayedSong played = played_song(true);
    WaitRig rig(played.song, WaitOptions{});
    rig.play_at(1.0);
    std::size_t next = 0;
    std::size_t keys_seen = 0;
    const auto frame = [&] {
        const std::int64_t at = rig.now + rig.frame;
        while (next < played.presses.size() && played.presses[next].time_ns <= at) {
            rig.eval.apply(played.presses[next++]);
        }
        rig.now = at;
        rig.transport.update(at);
        const WaitDirective d = rig.eval.update(at);
        if (d.hold_now) {
            rig.transport.hold(at);
        }
        if (d.release) {
            rig.transport.release(at);
        }
        if (d.arm_hold) {
            rig.transport.hold_at(at, d.hold_at_s);
        }
        rig.frame_ops.clear();
        rig.transport.take_ops(rig.frame_ops);
        for (const TransportOp& op : rig.frame_ops) {
            rig.eval.apply(op);
        }
        keys_seen += rig.eval.waiting_keys().size(); // the frame asks for the keys it shows
    };
    for (int i = 0; i < 180; ++i) { // the first frames grow the buffers
        frame();
    }
    etude::test::start_counting_allocations();
    const std::int64_t end = sec(1.0 + rig.song.duration_s);
    int frames = 0;
    while (rig.now < end) {
        frame();
        ++frames;
    }
    const int allocations = etude::test::stop_counting_allocations();
    CHECK(frames > 1000);
    CHECK(allocations == 0);
    // It graded the song: every note was played, none missed or wrong.
    const EvaluationSummary& summary = rig.eval.summary();
    CHECK(summary.counts.played == static_cast<int>(played.song.notes.size()));
    CHECK(summary.counts.missed == 0);
    CHECK(summary.wrong == 0);
    CHECK(keys_seen > 0);
}

TEST_CASE("flow: undecided notes sort the same either side of the short-list limit") {
    // Notes on descending keys, all undecided at once, so the sort has to reorder them. The list
    // is sorted by insertion up to 64 entries and by std::stable_sort past that.
    for (const int n : {1, 63, 64, 65, 66}) {
        CAPTURE(n);
        std::vector<N> list;
        for (int i = 0; i < n; ++i) {
            list.push_back({0.1 + i * 0.5, 100 - i, Hand::Right, 0.1});
        }
        FlowRig rig(song_of(list), FlowOptions{});
        rig.play_at(1.0);
        rig.transport.update(sec(1.0 + rig.song.duration_s));
        rig.flush_ops();
        for (int i = 0; i < n; ++i) {
            rig.eval.apply(note_on(1.0 + 0.1 + i * 0.5, 100 - i));
        }
        rig.eval.update(sec(2.0 + rig.song.duration_s));
        CHECK(rig.eval.summary().counts.perfect == n);
        CHECK(rig.eval.summary().wrong == 0);
        CHECK(rig.eval.pending_notes() == 0);
    }
}

TEST_CASE("flow: chords decided together keep their order either side of the sort limit") {
    // Chords of three keys, written from the highest key down, each key pressed at exactly the
    // note's time: the notes of a chord and their outcomes have equal times, so a sort that is not
    // stable (or not the same either side of the limit) would reorder them. 3 * n outcomes
    // straddle 64.
    for (const int chords : {1, 21, 22, 23}) {
        CAPTURE(chords);
        std::vector<N> list;
        for (int i = 0; i < chords; ++i) {
            for (const int key : {72, 60, 48}) {
                list.push_back({0.1 + i * 0.5, key, Hand::Right, 0.1});
            }
        }
        FlowRig rig(song_of(list), FlowOptions{});
        rig.play_at(1.0);
        rig.transport.update(sec(1.0 + rig.song.duration_s));
        rig.flush_ops();
        for (int i = 0; i < chords; ++i) {
            for (const int key : {72, 60, 48}) {
                rig.eval.apply(note_on(1.0 + 0.1 + i * 0.5, key));
            }
        }
        rig.eval.update(sec(2.0 + rig.song.duration_s));
        const EvaluationSummary& summary = rig.eval.summary();
        CHECK(summary.counts.perfect == chords * 3);
        CHECK(summary.wrong == 0);
        REQUIRE(summary.notes.size() == static_cast<std::size_t>(chords) * 3);
        // In time order, and within one chord by rising key (the order the keys are decided in).
        for (std::size_t i = 1; i < summary.notes.size(); ++i) {
            const NoteResult& before = summary.notes[i - 1];
            const NoteResult& after = summary.notes[i];
            CHECK(before.song_s <= after.song_s);
            if (before.song_s == after.song_s) {
                CHECK(before.expected_pitch < after.expected_pitch);
            }
        }
    }
}

TEST_CASE("flow: the banded matching keeps the optimal assignment (F9)") {
    // Random runs of one key with one jittered press per note: a full assignment exists, so the
    // optimal one hits every note.
    for (unsigned seed = 1; seed <= 20; ++seed) {
        CAPTURE(seed);
        std::mt19937 rng(seed);
        std::uniform_real_distribution<double> gap(0.06, 0.2);
        std::uniform_real_distribution<double> jitter(-0.075, 0.075);
        std::vector<N> list;
        double t = 0.2;
        for (int i = 0; i < 300; ++i) {
            list.push_back({t, 60, Hand::Right, 0.03});
            t += gap(rng);
        }
        FlowRig rig(song_of(list), FlowOptions{});
        rig.play_at(1.0);
        for (const N& note : list) {
            rig.send(note_on(1.0 + note.start + jitter(rng), 60));
        }
        rig.run_until(1.0 + rig.song.duration_s + 1.0);
        rig.eval.finish();
        const EvaluationSummary& s = rig.eval.summary();
        CHECK(hits(s) == static_cast<int>(list.size()));
        CHECK(s.wrong == 0);
    }
}

TEST_CASE("flow: NaN, infinite and huge options fall back or are clamped (F11)") {
    const double nan = std::numeric_limits<double>::quiet_NaN();
    const double inf = std::numeric_limits<double>::infinity();
    FlowOptions o;
    o.window_ms = nan;
    o.perfect_fraction = nan;
    o.good_fraction = nan;
    o.latency_ms = nan;
    o.settle_ms = 1e13;
    o.lookahead_s = inf;
    FlowEvaluator e(song_of({{1.0, 60}}), o);
    CHECK(e.options().window_ms == FlowOptions{}.window_ms);
    CHECK(e.options().perfect_fraction == FlowOptions{}.perfect_fraction);
    CHECK(e.options().good_fraction == FlowOptions{}.good_fraction);
    CHECK(e.options().latency_ms == 0.0);
    CHECK(e.options().settle_ms <= 1000.0);
    CHECK(std::isfinite(e.options().lookahead_s));
    FlowRig rig(song_of({{1.0, 60}}), o);
    rig.play_at(1.0);
    rig.send(note_on(2.005, 60)); // 5 ms late
    rig.run_until(4.0);
    rig.eval.finish();
    CHECK(rig.eval.summary().counts.perfect == 1);
    CHECK(rig.eval.summary().wrong == 0);

    o.window_ms = inf;
    o.latency_ms = -inf;
    FlowEvaluator f(song_of({{1.0, 60}}), o);
    CHECK(f.options().window_ms == 500.0);
    CHECK(std::isfinite(f.options().latency_ms));

    WaitOptions w;
    w.early_ms = nan;
    w.release_slack_ms = inf;
    w.settle_ms = nan;
    w.lookahead_s = nan;
    WaitEvaluator we(song_of({{1.0, 60}}), w);
    CHECK(we.options().early_ms == WaitOptions{}.early_ms);
    CHECK(std::isfinite(we.options().release_slack_ms));
    CHECK(we.options().settle_ms == WaitOptions{}.settle_ms);
    CHECK(we.options().lookahead_s == WaitOptions{}.lookahead_s);
}

TEST_CASE("flow and wait: passes taken each frame are forgotten, however many loops (F13)") {
    {
        FlowRig rig(song_of({{0.05, 60}}, 1.0), FlowOptions{});
        REQUIRE(rig.transport.set_loop(0, 0.0, 0.1));
        rig.transport.set_loop_enabled(0, true);
        rig.play_at(1.0);
        std::vector<PassResult> passes;
        std::size_t most = 0;
        while (rig.transport.loop_count() < 3000) {
            rig.step(rig.now + rig.frame);
            rig.eval.take_passes(passes);
            most = std::max(most, rig.eval.kept_passes());
        }
        CHECK(most <= 4);
        CHECK(passes.size() >= 2990);
    }
    {
        WaitRig rig(song_of({{0.05, 60}}, 1.0), WaitOptions{});
        REQUIRE(rig.transport.set_loop(0, 0.0, 0.1));
        rig.transport.set_loop_enabled(0, true);
        rig.play_at(1.0);
        std::vector<PassResult> passes;
        std::size_t most = 0;
        bool down = false;
        while (rig.transport.loop_count() < 1000) {
            rig.step(rig.now + rig.frame);
            if (rig.transport.held()) {
                const double t = static_cast<double>(rig.now) * 1e-9;
                rig.send(down ? note_off(t, 60) : note_on(t, 60));
                down = !down;
            }
            rig.eval.take_passes(passes);
            most = std::max(most, rig.eval.kept_passes());
        }
        CHECK(most <= 4);
        CHECK(passes.size() >= 990);
    }
}

TEST_CASE("flow: a pass cut short by a seek is dropped, not kept forever (F13)") {
    FlowRig rig(song_of({{1.0, 60}, {5.0, 62}}), FlowOptions{});
    rig.play_at(1.0);
    for (int i = 0; i < 50; ++i) {
        rig.run_until(1.5 + i * 0.1);
        rig.transport.seek(rig.now, 0.5);
    }
    rig.run_until(7.0);
    std::vector<PassResult> passes;
    rig.eval.take_passes(passes);
    CHECK(rig.eval.kept_passes() <= 2);
}
