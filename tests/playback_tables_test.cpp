// SPDX-License-Identifier: GPL-3.0-or-later
// playback: tables (split from playback_test.cpp).
#include "etude/metronome.hpp"
#include "etude/playback.hpp"
#include "timing_factor.hpp"
#include "etude/evaluator.hpp"
#include "etude/keyboard.hpp"
#include "etude/midi_file.hpp"
#include "etude/note_window.hpp"
#include "etude/transport.hpp"
#include "etude/transpose.hpp"
#include "smf_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <limits>
#include <random>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "playback_test_support.hpp"

using namespace etude;
using namespace etude::test;
using MsgKind = PlaybackMessage::Kind;
using OpKind = TransportOp::Kind;
using namespace playback_test_support;

// ---- Tables made ahead (Playback::prepare), shared by two playbacks -----------------------------

namespace {

Song tabled_song(int velocity = 90, int pedal_value = 127) {
    return make_song({AbsTrack("Right")
                          .program(0.0, 0, 5)
                          .note(0.0, 1.0, 0, 60, velocity)
                          .cc(0.5, 0, 64, pedal_value)
                          .note(1.0, 2.5, 0, 64, 70)
                          .bend(1.5, 0, 9000)
                          .cc(2.0, 0, 64, 0),
                      AbsTrack("Left").note(0.5, 3.0, 1, 48, 80).note(3.0, 3.5, 1, 43, 80)});
}

// Plays the song through a transport into `playback`, with a seek in the middle, and returns every
// message it sent.
std::vector<PlaybackMessage> play_through(Playback& playback, const Song& song) {
    Transport transport;
    transport.set_song(song);
    std::vector<PlaybackMessage> all;
    const auto pump = [&] {
        std::vector<TransportOp> ops;
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            playback.apply(op, all);
        }
    };
    pump();
    transport.play(sec(1.0));
    pump();
    for (double now = 1.25; now < 3.0; now += 0.25) {
        transport.update(sec(now));
        pump();
    }
    transport.seek(sec(3.0), 0.75);
    pump();
    for (double now = 3.25; now < 7.0; now += 0.25) {
        transport.update(sec(now));
        pump();
    }
    return all;
}

bool same_messages(const std::vector<PlaybackMessage>& a, const std::vector<PlaybackMessage>& b) {
    if (a.size() != b.size()) {
        return false;
    }
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (a[i].kind != b[i].kind || a[i].channel != b[i].channel || a[i].data != b[i].data ||
            a[i].value != b[i].value || a[i].at_ns != b[i].at_ns || a[i].song_s != b[i].song_s) {
            return false;
        }
    }
    return true;
}

} // namespace

TEST_CASE("playback: tables made ahead on another thread play exactly like a plain load, shared by two") {
    const Song song = tabled_song();
    std::vector<PlaybackMessage> sink;
    Playback plain;
    plain.set_song(song, 0, sink);
    const std::vector<PlaybackMessage> expected = play_through(plain, song);
    CHECK(expected.size() > 8);

    Playback::Prepared prepared;
    std::thread maker([&] { prepared = Playback::prepare(song); });
    maker.join();
    REQUIRE(prepared.valid());
    CHECK(prepared.note_count() == song.notes.size());
    const std::uint64_t fingerprint = song_fingerprint(song);
    Playback first;
    Playback second; // the audition's, holding the same tables
    first.set_song(song, 0, sink, prepared, fingerprint);
    second.set_song(song, 0, sink, prepared, fingerprint);
    Playback copy = first; // a Playback copied before it plays shares them too
    CHECK(same_messages(play_through(first, song), expected));
    CHECK(same_messages(play_through(second, song), expected));
    CHECK(same_messages(play_through(copy, song), expected));
}

TEST_CASE("playback: tables of another song are not used, however alike (velocity or a pedal event)") {
    const Song song = tabled_song();
    std::vector<PlaybackMessage> sink;
    Playback plain;
    plain.set_song(song, 0, sink);
    const std::vector<PlaybackMessage> expected = play_through(plain, song);

    // Same notes, bars and number of events; one note a different velocity, or one pedal value.
    for (const Song& other : {tabled_song(60), tabled_song(90, 100)}) {
        REQUIRE(other.notes.size() == song.notes.size());
        REQUIRE(other.events.size() == song.events.size());
        CHECK(song_fingerprint(other) != song_fingerprint(song));
        Playback taken;
        taken.set_song(song, 0, sink, Playback::prepare(other), song_fingerprint(song));
        CHECK(same_messages(play_through(taken, song), expected));
    }
    // An empty Prepared is just ignored.
    Playback none;
    none.set_song(song, 0, sink, Playback::Prepared{}, song_fingerprint(song));
    CHECK(same_messages(play_through(none, song), expected));
}

TEST_CASE("playback: dropping the song silences what sounds and leaves nothing to play") {
    const Song song = tabled_song();
    Playback playback(song);
    Transport transport;
    transport.set_song(song);
    std::vector<PlaybackMessage> out;
    transport.play(sec(1.0));
    std::vector<TransportOp> ops;
    transport.take_ops(ops);
    for (const TransportOp& op : ops) {
        playback.apply(op, out);
    }
    transport.update(sec(1.75));
    transport.take_ops(ops);
    for (const TransportOp& op : ops) {
        playback.apply(op, out);
    }
    REQUIRE(playback.sounding_count() > 0);
    out.clear();
    playback.drop_song(sec(1.75), out);
    CHECK(playback.sounding_count() == 0);
    CHECK_FALSE(playback.needs_silence());
    CHECK_FALSE(of_kind(out, MsgKind::NoteOff).empty());
    // It holds no notes now: a transport over an empty song sends nothing.
    Transport empty;
    empty.set_song(Song{});
    empty.play(sec(2.0));
    empty.update(sec(3.0));
    ops.clear();
    empty.take_ops(ops);
    out.clear();
    for (const TransportOp& op : ops) {
        playback.apply(op, out);
    }
    CHECK(of_kind(out, MsgKind::NoteOn).empty());
}
