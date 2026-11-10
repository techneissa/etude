// SPDX-License-Identifier: GPL-3.0-or-later
// The song clock: the position follows the caller's clock and the speed, pause and play never catch
// up, the end stops once, the A-B loop wraps B to A, a count-in waits its beats, a hold stops the
// clock where a chord waits, and bar ranges turn into times across tempo and meter changes.
#include "etude/look_ahead.hpp"
#include "etude/midi_file.hpp"
#include "etude/playback.hpp"
#include "etude/song_schedule.hpp"
#include "etude/transport.hpp"
#include "timed_sender.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <bitset>
#include <chrono>
#include <cstdint>
#include <iterator>
#include <optional>
#include <vector>

using namespace etude;
using namespace etude::test;
using Kind = TransportOp::Kind;

namespace {

// Whole seconds and halves are exact in binary, so positions compare exactly.
constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(s * 1e9); }

Song plain_song(double duration_s) {
    Song s;
    s.duration_s = duration_s;
    return s;
}

Song midi_song(const Track& track) {
    MidiLoadResult r = load_midi(smf({track}));
    REQUIRE(r.ok);
    return r.song;
}

// `bars` bars of 4/4 at 120 BPM (2 s each), one note across all of them.
Song bars_song(int bars) {
    Track t;
    t.bpm(0, 120)
        .time_sig(0, 4, 4)
        .on(0, 0, 60)
        .off(1920 * static_cast<std::uint32_t>(bars), 0, 60)
        .end();
    return midi_song(t);
}

std::vector<TransportOp> drain(Transport& t) {
    std::vector<TransportOp> ops;
    t.take_ops(ops);
    return ops;
}

std::vector<TransportOp> only(const std::vector<TransportOp>& ops, Kind kind) {
    std::vector<TransportOp> out;
    std::copy_if(ops.begin(), ops.end(), std::back_inserter(out),
                 [&](const TransportOp& o) { return o.kind == kind; });
    return out;
}

Transport ready(const Song& song) {
    Transport t;
    t.set_song(song);
    return t;
}

} // namespace

TEST_CASE(
    "transport: the position follows the clock and the speed, and a speed change moves nothing") {
    Transport t = ready(plain_song(100));
    CHECK(t.position_s() == 0.0);
    CHECK_FALSE(t.playing());
    t.play(sec(1000));
    t.update(sec(1003.5));
    CHECK(t.position_s() == 3.5);
    t.update(sec(1002)); // a time that is not later than the last is ignored
    CHECK(t.position_s() == 3.5);

    // The 0.25 s before a speed change run at the old speed; after it the clock scales.
    t.set_speed_percent(sec(1003.75), 50);
    CHECK(t.position_s() == 3.75);
    t.update(sec(1005.75));
    CHECK(t.position_s() == 4.75);
    t.set_speed_percent(sec(1005.75), 150);
    t.update(sec(1006.75));
    CHECK(t.position_s() == 6.25);

    // Speed is clamped to 25..200 and snaps to steps of five.
    Transport speed = ready(plain_song(100));
    CHECK(speed.set_speed_percent(0, 10) == 25);
    CHECK(speed.set_speed_percent(0, 260) == 200);
    CHECK(speed.set_speed_percent(0, 63) == 63);
    CHECK(snap_speed_percent(27) == 25);
    CHECK(snap_speed_percent(28) == 30);
}

TEST_CASE("transport: pause freezes the position and play resumes without catching up") {
    Transport t = ready(plain_song(10));
    t.play(0);
    t.pause(sec(2.5)); // no update() in between: the clock settles to the pause time first
    CHECK(t.position_s() == 2.5);
    t.update(sec(8));
    CHECK(t.position_s() == 2.5);
    t.play(sec(8));
    t.update(sec(9));
    CHECK(t.position_s() == 3.5);

    // A seek jumps and keeps playing from there; it clamps to the song; paused it stays paused.
    t.seek(sec(9), 6);
    t.update(sec(10));
    CHECK(t.position_s() == 7.0);
    t.seek(sec(10), -4);
    CHECK(t.position_s() == 0.0);
    t.seek(sec(10), 99);
    CHECK(t.position_s() == 10.0);
    Transport paused = ready(plain_song(10));
    paused.seek(sec(1), 4);
    paused.update(sec(3));
    CHECK(paused.position_s() == 4.0);
    CHECK_FALSE(paused.playing());
}

