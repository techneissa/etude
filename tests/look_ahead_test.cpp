// SPDX-License-Identifier: GPL-3.0-or-later
// Handing the song's notes over ahead of the clock (etude/look_ahead.hpp): each note once, at
// its exact time, with Playback, the real Transport and a receiver that starts what is due.
#include "etude/look_ahead.hpp"

#include "etude/playback.hpp"
#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"
#include "etude/transport.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace etude;

namespace {

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kSec = 1'000'000'000;

// A note every 100 ms for `seconds`, 80 ms long, with a second voice a third above on every fifth.
Song steady_song(double seconds) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0); // 960 ticks a second
    const int count = static_cast<int>(seconds * 10.0);
    for (int i = 0; i < count; ++i) {
        for (int voice = 0; voice < (i % 5 == 0 ? 2 : 1); ++voice) {
            Note n;
            n.start_tick = i * 96;
            n.end_tick = n.start_tick + 77;
            n.pitch = static_cast<std::uint8_t>(60 + voice * 4 + (i % 12));
            n.velocity = 80;
            n.hand = Hand::Right;
            n.channel = 0;
            s.notes.push_back(n);
        }
    }
    REQUIRE(finish_song(s));
    return s;
}

// The song's sound as the app makes it: the real Transport, Playback and the planner, a receiver
// that starts each message when it is due, and the log of what was handed over and when.
struct Sim {
    Song song;
    Playback playback;
    Transport transport;
    DueQueue<8192> queue;
    double cursor = 0.0;
    int planned_wraps = 0;
    std::int64_t lead = kSongSoundLeadNs;
    std::int64_t t0 = 10 * kSec;
    struct Handed {
        PlaybackMessage m;
        std::int64_t handed_ns = 0;
    };
    std::vector<Handed> handed;
    std::vector<PlaybackMessage> started; // as the receiver started them, in order

    explicit Sim(Song s) : song(std::move(s)), playback(song) { transport.set_song(song); }

    void play(std::int64_t at) {
        transport.play(at);
        std::vector<TransportOp> ops;
        transport.take_ops(ops);
        std::vector<PlaybackMessage> out;
        for (const TransportOp& op : ops) {
            if (op.kind == TransportOp::Kind::Play) {
                playback.apply(op, out);
                cursor = op.to_s;
            }
        }
        planned_wraps = transport.loop_count();
    }

    // A frame at `now`: the clock settles, the notes of the next window are handed over, and the
    // receiver starts what is due by now.
    void frame(std::int64_t now) {
        transport.update(now);
        std::vector<TransportOp> ignored;
        transport.take_ops(ignored);
        LookAheadInput in;
        in.position_s = transport.position_s();
        in.speed_percent = transport.speed_percent();
        in.moving = transport.state() == TransportState::Playing;
        in.duration_s = transport.duration_s();
        in.loop_on = transport.loop_enabled();
        in.loop_start_s = transport.loop_start_s();
        in.loop_end_s = transport.loop_end_s();
        in.wraps_ahead = transport.loop_count() - planned_wraps;
        std::vector<TransportOp> plan_ops;
        planned_wraps += plan_look_ahead(in, now, lead, cursor, plan_ops).wraps_planned;
        std::vector<PlaybackMessage> out;
        for (const TransportOp& op : plan_ops) {
            playback.apply(op, out);
        }
        for (const PlaybackMessage& m : out) {
            handed.push_back({m, now});
            REQUIRE(queue.push(m));
        }
        advance(now);
    }

    // A message due at `now` has not started yet: that is the side a cancel at `now` takes too.
    void advance(std::int64_t now) {
        while (!queue.empty() && queue.front().at_ns < now) {
            started.push_back(queue.front());
            queue.pop_front();
        }
    }
};

