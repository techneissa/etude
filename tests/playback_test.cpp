// SPDX-License-Identifier: GPL-3.0-or-later
// playback: the first topics (split from playback_test.cpp).
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

namespace {

std::vector<PlaybackMessage> note_log(const std::vector<PlaybackMessage>& v) {
    std::vector<PlaybackMessage> notes;
    std::copy_if(v.begin(), v.end(), std::back_inserter(notes), [](const PlaybackMessage& m) {
        return m.kind == MsgKind::NoteOn || m.kind == MsgKind::NoteOff;
    });
    return notes;
}

} // namespace

// ---- Notes, timing and order --------------------------------------------------------------------

TEST_CASE("playback: notes are sent at their start and stopped at their end") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 60, 90).note(1.0, 2.0, 0, 64, 70)}));
    r.play(10.0);
    r.update(14.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 60@0", "off 60@0", "on 64@0", "off 64@0"});
    const std::vector<PlaybackMessage> log = note_log(r.piano.log);
    REQUIRE(log.size() == 4);
    CHECK(log[0].value == 90);
    CHECK(log[0].at_ns == sec(10.0));
    CHECK(log[1].at_ns == sec(11.0));
    CHECK(log[2].value == 70);
    CHECK(log[2].at_ns == sec(11.0));
    CHECK(log[3].at_ns == sec(12.0));
    CHECK(log[3].song_s == doctest::Approx(2.0));
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: the notes between two updates are sent once, whatever the update rate") {
    for (double step : {0.016, 0.1, 0.7, 5.0}) {
        Rig r(make_song(
            {AbsTrack("Right").note(0.0, 1.0, 0, 60).note(1.5, 2.5, 0, 62).note(3.0, 3.2, 0, 64)}));
        r.play(0.0);
        for (double t = step; t < 4.0 + step; t += step) {
            r.update(t);
        }
        CAPTURE(step);
        CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 3);
        CHECK(of_kind(r.piano.log, MsgKind::NoteOff).size() == 3);
        CHECK_FALSE(r.piano.ringing());
    }
}

TEST_CASE("playback: speed changes the timing of messages, not their content") {
    Rig r(make_song({AbsTrack("Right").note(1.0, 2.0, 0, 60)}));
    r.transport.set_speed_percent(0, 50);
    r.play(0.0);
    r.update(6.0);
    const std::vector<PlaybackMessage> log = note_log(r.piano.log);
    REQUIRE(log.size() == 2);
    CHECK(log[0].data == 60);
    CHECK(log[0].at_ns == sec(2.0)); // song second 1 at half speed
    CHECK(log[1].at_ns == sec(4.0));
}

TEST_CASE("playback: a speed change in the middle of a note keeps its times consistent") {
    Rig r(make_song({AbsTrack("Right").note(1.0, 3.0, 0, 60)}));
    r.play(0.0);
    r.update(2.0); // song 2.0: the note has started at wall 1.0
    r.transport.set_speed_percent(sec(2.0), 50);
    r.update(
        5.0); // 3 s wall at half speed = 1.5 song seconds: the note ends at song 3.0 = wall 4.0
    const std::vector<PlaybackMessage> log = note_log(r.piano.log);
    REQUIRE(log.size() == 2);
    CHECK(log[0].at_ns == sec(1.0));
    CHECK(log[1].at_ns == sec(4.0));
}

TEST_CASE("playback: at one time the note-off comes first, then events, then the note-on") {
    Rig r(make_song({AbsTrack("Right")
                         .note(0.0, 1.0, 0, 60)
                         .program(1.0, 0, 40)
                         .cc(1.0, 0, 7, 50)
                         .note(1.0, 2.0, 0, 60) // the same key struck again at the same instant
                         .note(0.5, 1.0, 0, 62)}));
    r.play(0.0);
    r.update(3.0);
    std::vector<std::string> d = describe(r.piano.log);
    auto pos = [&](const std::string& s, std::size_t from = 0) {
        return static_cast<std::size_t>(
            std::find(d.begin() + static_cast<std::ptrdiff_t>(from), d.end(), s) - d.begin());
    };
    std::size_t first_on = pos("on 60@0");
    std::size_t off = pos("off 60@0");
    std::size_t pc = pos("pc 40@0");
    std::size_t vol = pos("cc 7=50@0");
    std::size_t second_on = pos("on 60@0", first_on + 1);
    REQUIRE(second_on < d.size());
    CHECK(off < pc);
    CHECK(pc < vol); // file order inside one tick
    CHECK(vol < second_on);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: two overlapping notes on one key do not stop each other early") {
    // Two tracks both play C4 on channel 1: 0-2 s and 1-3 s.
    Rig r(make_song({AbsTrack("A").note(0.0, 2.0, 0, 60), AbsTrack("B").note(1.0, 3.0, 0, 60)}));
    r.play(0.0);
    r.update(2.5);
    CHECK(r.piano.key_down(0, 60)); // the second note is still sounding after the first one ended
    r.update(3.5);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: program changes and controllers from the file are sent in time") {
    Rig r(make_song({AbsTrack("Right")
                         .program(0.0, 0, 5)
                         .cc(0.0, 0, 0, 1)
                         .note(0.0, 4.0, 0, 60)
                         .cc(1.0, 0, 64, 127)
                         .bend(2.0, 0, 9000)
                         .program(3.0, 0, 73)}));
    r.play(0.0);
    // The state at the start is sent when play starts, before any Move.
    CHECK(r.piano.program(0) == 5);
    CHECK(r.piano.controller(0, 0) == 1);
    r.update(0.5);
    CHECK(r.piano.controller(0, 64) == 0);
    r.update(1.5);
    CHECK(r.piano.controller(0, 64) == 127);
    r.update(2.5);
    CHECK(r.piano.bend(0) == 9000);
    CHECK(r.piano.program(0) == 5);
    r.update(3.5);
    CHECK(r.piano.program(0) == 73);
}