TEST_CASE("transport: the end of the song stops the clock once; play after it starts over") {
    Transport t = ready(plain_song(2));
    t.play(0);
    drain(t);
    t.update(sec(5));
    CHECK(t.position_s() == 2.0);
    CHECK(t.ended());
    std::vector<TransportOp> ops = drain(t);
    REQUIRE(only(ops, Kind::End).size() == 1);
    CHECK(only(ops, Kind::End)[0].at_ns == sec(2)); // when it got there, not when update() ran
    t.update(sec(9));
    CHECK(only(drain(t), Kind::End).empty());

    t.play(sec(9));
    CHECK_FALSE(t.ended());
    CHECK(t.position_s() == 0.0);
    t.update(sec(10));
    CHECK(t.position_s() == 1.0);
    t.seek(sec(10), 2); // seeking to the end while playing ends the song
    t.update(sec(11));
    CHECK(t.ended());
    t.seek(sec(11), 1); // seeking away clears the ended state
    CHECK_FALSE(t.ended());
}

TEST_CASE("transport: the ops record what the clock did, with wall and song times") {
    Transport t = ready(plain_song(100));
    t.set_speed_percent(0, 50);
    t.play(sec(10));
    t.update(sec(11));
    t.update(sec(12));                 // contiguous: merges into the same Move
    t.set_speed_percent(sec(12), 100); // a speed change splits the Move: one speed in each
    t.update(sec(13));
    t.seek(sec(13), 20);
    t.pause(sec(14));
    const std::vector<TransportOp> ops = drain(t);
    REQUIRE(ops.size() == 6);
    CHECK(ops[0].kind == Kind::Play);
    CHECK(ops[1].kind == Kind::Move);
    CHECK(ops[1].at_ns == sec(10));
    CHECK(ops[1].end_ns == sec(12));
    CHECK(ops[1].to_s == 1.0);
    CHECK(ops[1].speed_percent == 50);
    CHECK(ops[2].kind == Kind::Move);
    CHECK(ops[2].speed_percent == 100);
    CHECK(ops[2].from_s == 1.0);
    CHECK(ops[2].to_s == 2.0);
    CHECK(ops[3].kind == Kind::Seek);
    CHECK(ops[3].from_s == 2.0);
    CHECK(ops[3].to_s == 20.0);
    CHECK(ops[4].kind == Kind::Move);
    CHECK(ops[5].kind == Kind::Pause);
    CHECK(ops[5].to_s == 21.0);

    // A song change drops the ops not yet taken, counts them, and reports the reset.
    Transport u = ready(plain_song(10));
    u.play(0);
    u.update(sec(4)); // a Play and a Move still queued
    CHECK(u.set_song(plain_song(20)) == 2);
    const std::vector<TransportOp> reset = drain(u);
    REQUIRE(reset.size() == 2);
    CHECK(reset[0].kind == Kind::Pause);
    CHECK(reset[1].kind == Kind::Seek);
    CHECK(reset[1].to_s == 0.0);
    CHECK(u.position_s() == 0.0);
}