std::vector<PlaybackMessage> note_ons(const std::vector<Sim::Handed>& log) {
    std::vector<PlaybackMessage> out;
    for (const auto& h : log) {
        if (starts_note(h.m)) {
            out.push_back(h.m);
        }
    }
    return out;
}

std::size_t count_offs(const std::vector<Sim::Handed>& log) {
    return static_cast<std::size_t>(
        std::count_if(log.begin(), log.end(), [](const auto& h) { return ends_note(h.m); }));
}

// Plays `seconds` of the clock in frames of random length in [min_ms, max_ms].
void run_frames(Sim& sim, double seconds, int min_ms, int max_ms, unsigned seed) {
    std::mt19937 rng(seed);
    std::int64_t now = sim.t0;
    sim.play(now);
    sim.frame(now);
    const std::int64_t end = sim.t0 + static_cast<std::int64_t>(seconds * 1e9);
    while (now < end) {
        now += static_cast<std::int64_t>(
                   min_ms + static_cast<int>(rng() % static_cast<unsigned>(max_ms - min_ms + 1))) *
               kMs;
        sim.frame(now);
    }
}

} // namespace

TEST_CASE(
    "frames shorter than the look-ahead hand every note over once, in time, at its exact time") {
    for (const int speed : {100, 75, 150}) {
        for (const unsigned seed : {1u, 2u, 3u}) {
            INFO("speed " << speed << " seed " << seed);
            const Song song = steady_song(6.0);
            Sim sim(song);
            sim.transport.set_speed_percent(sim.t0, speed);
            run_frames(sim, 6.0 * 100.0 / speed + 0.5, 1, 29, seed);
            const auto ons = note_ons(sim.handed);
            REQUIRE(ons.size() == song.notes.size()); // none missed, none twice
            std::vector<double> starts;
            for (const Note& n : song.notes) {
                starts.push_back(n.start_s);
            }
            std::sort(starts.begin(), starts.end());
            for (std::size_t i = 0; i < ons.size(); ++i) {
                CHECK(ons[i].song_s == doctest::Approx(starts[i]));
                const std::int64_t exact = sim.t0 + std::llround(starts[i] / (speed / 100.0) * 1e9);
                CHECK(std::llabs(ons[i].at_ns - exact) <= 3); // the exact time, to rounding
            }
            for (const auto& h : sim.handed) {
                if (starts_note(h.m)) {
                    CHECK(h.handed_ns <= h.m.at_ns); // handed over before it was due
                }
            }
            // The last note ends with the song: its note-off is the End's.
            CHECK(count_offs(sim.handed) + 2 >= ons.size());
            // The receiver started them in order.
            CHECK(std::is_sorted(sim.started.begin(), sim.started.end(),
                                 [](const auto& a, const auto& b) { return a.at_ns < b.at_ns; }));
        }
    }
}

TEST_CASE(
    "frames longer than the look-ahead hand notes over late, and drop none, note-offs included") {
    for (const unsigned seed : {4u, 5u, 6u}) {
        INFO("seed " << seed);
        const Song song = steady_song(6.0);
        Sim sim(song);
        run_frames(sim, 6.5, 20, 120, seed);
        const auto ons = note_ons(sim.handed);
        REQUIRE(ons.size() == song.notes.size());
        int late = 0;
        for (const auto& h : sim.handed) {
            if (starts_note(h.m) && h.handed_ns > h.m.at_ns) {
                ++late;
            }
        }
        CHECK(late > 0);
        // The due times are the notes' own whatever the frames did.
        for (std::size_t i = 1; i < ons.size(); ++i) {
            CHECK(ons[i].at_ns >= ons[i - 1].at_ns);
        }
        CHECK(std::llabs(ons.back().at_ns -
                         (sim.t0 + std::llround(song.notes.back().start_s * 1e9))) <= 3);
        CHECK(count_offs(sim.handed) + 2 >= ons.size());
    }
}