// ---- Pause, resume, end ------------------------------------------------------------------------

TEST_CASE("playback: pause stops every sounding note, lifts the pedal and sends all-notes-off") {
    Rig r(make_song(
        {AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 4.0, 0, 60).note(0.5, 4.0, 0, 64),
         AbsTrack("Left").note(0.0, 4.0, 1, 36)}));
    r.play(0.0);
    r.update(1.0);
    REQUIRE(r.piano.key_down(0, 60));
    REQUIRE(r.piano.key_down(1, 36));
    r.pause(1.25);
    CHECK_FALSE(r.piano.ringing());
    CHECK(r.piano.controller(0, 64) == 0);
    std::vector<std::string> d = describe(r.last);
    CHECK(of_kind(r.last, MsgKind::NoteOff).size() == 3);
    CHECK(std::count(d.begin(), d.end(), "cc 123=0@0") == 1);
    CHECK(std::count(d.begin(), d.end(), "cc 123=0@1") == 1);
    CHECK(std::count(d.begin(), d.end(), "cc 64=0@0") == 1);
    for (const PlaybackMessage& m : r.last) {
        CHECK(m.at_ns == sec(1.25));
    }
    // The note-offs come before the all-notes-off.
    CHECK(d.front().rfind("off", 0) == 0);
    CHECK(d.back() == "cc 123=0@1");
}

TEST_CASE("playback: a second pause with nothing sounding sends nothing") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 60)}));
    r.play(0.0);
    r.update(0.5);
    r.pause(0.5);
    r.play(1.0);
    r.pause(1.0);
    CHECK(r.last.empty());
}

TEST_CASE("playback: resuming restores the pedal and carries on without restarting the note") {
    Rig r(make_song(
        {AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 4.0, 0, 60).note(2.0, 3.0, 0, 64)}));
    r.play(0.0);
    r.update(1.0);
    r.pause(1.0);
    REQUIRE_FALSE(r.piano.ringing());
    r.play(5.0);
    CHECK(r.piano.controller(0, 64) == 127); // pedal state of the file at this position
    CHECK_FALSE(r.piano.key_down(0, 60));    // the note in progress is not restarted
    r.update(6.5);
    CHECK(r.piano.key_down(0, 64)); // the next note plays on time
    r.update(9.0);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: the end of the song leaves nothing ringing, pedal included") {
    Rig r(make_song({AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 2.0, 0, 60)}));
    r.play(0.0);
    r.update(10.0);
    CHECK(r.transport.ended());
    CHECK_FALSE(r.piano.ringing());
    CHECK(r.piano.controller(0, 64) == 0);
}

// ---- Seek --------------------------------------------------------------------------------------

TEST_CASE("playback: seeking silences the piano and restores the state at the new position") {
    Rig r(make_song({AbsTrack("Right")
                         .program(0.0, 0, 40)
                         .cc(0.0, 0, 7, 90)
                         .note(0.0, 10.0, 0, 60)
                         .program(5.0, 0, 73)
                         .cc(5.0, 0, 7, 40)
                         .cc(6.0, 0, 64, 127)
                         .bend(6.5, 0, 12000)
                         .note(8.0, 9.0, 0, 67)}));
    r.play(0.0);
    r.update(7.0); // the piano now has program 73, volume 40, pedal down, bend 12000
    REQUIRE(r.piano.program(0) == 73);
    REQUIRE(r.piano.key_down(0, 60));

    r.seek(7.0, 2.0);
    CHECK_FALSE(r.piano.ringing()); // note 60 began before 2.0: not started again
    CHECK(r.piano.program(0) == 40);
    CHECK(r.piano.controller(0, 7) == 90);
    CHECK(r.piano.controller(0, 64) == 0);
    CHECK(r.piano.bend(0) == 8192); // the file's bend comes later: back to centre

    r.update(8.0); // 3.0 s in
    r.update(10.5);
    r.pause(10.5);
    CHECK_FALSE(r.piano.ringing());

    r.seek(11.0, 7.0);
    r.play(11.0);
    CHECK(r.piano.program(0) == 73);
    CHECK(r.piano.controller(0, 7) == 40);
    CHECK(r.piano.controller(0, 64) == 127);
    CHECK(r.piano.bend(0) == 12000);
}