TEST_CASE("transport loop: the position wraps from B to A, exactly, however long a frame is") {
    Transport t = ready(plain_song(10));
    REQUIRE(t.set_loop(0, 2, 4));
    t.seek(0, 3.5);
    t.play(0);
    drain(t);
    t.update(sec(1)); // 0.5 s to B, then 0.5 s into the second pass
    CHECK(t.position_s() == 2.5);
    CHECK(t.loop_count() == 1);
    const std::vector<TransportOp> ops = drain(t);
    REQUIRE(ops.size() == 3);
    CHECK(ops[0].kind == Kind::Move);
    CHECK(ops[0].to_s == 4.0);
    CHECK(ops[1].kind == Kind::Wrap);
    CHECK(ops[1].from_s == 4.0);
    CHECK(ops[1].to_s == 2.0);
    CHECK(ops[1].at_ns == sec(0.5));
    CHECK(ops[2].from_s == 2.0);
    CHECK(ops[2].to_s == 2.5);

    // Landing exactly on B is A; a long stall wraps several times; the position is never at B.
    Transport exact = ready(plain_song(10));
    REQUIRE(exact.set_loop(0, 2, 4));
    exact.seek(0, 3.5);
    exact.play(0);
    exact.update(sec(0.5));
    CHECK(exact.position_s() == 2.0);
    Transport stall = ready(plain_song(10));
    REQUIRE(stall.set_loop(0, 2, 3));
    stall.seek(0, 2);
    stall.play(0);
    stall.update(sec(3.5));
    CHECK(stall.position_s() == 2.5);
    CHECK(stall.loop_count() == 3);
    Transport steps = ready(plain_song(10));
    REQUIRE(steps.set_loop(0, 2.2, 3.7));
    steps.seek(0, 2.2);
    steps.play(0);
    for (int i = 1; i <= 500; ++i) {
        steps.update(i * 7'000'000LL); // 7 ms steps, not a divisor of the loop
        REQUIRE(steps.position_s() >= 2.2);
        REQUIRE(steps.position_s() < 3.7);
    }
}

TEST_CASE("transport loop: a loop at the song's end wraps, one cleared or switched off lets the "
          "song run") {
    Transport end = ready(plain_song(4));
    REQUIRE(end.set_loop(0, 2, 4));
    end.seek(0, 3);
    end.play(0);
    end.update(sec(1.5));
    CHECK_FALSE(end.ended());
    CHECK(end.position_s() == 2.5);

    Transport t = ready(plain_song(10));
    REQUIRE(t.set_loop(0, 2, 4));
    t.seek(0, 6); // past B: plays on to the end
    t.play(0);
    t.update(sec(2));
    CHECK(t.position_s() == 8.0);
    t.seek(sec(2), 3);
    t.clear_loop(sec(2));
    t.update(sec(4));
    CHECK(t.position_s() == 5.0);
    REQUIRE(t.set_loop(sec(4), 2, 4));
    t.set_loop_enabled(sec(4), false);
    t.seek(sec(4), 3);
    t.update(sec(6));
    CHECK(t.position_s() == 5.0);
    t.set_loop_enabled(sec(6), true);
    t.seek(sec(6), 3);
    t.update(sec(7.5));
    CHECK(t.position_s() == 2.5);

    // set_loop swaps reversed times, clamps, and refuses a loop that is nearly empty.
    Transport s = ready(plain_song(10));
    CHECK(s.set_loop(0, 6, 2));
    CHECK(s.loop_start_s() == 2.0);
    CHECK(s.set_loop(0, -5, 99));
    CHECK(s.loop_end_s() == 10.0);
    CHECK_FALSE(s.set_loop(0, 3, 3.05));
    CHECK(s.loop_start_s() == 0.0);
}

TEST_CASE("transport loop: skips and scrubs inside, past and before the loop") {
    // A loop from 4 s to 8 s in a 20 s song. A skip moves by an amount and stays inside a loop it
    // starts in (past B wraps to A, before A stops at A); from outside, a move that lands outside
    // ends the loop. A scrub jumps: outside the loop, or exactly at B, ends it.
    struct Row {
        const char* what;
        bool scrub;
        double from, amount; // the position before; the skip amount or the scrub target
        double position;
        bool loop_on, returns;
    };
    const Row rows[] = {
        {"skip past B wraps to A", false, 6, 5, 4, true, false},
        {"skip inside lands there", false, 4, 3, 7, true, false},
        {"skip before A stops at A", false, 5, -5, 4, true, false},
        {"skip landing exactly on B wraps to A", false, 5, 3, 4, true, false},
        {"skip landing just short of B stays", false, 5, 2.9990234375, 7.9990234375, true, false},
        {"skip landing just past B wraps to A", false, 5, 3.0009765625, 4, true, false},
        {"skip from inside never ends it", false, 6, 30, 4, true, false},
        {"skip from after the loop, landing outside", false, 12, -2, 10, false, true},
        {"skip from before, jumping over it", false, 1, 10, 11, false, true},
        {"skip from before, landing in it", false, 1, 5, 6, true, false},
        {"scrub past B", true, 6, 12, 12, false, true},
        {"scrub before A", true, 6, 1, 1, false, true},
        {"scrub exactly at B", true, 6, 8, 8, false, true},
        {"scrub to A", true, 6, 4, 4, true, false},
        {"scrub inside", true, 6, 7.5, 7.5, true, false},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        Transport t = ready(plain_song(20));
        REQUIRE(t.set_loop(0, 4, 8));
        t.seek(0, row.from);
        const bool returned = row.scrub ? t.scrub(0, row.amount) : t.skip(0, row.amount);
        CHECK(t.position_s() == row.position);
        CHECK(t.loop_enabled() == row.loop_on);
        CHECK(returned == row.returns);
    }

    // A skip while playing keeps wrapping inside the loop; with no loop, skips follow the song.
    Transport playing = ready(plain_song(20));
    REQUIRE(playing.set_loop(0, 4, 8));
    playing.seek(0, 6);
    playing.play(0);
    playing.skip(0, 5);
    playing.update(sec(5)); // from 4 s: a pass is 4 s, so 5 s later it is a second into the next
    CHECK(playing.position_s() == 5.0);
    CHECK(playing.loop_count() == 1);
    Transport free = ready(plain_song(20));
    free.seek(0, 6);
    free.skip(0, 5);
    CHECK(free.position_s() == 11.0);
    free.skip(0, -30);
    CHECK(free.position_s() == 0.0);
    free.skip(0, 100);
    CHECK(free.position_s() == 20.0);
}

