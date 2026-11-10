// SPDX-License-Identifier: GPL-3.0-or-later
// playback: loop (split from playback_test.cpp).
#include "etude/metronome.hpp"
#include "etude/playback.hpp"
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

// ---- Loop --------------------------------------------------------------------------------------

TEST_CASE("playback: a loop wrap stops what B left sounding and restores the state at A") {
    Rig r(make_song({AbsTrack("Right")
                         .cc(0.0, 0, 64, 127)
                         .cc(2.5, 0, 64, 0)
                         .program(3.0, 0, 73)
                         .note(0.0, 10.0, 0, 48) // spans the whole loop
                         .note(2.0, 2.5, 0, 60)
                         .note(3.5, 4.5, 0, 62) // crosses B
                         .note(4.0, 5.0, 0, 64)}));
    REQUIRE(r.transport.set_loop(0, 2.0, 4.0));
    r.transport.seek(0, 2.0);
    r.play(0.0);
    r.update(1.75); // song 3.75: the note at 3.5 sounds, the program is 73, the pedal is up
    REQUIRE(r.piano.key_down(0, 62));
    REQUIRE(r.piano.program(0) == 73);

    r.update(2.25); // wraps at song 4.0 (wall 2.0), then 0.25 s into the second pass
    CHECK(r.transport.loop_count() == 1);
    CHECK_FALSE(r.piano.key_down(0, 62));
    CHECK_FALSE(r.piano.key_down(0, 48));    // started before A: not retriggered
    CHECK(r.piano.program(0) == 0);          // back to the program the file has at A
    CHECK(r.piano.controller(0, 64) == 127); // the pedal state at A
    CHECK(r.piano.key_down(0, 60));          // starts exactly at A, so it plays on every pass
    r.update(5.0);
    r.pause(5.0);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: every wrap of a tight loop leaves the piano clean, at any update rate") {
    for (double step : {0.016, 0.3, 1.1}) {
        Rig r(make_song(
            {AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 20.0, 0, 48).note(1.0, 2.2, 0, 60)}));
        REQUIRE(r.transport.set_loop(0, 1.0, 2.0));
        r.transport.seek(0, 1.0);
        r.play(0.0);
        int wraps_seen = 0;
        for (double t = step; t < 12.0; t += step) {
            r.update(t);
            wraps_seen = r.transport.loop_count();
            // After any update, the only thing that can ring is the loop's own note.
            for (int k = 0; k < 128; ++k) {
                if (k != 60) {
                    CHECK_FALSE(r.piano.key_down(0, k));
                }
            }
        }
        CAPTURE(step);
        CHECK(wraps_seen >= 5);
        r.pause(12.0);
        CHECK_FALSE(r.piano.ringing());
    }
}

// ---- Hold --------------------------------------------------------------------------------------

TEST_CASE("playback: a hold sends nothing, and notes sounding stay sounding") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 4.0, 0, 60).note(2.0, 3.0, 0, 64)}));
    r.play(0.0);
    r.transport.hold_at(0, 1.0);
    r.update(3.0);
    REQUIRE(r.transport.held());
    CHECK(r.piano.key_down(0, 60));
    std::size_t before = r.piano.log.size();
    r.update(9.0);
    CHECK(r.piano.log.size() == before);
    r.transport.release(sec(9.0));
    r.pump();
    CHECK(r.last.empty());
    r.update(10.5); // song 2.5
    CHECK(r.piano.key_down(0, 64));
    r.pause(10.5);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: a pause during a hold still silences the piano") {
    Rig r(make_song({AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 4.0, 0, 60)}));
    r.play(0.0);
    r.update(1.0);
    r.transport.hold(sec(1.0));
    r.pump();
    REQUIRE(r.piano.key_down(0, 60));
    r.pause(2.0);
    CHECK_FALSE(r.piano.ringing());
}

// ---- Mute --------------------------------------------------------------------------------------