TEST_CASE("playback: a seek while playing starts the notes at the target, not the ones before it") {
    Rig r(make_song(
        {AbsTrack("Right").note(0.0, 1.0, 0, 60).note(5.0, 6.0, 0, 62).note(5.0, 6.0, 0, 65)}));
    r.play(0.0);
    r.update(0.5);
    r.seek(0.5, 5.0); // exactly on two notes
    r.piano.log.clear();
    r.update(1.0);
    CHECK(describe(r.piano.log) == Strings{"on 62@0", "on 65@0"});
}

TEST_CASE("playback: the state is restored in order: bank select, program, controllers, bend") {
    Rig r(make_song({AbsTrack("Right")
                         .bend(0.5, 0, 9999)
                         .cc(0.5, 0, 7, 77)
                         .program(0.5, 0, 12)
                         .cc(0.5, 0, 32, 3)
                         .cc(0.5, 0, 0, 1)
                         .note(5.0, 6.0, 0, 60)}));
    r.play(0.0);
    r.update(0.1);
    r.seek(0.1, 3.0);
    // The piano has what the file sets; nothing is wasted on what it already has.
    r.pause(0.1);
    r.seek(0.2, 0.0);
    r.play(0.2);
    r.seek(0.3, 3.0);
    std::vector<std::string> d = describe(r.last);
    auto at = [&](const std::string& s) { return std::find(d.begin(), d.end(), s) - d.begin(); };
    REQUIRE(at("cc 0=1@0") < static_cast<std::ptrdiff_t>(d.size()));
    CHECK(at("cc 0=1@0") < at("pc 12@0"));
    CHECK(at("cc 32=3@0") < at("pc 12@0"));
    CHECK(at("pc 12@0") < at("cc 7=77@0"));
    CHECK(at("cc 7=77@0") < at("bend 9999@0"));
}

TEST_CASE("playback: only what the piano doesn't already have is sent") {
    Rig r(
        make_song({AbsTrack("Right").program(0.5, 0, 12).cc(0.5, 0, 7, 77).note(5.0, 6.0, 0, 60)}));
    r.play(0.0);
    r.seek(0.0, 3.0);
    CHECK_FALSE(r.last.empty());
    r.seek(0.1, 4.0);
    CHECK(r.last.empty()); // same state again: no program change, no volume
    r.seek(0.2, 0.0);      // back before the changes: defaults for what was set
    CHECK(describe(r.last) == Strings{"pc 0@0", "cc 7=100@0"});
}

TEST_CASE("playback: program changes at time 0 and mid-song") {
    Rig r(make_song({AbsTrack("Right")
                         .program(0.0, 2, 40)
                         .note(0.0, 1.0, 2, 60)
                         .program(3.0, 2, 73)
                         .note(3.0, 4.0, 2, 62)}));
    r.play(0.0);
    CHECK(r.piano.program(2) == 40); // set up before the first note
    CHECK(r.piano.log.size() == 1);
    r.update(0.001);
    CHECK(r.piano.key_down(2, 60));
    r.seek(0.001, 3.5);
    CHECK(r.piano.program(2) == 73);
    r.seek(0.002, 0.0);
    CHECK(r.piano.program(2) == 40);
    r.seek(0.003, 3.0); // the change sits exactly at the target
    CHECK(r.piano.program(2) == 73);
    r.update(0.5);
    CHECK(r.piano.key_down(2, 62));
    CHECK(of_kind(r.last, MsgKind::ProgramChange).empty()); // not sent twice
}

TEST_CASE("playback: a seek while paused sends nothing until play") {
    Rig r(make_song(
        {AbsTrack("Right").program(0.0, 0, 40).program(2.0, 0, 73).note(3.0, 4.0, 0, 60)}));
    r.seek(0.0, 2.5);
    CHECK(r.last.empty());
    CHECK(r.piano.log.empty());
    r.play(1.0);
    CHECK(r.piano.program(0) == 73);
}