TEST_CASE(
    "a window that crosses the loop end continues from the loop start, nothing past the end") {
    for (const bool long_frames : {false, true}) {
        INFO("long frames " << long_frames);
        const Song song = steady_song(6.0);
        Sim sim(song);
        sim.transport.seek(sim.t0, 1.55);
        REQUIRE(sim.transport.set_loop(sim.t0, 2.0, 3.0));
        sim.transport.set_loop_enabled(sim.t0, true);
        std::vector<TransportOp> ignored;
        sim.transport.take_ops(ignored);
        std::mt19937 rng(11);
        std::int64_t now = sim.t0;
        sim.play(now);
        sim.frame(now);
        const std::int64_t end = sim.t0 + static_cast<std::int64_t>(4.4 * 1e9);
        while (now < end) {
            now += (long_frames ? 20 + static_cast<int>(rng() % 90)
                                : 1 + static_cast<int>(rng() % 29)) *
                   kMs;
            sim.frame(now);
        }
        const auto ons = note_ons(sim.handed);
        REQUIRE_FALSE(ons.empty());
        for (const PlaybackMessage& m : ons) {
            CHECK(m.song_s >= 1.55);
            CHECK(m.song_s < 3.0); // never a note past the loop end
        }
        // Notes in [1.55, 2.0) once, then passes of [2.0, 3.0): the n-th pass starts at
        // 0.45 + n s after the start; a note's time is that plus its offset in the loop.
        std::vector<std::int64_t> expected;
        for (const Note& n : song.notes) {
            if (n.start_s >= 1.55 && n.start_s < 2.0) {
                expected.push_back(sim.t0 + std::llround((n.start_s - 1.55) * 1e9));
            }
        }
        for (int pass = 0; pass < 4; ++pass) {
            for (const Note& n : song.notes) {
                if (n.start_s >= 2.0 && n.start_s < 3.0) {
                    expected.push_back(sim.t0 +
                                       std::llround((0.45 + pass + (n.start_s - 2.0)) * 1e9));
                }
            }
        }
        std::sort(expected.begin(), expected.end());
        // What was handed over is a prefix of what the clock plays: everything due by the time the
        // last frame looked ahead to, each once and at its time.
        REQUIRE(ons.size() <= expected.size());
        REQUIRE(ons.size() >= expected.size() - 12);
        for (std::size_t i = 0; i < ons.size(); ++i) {
            CHECK(std::llabs(ons[i].at_ns - expected[i]) <= 3);
        }
        CHECK(sim.transport.loop_count() >= 3);
    }
}

TEST_CASE("the window stops at the end of the song and at an armed hold, and a held clock looks "
          "nowhere ahead") {
    double cursor = 0.0;
    std::vector<TransportOp> ops;
    LookAheadInput in;
    in.position_s = 9.99;
    in.duration_s = 10.0;
    plan_look_ahead(in, 100 * kSec, kSongSoundLeadNs, cursor, ops);
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].to_s == 10.0);
    CHECK(cursor == 10.0);
    ops.clear();
    plan_look_ahead(in, 100 * kSec, kSongSoundLeadNs, cursor, ops); // nothing new
    CHECK(ops.empty());

    cursor = 5.0;
    in.position_s = 5.0;
    in.hold_at_s = 5.02;
    plan_look_ahead(in, 100 * kSec, kSongSoundLeadNs, cursor, ops);
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].to_s == doctest::Approx(5.02));

    cursor = 5.0;
    ops.clear();
    in.hold_at_s.reset();
    in.moving = false;
    in.position_s = 5.0;
    plan_look_ahead(in, 100 * kSec, kSongSoundLeadNs, cursor, ops);
    CHECK(ops.empty()); // held at the cursor: nothing ahead
    in.position_s = 5.05;
    plan_look_ahead(in, 100 * kSec, kSongSoundLeadNs, cursor, ops); // behind: caught up, late
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].to_s == doctest::Approx(5.05));
    CHECK(ops[0].end_ns == 100 * kSec);
}