namespace {

Song two_hands() {
    return make_song(
        {AbsTrack("Right Hand").note(0.0, 1.0, 0, 72).note(2.0, 3.0, 0, 74).note(4.0, 5.0, 0, 76),
         AbsTrack("Left Hand").note(0.0, 2.0, 1, 48).note(2.0, 4.0, 1, 50)});
}

// two_hands() with the left hand's track a backing part (channel 1): its notes carry the flag.
Song with_backing() {
    Song s = two_hands();
    for (Note& n : s.notes) {
        n.accompaniment = n.channel == 1;
    }
    for (Part& p : s.parts) {
        p.accompaniment = p.channel == 1;
    }
    return s;
}

std::size_t part_index_of(const Song& s, int channel) {
    for (std::size_t i = 0; i < s.parts.size(); ++i) {
        if (s.parts[i].channel == channel) {
            return i;
        }
    }
    return s.parts.size();
}

} // namespace

TEST_CASE("playback: one accompaniment part is muted on its own, whatever the hands say") {
    const Song song = with_backing();
    const std::size_t part = part_index_of(song, 1);
    REQUIRE(part < song.parts.size());
    {
        Rig r(song);
        r.playback.set_part_muted(part, true, 0, r.last);
        r.play(0.0);
        r.update(10.0);
        CHECK(r.playback.part_muted(part));
        // Only the melody sounds; the hand mutes were never involved.
        for (const PlaybackMessage& m : of_kind(r.piano.log, MsgKind::NoteOn)) {
            CHECK(m.channel == 0);
        }
        CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 3);
    }
    {
        // Not muted: the backing part sounds, even with the piano's hands muted.
        Rig r(song);
        r.playback.set_hand_muted(Hand::Right, true, 0, r.last);
        r.playback.set_hand_muted(Hand::Left, true, 0, r.last);
        r.play(0.0);
        r.update(10.0);
        CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 2); // the left track's two notes
    }
    {
        // Unmuted again: back to all of it. A part that does not exist is ignored.
        Rig r(song);
        r.playback.set_part_muted(part, true, 0, r.last);
        r.playback.set_part_muted(part, false, 0, r.last);
        r.playback.set_part_muted(999, true, 0, r.last);
        CHECK_FALSE(r.playback.part_muted(999));
        r.play(0.0);
        r.update(10.0);
        CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 5);
    }
}

TEST_CASE("playback: muting an accompaniment part mid-note stops it at once") {
    const Song song = with_backing();
    Rig r(song);
    r.play(0.0);
    r.update(0.5);
    REQUIRE(r.piano.key_down(1, 48));
    std::vector<PlaybackMessage> out;
    r.playback.set_part_muted(part_index_of(song, 1), true, sec(0.5), out);
    r.piano.feed(out);
    CHECK(describe(out) == Strings{"off 48@1"});
    CHECK_FALSE(r.piano.key_down(1, 48));
    CHECK(r.piano.key_down(0, 72)); // the melody goes on
    r.update(10.0);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: the accompaniment volume scales the velocity of its notes only") {
    const Song song = with_backing();
    Rig r(song);
    std::vector<PlaybackMessage> out;
    r.playback.set_accompaniment_volume(50, 0, out);
    CHECK(r.playback.accompaniment_volume() == 50);
    r.play(0.0);
    r.update(10.0);
    for (const PlaybackMessage& m : of_kind(r.piano.log, MsgKind::NoteOn)) {
        CHECK(m.value == (m.channel == 1 ? 50 : 100));
    }
    // 0 sends none of them, and a sounding one stops.
    Rig quiet(song);
    quiet.play(0.0);
    quiet.update(0.5);
    REQUIRE(quiet.piano.key_down(1, 48));
    std::vector<PlaybackMessage> stop;
    quiet.playback.set_accompaniment_volume(0, sec(0.5), stop);
    quiet.piano.feed(stop);
    CHECK_FALSE(quiet.piano.key_down(1, 48));
    quiet.update(10.0);
    for (const PlaybackMessage& m : of_kind(quiet.piano.log, MsgKind::NoteOn)) {
        CHECK((m.channel == 0 || m.at_ns < sec(0.6)));
    }
    // Out of range is kept in range.
    quiet.playback.set_accompaniment_volume(400, 0, stop);
    CHECK(quiet.playback.accompaniment_volume() == 100);
}

TEST_CASE("playback: a new song starts with no part muted") {
    const Song song = with_backing();
    Rig r(song);
    r.playback.set_part_muted(part_index_of(song, 1), true, 0, r.last);
    r.playback.set_song(song, 0, r.last);
    CHECK_FALSE(r.playback.part_muted(part_index_of(song, 1)));
}