TEST_CASE("transport loop: bar ranges become times, through a pickup and a time signature change") {
    const Song s = bars_song(8); // bars are 2 s long
    CHECK(snap_to_measure(s, 4.9, SnapMode::Floor) == 4.0);
    CHECK(snap_to_measure(s, 4.0, SnapMode::Ceil) == 4.0);
    CHECK(snap_to_measure(s, 4.1, SnapMode::Ceil) == 6.0);
    CHECK(snap_to_measure(s, 5.0, SnapMode::Nearest) == 6.0); // halfway goes up
    CHECK(snap_to_measure(s, 15.5, SnapMode::Ceil) == s.duration_s);
    const LoopSpan span = measures_span(s, 3, 4);
    REQUIRE(span.ok);
    CHECK(span.start_s == 4.0);
    CHECK(span.end_s == 8.0);
    CHECK(measures_span(s, 7, 8).end_s == s.duration_s);
    CHECK_FALSE(measures_span(s, 4, 3).ok);
    CHECK_FALSE(measures_span(s, 3, 9).ok);
    CHECK_FALSE(measures_span(s, 0, 2).ok);

    // A one-beat pickup is bar 0 (0.5 s), then 2 s bars.
    const Song pickup = midi_song(Track()
                                      .time_sig(0, 1, 4)
                                      .on(0, 0, 67)
                                      .off(480, 0, 67)
                                      .time_sig(0, 4, 4)
                                      .on(0, 0, 60)
                                      .off(1920 * 2, 0, 60)
                                      .end());
    const LoopSpan zero = measures_span(pickup, 0, 1);
    REQUIRE(zero.ok);
    CHECK(zero.start_s == 0.0);
    CHECK(zero.end_s == 0.5 + 2.0);

    // 4/4, then 3/4 from bar 3 (tick 3840): bar 3 is 1.5 s long.
    const Song changed = midi_song(Track()
                                       .bpm(0, 120)
                                       .time_sig(0, 4, 4)
                                       .on(0, 0, 60)
                                       .off(3840, 0, 60)
                                       .time_sig(0, 3, 4)
                                       .on(0, 0, 62)
                                       .off(1440 * 2, 0, 62)
                                       .end());
    const LoopSpan three = measures_span(changed, 3, 3);
    REQUIRE(three.ok);
    CHECK(three.start_s == 4.0);
    CHECK(three.end_s == 5.5);
}