TEST_CASE("a Move carries the exact wall times: the position now maps to now, the speed scales") {
    double cursor = 2.0;
    std::vector<TransportOp> ops;
    LookAheadInput in;
    in.position_s = 2.0;
    in.speed_percent = 50;
    in.duration_s = 10.0;
    plan_look_ahead(in, 7 * kSec, 30 * kMs, cursor, ops);
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].from_s == 2.0);
    CHECK(ops[0].to_s == doctest::Approx(2.015)); // 30 ms of clock at half speed
    CHECK(ops[0].at_ns == 7 * kSec);
    CHECK(ops[0].end_ns == 7 * kSec + 30 * kMs);
}

TEST_CASE("cancelling and going on from the position sends no note twice and misses none") {
    const Song song = steady_song(6.0);
    Sim sim(song);
    std::mt19937 rng(21);
    std::int64_t now = sim.t0;
    sim.play(now);
    sim.frame(now);
    const std::int64_t end = sim.t0 + 6 * kSec + 500 * kMs;
    int cancels = 0;
    while (now < end) {
        now += (1 + static_cast<int>(rng() % 29)) * kMs;
        sim.frame(now);
        if (rng() % 9 == 0) {
            // A speed-less change (a mute, say): everything not started is cancelled, Playback goes
            // back to the position and the plan goes on from it.
            sim.queue.cancel_unstarted(now);
            sim.playback.rewind(sim.transport.position_s());
            sim.cursor = sim.transport.position_s();
            ++cancels;
            // Notes of the lookahead were taken back: forget them in the log of what is handed.
        }
    }
    sim.advance(end + kSec);
    REQUIRE(cancels > 5);
    // What the receiver started: each note once, at its exact time.
    std::vector<PlaybackMessage> on;
    std::size_t offs = 0;
    for (const PlaybackMessage& m : sim.started) {
        if (starts_note(m)) {
            on.push_back(m);
        } else if (ends_note(m)) {
            ++offs;
        }
    }
    REQUIRE(on.size() == song.notes.size());
    std::vector<double> starts;
    for (const Note& n : song.notes) {
        starts.push_back(n.start_s);
    }
    std::sort(starts.begin(), starts.end());
    std::sort(on.begin(), on.end(), [](const auto& a, const auto& b) { return a.at_ns < b.at_ns; });
    for (std::size_t i = 0; i < on.size(); ++i) {
        CHECK(std::llabs(on[i].at_ns - (sim.t0 + std::llround(starts[i] * 1e9))) <= 3);
    }
    CHECK(offs >= on.size() - 3); // the notes that had not ended when the run stopped aside
}

TEST_CASE("Playback rewound to a position forgets the notes not started and keeps those sounding") {
    const Song song = steady_song(2.0);
    Playback pb(song);
    std::vector<PlaybackMessage> out;
    TransportOp play;
    play.kind = TransportOp::Kind::Play;
    play.at_ns = 0;
    play.to_s = 0.0;
    pb.apply(play, out);
    out.clear();
    TransportOp move;
    move.kind = TransportOp::Kind::Move;
    move.from_s = 0.0;
    move.to_s = 0.55;
    move.at_ns = 0;
    move.end_ns = 550 * kMs;
    pb.apply(move, out);
    // The two notes at 0.50 started and last to 0.58.
    CHECK(pb.sounding_count() == 2);
    pb.rewind(0.45);
    // Only notes that started before 0.45 are still sounding: none (the one at 0.4 ended at 0.48,
    // after the Move; it was released then).
    CHECK(pb.sounding_count() == 0);
    out.clear();
    move.from_s = 0.45;
    move.to_s = 0.62;
    pb.apply(move, out);
    int ons = 0;
    for (const auto& m : out) {
        ons += starts_note(m) ? 1 : 0;
    }
    CHECK(ons == 3); // the two at 0.5 and the one at 0.6, each once
}