TEST_CASE("playback: a muted hand is not sent, and the piano plays the other hand") {
    Rig r(two_hands());
    r.playback.set_hand_muted(Hand::Right, true, 0, r.last);
    r.play(0.0);
    r.update(10.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 48@1", "off 48@1", "on 50@1", "off 50@1"});
    CHECK(r.playback.hand_muted(Hand::Right));
    CHECK_FALSE(r.playback.hand_muted(Hand::Left));

    Rig left_muted(two_hands());
    left_muted.playback.set_hand_muted(Hand::Left, true, 0, left_muted.last);
    left_muted.play(0.0);
    left_muted.update(10.0);
    CHECK(of_kind(left_muted.piano.log, MsgKind::NoteOn).size() == 3);
    for (const PlaybackMessage& m : of_kind(left_muted.piano.log, MsgKind::NoteOn)) {
        CHECK(m.channel == 0);
    }
}

TEST_CASE("playback: muting a hand mid-note stops its sounding notes at once") {
    Rig r(two_hands());
    r.play(0.0);
    r.update(0.5);
    REQUIRE(r.piano.key_down(0, 72));
    REQUIRE(r.piano.key_down(1, 48));
    std::vector<PlaybackMessage> out;
    r.playback.set_hand_muted(Hand::Right, true, sec(0.5), out);
    r.piano.feed(out);
    CHECK(describe(out) == Strings{"off 72@0"});
    CHECK(out[0].at_ns == sec(0.5));
    CHECK_FALSE(r.piano.key_down(0, 72));
    CHECK(r.piano.key_down(1, 48)); // the other hand keeps playing
    r.update(0.9);
    r.update(10.0);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: unmuting in the middle of a note starts nothing and leaves nothing stuck") {
    Rig r(two_hands());
    std::vector<PlaybackMessage> out;
    r.playback.set_hand_muted(Hand::Right, true, 0, out);
    r.play(0.0);
    r.update(0.5);
    r.playback.set_hand_muted(Hand::Right, false, sec(0.5), out);
    CHECK(out.empty());
    r.update(1.5); // the muted note's note-off comes by: nothing to stop
    CHECK_FALSE(r.piano.key_down(0, 72));
    r.update(2.5); // the next right-hand note plays
    CHECK(r.piano.key_down(0, 74));
    r.update(10.0);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: muting is kept across seeks, loops and pauses") {
    Rig r(two_hands());
    std::vector<PlaybackMessage> out;
    r.playback.set_hand_muted(Hand::Left, true, 0, out);
    r.play(0.0);
    r.seek(0.0, 2.0);
    r.update(1.0);
    r.pause(1.0);
    for (const PlaybackMessage& m : r.piano.log) {
        if (m.kind == MsgKind::NoteOn) {
            CHECK(m.channel == 0);
        }
    }
}

TEST_CASE("playback: drums can be muted on their own") {
    Rig r(make_song(
        {AbsTrack("Right").note(0.0, 1.0, 0, 60), AbsTrack("Drums").note(0.0, 0.5, 9, 36)}));
    std::vector<PlaybackMessage> out;
    r.playback.set_drums_muted(true, 0, out);
    r.play(0.0);
    r.update(2.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 60@0", "off 60@0"});
    CHECK(r.playback.drums_muted());
}

// ---- Panic and song change ----------------------------------------------------------------------

TEST_CASE("playback: silence() stops everything for quit or a lost connection") {
    Rig r(make_song({AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 4.0, 0, 60)}));
    r.play(0.0);
    r.update(1.0);
    std::vector<PlaybackMessage> out;
    r.playback.silence(sec(1.0), out);
    r.piano.feed(out);
    CHECK_FALSE(r.piano.ringing());
    CHECK(r.playback.sounding_count() == 0);
    CHECK(r.piano.controller(0, 64) == 0);
}

TEST_CASE("playback: loading another song stops the old one first") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 4.0, 0, 60)}));
    r.play(0.0);
    r.update(1.0);
    std::vector<PlaybackMessage> out;
    r.playback.set_song(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 70)}), sec(1.0), out);
    r.piano.feed(out);
    CHECK_FALSE(r.piano.ringing());
    CHECK_FALSE(r.playback.running());
}