TEST_CASE("transport: a count-in waits its beats at the tempo where the song starts, then plays") {
    Transport t = ready(bars_song(4)); // 120 BPM: a beat is 0.5 s
    t.set_count_in_beats(4);
    t.play(sec(10));
    CHECK(t.counting_in());
    t.update(sec(11.9));
    CHECK(t.position_s() == 0.0);
    t.update(sec(12.5));
    CHECK(t.position_s() == doctest::Approx(0.5));
    CHECK_FALSE(t.counting_in());

    // At half speed each beat takes twice as long, and the beats already counted stay counted.
    Transport half = ready(bars_song(4));
    half.set_count_in_beats(4);
    half.play(0);
    half.update(sec(1)); // two beats done
    half.set_speed_percent(sec(1), 50);
    half.update(sec(2.5)); // two beats take 2 s at half speed
    CHECK(half.position_s() == 0.0);
    CHECK(half.count_in_beats_left() == doctest::Approx(0.5));
    half.update(sec(3.5));
    CHECK(half.position_s() == 0.25);

    // Pause or a seek cancels it, and a new play counts in again.
    Transport paused = ready(bars_song(4));
    paused.set_count_in_beats(4);
    paused.play(0);
    paused.update(sec(1));
    paused.pause(sec(1));
    CHECK_FALSE(paused.counting_in());
    paused.play(sec(5));
    CHECK(paused.count_in_beats_left() == 4.0);

    // 120 BPM in 4/4, then from bar 2 60 BPM in 3/4: starting at bar 2 counts one-second beats.
    Track tempo;
    tempo.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).time_sig(0, 3, 4);
    tempo.off(1440 * 2, 0, 60).end();
    const Song song = midi_song(tempo);
    REQUIRE(song.measures.size() == 3);
    Transport at_bar2 = ready(song);
    at_bar2.set_count_in_beats(6);
    at_bar2.seek(0, song.measures[1].start_s);
    at_bar2.play(0);
    at_bar2.update(sec(10)); // six beats took 6 s, then the song moved for 4 s
    CHECK(at_bar2.position_s() == doctest::Approx(song.measures[1].start_s + 4.0));
}

TEST_CASE("transport hold: the clock stops where a chord waits and resumes without catching up") {
    Transport t = ready(plain_song(100));
    t.play(0);
    t.hold(sec(1));
    CHECK(t.held());
    CHECK(t.playing()); // not a user pause
    t.update(sec(5));
    CHECK(t.position_s() == 1.0);
    t.release(sec(5));
    t.update(sec(6));
    CHECK(t.position_s() == 2.0);
    CHECK(only(drain(t), Kind::Pause).empty());

    // hold_at stops exactly at the song time, not a frame later, and is used up.
    Transport at = ready(plain_song(100));
    at.play(0);
    at.hold_at(0, 1.5);
    at.update(sec(3)); // would be 3.0 without the hold point
    CHECK(at.position_s() == 1.5);
    const std::vector<TransportOp> ops = drain(at);
    REQUIRE(only(ops, Kind::Hold).size() == 1);
    CHECK(only(ops, Kind::Hold)[0].at_ns == sec(1.5));
    at.release(sec(3));
    at.update(sec(5));
    CHECK(at.position_s() == 3.5);

    // A hold point that is not ahead is ignored, a seek forgets one, a loop end at the same time
    // wraps first, and a speed change keeps it exact.
    Transport edge = ready(plain_song(100));
    edge.play(0);
    edge.update(sec(2));
    edge.hold_at(sec(2), 2.0);
    CHECK_FALSE(edge.hold_point().has_value());
    edge.hold_at(sec(2), 4.0);
    edge.seek(sec(2), 3.0);
    edge.update(sec(3));
    CHECK_FALSE(edge.held());
    Transport wrapped = ready(plain_song(10));
    REQUIRE(wrapped.set_loop(0, 2, 4));
    wrapped.seek(0, 3);
    wrapped.play(0);
    wrapped.hold_at(0, 4);
    wrapped.update(sec(1.5));
    CHECK(wrapped.position_s() == 2.5);
    CHECK_FALSE(wrapped.held());
    Transport slow = ready(plain_song(100));
    slow.play(0);
    slow.hold_at(0, 2.0);
    slow.set_speed_percent(sec(1), 50);
    slow.update(sec(10));
    CHECK(slow.position_s() == 2.0);
    CHECK(only(drain(slow), Kind::Hold)[0].at_ns ==
          sec(3)); // 1 s at 100%, then 1 song second at 50%
}

TEST_CASE("playback: every note is handed over once, none missed, in frames of any length") {
    // Twenty notes of different pitch, a quarter second each. Frames of 8 ms to 120 ms (the last
    // is longer than the look-ahead, so its notes go late, not missing) drive the clock; each
    // frame's plan goes to Playback as the song controller does.
    Track track;
    for (int i = 0; i < 20; ++i) {
        track.on(0, 0, 40 + i).off(240, 0, 40 + i);
    }
    const Song song = midi_song(track.end());
    Transport clock = ready(song);
    Playback playback(song);
    std::vector<PlaybackMessage> sent;
    double cursor = 0.0;
    auto plan = [&](bool moving, double position, std::int64_t now_ns, std::int64_t window_ns) {
        LookAheadInput in;
        in.position_s = position;
        in.moving = moving;
        in.duration_s = song.duration_s;
        std::vector<TransportOp> ops;
        plan_look_ahead(in, now_ns, window_ns, cursor, ops);
        for (const TransportOp& op : ops) {
            playback.apply(op, sent);
        }
    };
    clock.play(0);
    std::int64_t now = 0;
    const std::int64_t frames[] = {8'000'000, 16'000'000, 33'000'000, 120'000'000, 5'000'000};
    for (int i = 0; !clock.ended() && i < 2000; ++i) {
        now += frames[i % 5];
        clock.update(now);
        for (const TransportOp& op : drain(clock)) {
            if (op.kind == Kind::End) {
                plan(false, op.to_s, op.at_ns, 0); // the notes before the end that are left
            }
            if (op.kind != Kind::Move && op.kind != Kind::Wrap) {
                playback.apply(op, sent);
            }
        }
        if (!clock.ended()) {
            plan(true, clock.position_s(), now, kSongSoundLeadNs);
        }
    }
    REQUIRE(clock.ended());
    // Each pitch is started once, when its note reaches the line, and stopped once, when it ends.
    for (int i = 0; i < 20; ++i) {
        std::vector<std::int64_t> on;
        std::vector<std::int64_t> off;
        for (const PlaybackMessage& m : sent) {
            if (m.data == 40 + i && m.kind == PlaybackMessage::Kind::NoteOn) {
                on.push_back(m.at_ns);
            }
            if (m.data == 40 + i && m.kind == PlaybackMessage::Kind::NoteOff) {
                off.push_back(m.at_ns);
            }
        }
        CAPTURE(i);
        REQUIRE(on.size() == 1);
        REQUIRE(off.size() == 1);
        CHECK(on[0] == doctest::Approx(0.25e9 * i).epsilon(1e-6));
        CHECK(off[0] == doctest::Approx(0.25e9 * (i + 1)).epsilon(1e-6));
    }
}

// ---- Queued notes: a cancel and a full queue never leave a note sounding

namespace {

constexpr std::int64_t kMs = 1'000'000;

PlaybackMessage message(PlaybackMessage::Kind kind, int key, std::int64_t due, int channel = 0) {
    PlaybackMessage m;
    m.kind = kind;
    m.channel = static_cast<std::uint8_t>(channel);
    m.data = static_cast<std::uint8_t>(key);
    m.value = kind == PlaybackMessage::Kind::NoteOn ? 80 : 0;
    m.at_ns = due;
    return m;
}
PlaybackMessage note_on(int key, std::int64_t due, int channel = 0) {
    return message(PlaybackMessage::Kind::NoteOn, key, due, channel);
}
PlaybackMessage note_off(int key, std::int64_t due, int channel = 0) {
    return message(PlaybackMessage::Kind::NoteOff, key, due, channel);
}

} // namespace

TEST_CASE("due queue: messages leave in due order, equal times as they came") {
    DueQueue<16> q;
    q.push(note_on(60, 30 * kMs));
    q.push(note_on(62, 10 * kMs));
    q.push(note_off(62, 10 * kMs));
    q.push(note_on(64, 20 * kMs));
    std::vector<int> keys;
    std::vector<PlaybackMessage::Kind> kinds;
    for (; !q.empty(); q.pop_front()) {
        keys.push_back(q.front().data);
        kinds.push_back(q.front().kind);
    }
    CHECK(keys == std::vector<int>{62, 62, 64, 60});
    CHECK(kinds[1] == PlaybackMessage::Kind::NoteOff); // after its own note-on
}

TEST_CASE("due queue: a cancel drops the notes not yet started with their ends, keeps those that did") {
    DueQueue<16> q;
    q.push(note_on(60, 100 * kMs - 1)); // started a nanosecond before the command
    q.push(note_on(64, 100 * kMs));     // due at the command: not started
    q.push(note_off(64, 125 * kMs));    // ends the one not started
    q.push(note_off(60, 130 * kMs));    // ends the one that started: stays, due now
    CHECK(q.cancel_unstarted(100 * kMs) == 2);
    REQUIRE(q.size() == 2);
    CHECK(q[0].data == 60);
    CHECK(q[1].kind == PlaybackMessage::Kind::NoteOff);
    CHECK(q[1].at_ns == 100 * kMs);
}

TEST_CASE("due queue: a full queue drops its oldest note-on, never a note-off") {
    DueQueue<4> q;
    q.push(note_off(40, 1 * kMs));
    q.push(note_on(60, 2 * kMs));
    q.push(note_on(62, 3 * kMs));
    q.push(note_off(41, 4 * kMs));
    CHECK(q.push(note_off(42, 5 * kMs))); // the oldest note-on (60) makes room
    CHECK(q.dropped_note_ons() == 1);
    CHECK(q.push(note_on(70, 6 * kMs)));
    int offs = 0;
    for (std::size_t i = 0; i < q.size(); ++i) {
        offs += ends_note(q[i]) ? 1 : 0;
    }
    CHECK(offs == 3);
    CHECK(q.lost_note_offs() == 0);
}

TEST_CASE("a piano whose port came back is reset first, so a held note cannot stay on") {
    PortReconnect watch;
    CHECK_FALSE(watch.reconnected("Piano")); // a first connection is not reset
    watch.lost("Piano");
    CHECK_FALSE(watch.reconnected("Other keyboard"));
    CHECK(watch.reconnected("Piano"));
    CHECK_FALSE(watch.reconnected("Piano")); // once per drop-out
    // The reset: all-notes-off, then pedal up, on every channel.
    const auto reset = port_reset_messages();
    REQUIRE(reset.size() == 32);
    std::bitset<16> notes_off;
    std::bitset<16> pedal_up;
    for (const PlaybackMessage& m : reset) {
        (m.data == 123 ? notes_off : pedal_up).set(m.channel);
        CHECK(m.kind == PlaybackMessage::Kind::ControlChange);
        CHECK(m.value == 0);
    }
    CHECK(notes_off.all());
    CHECK(pedal_up.all());
    CHECK(reset.front().data == 123);
}

// ---- The MIDI sender (a due time far ahead, so only stop and StopAll let a message out)

TEST_CASE("timed sender: stop sends what is queued at once, then nothing more leaves") {
    std::vector<PlaybackMessage> sent;
    app::TimedSender sender([&](const PlaybackMessage& m) { sent.push_back(m); });
    const auto far = std::chrono::steady_clock::now().time_since_epoch() + std::chrono::hours(1);
    SongSoundEvent event;
    event.kind = SongSoundEvent::Kind::Message;
    event.message = note_off(60, std::chrono::duration_cast<std::chrono::nanoseconds>(far).count());
    sender.submit(event);
    sender.stop(); // returns once the sender's thread has ended
    CHECK(sent.size() == 1);
    event.message = note_on(62, 0);
    sender.submit(event);
    sender.stop();
    CHECK(sent.size() == 1);
}

TEST_CASE("timed sender: StopAll forgets the queued note-ons and resets every channel") {
    std::vector<PlaybackMessage> sent;
    app::TimedSender sender([&](const PlaybackMessage& m) { sent.push_back(m); });
    const auto far = std::chrono::steady_clock::now().time_since_epoch() + std::chrono::hours(1);
    SongSoundEvent event;
    event.kind = SongSoundEvent::Kind::Message;
    event.message = note_on(60, std::chrono::duration_cast<std::chrono::nanoseconds>(far).count());
    sender.submit(event);
    event.kind = SongSoundEvent::Kind::StopAll;
    sender.submit(event);
    sender.stop();
    REQUIRE(sent.size() == 32); // the reset only: the note never left
    for (const PlaybackMessage& m : sent) {
        CHECK(m.kind == PlaybackMessage::Kind::ControlChange);
    }
}
