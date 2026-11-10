// SPDX-License-Identifier: GPL-3.0-or-later
// playback: transposition (split from playback_test.cpp).
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

// ---- Transposition ------------------------------------------------------------------------------

namespace {

// Sets the transposition the way the app will, and lets the piano hear what came out.
void transpose(Rig& r, const Transposition& t, double now_s) {
    std::vector<PlaybackMessage> out;
    r.playback.set_transposition(t, sec(now_s), out);
    r.last = out;
    r.piano.feed(out);
}

} // namespace

TEST_CASE("playback transposition: the piano gets the shifted pitch, speed never changes it") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 60).note(1.0, 2.0, 0, 64)}));
    transpose(r, Transposition(5, kRange61), 0.0);
    r.play(0.0);
    r.update(3.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 65@0", "off 65@0", "on 69@0", "off 69@0"});
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback transposition: a note folded onto the keyboard plays folded") {
    // 94 + 5 = 99 is above a 61-key keyboard (top key 96); the display folds it to 87.
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 94)}));
    transpose(r, Transposition(5, kRange61), 0.0);
    r.play(0.0);
    r.update(2.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 87@0", "off 87@0"});
}

TEST_CASE("playback transposition: a note no key can play is not sent, and nothing sticks") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 66).note(1.0, 2.0, 0, 62)}));
    transpose(r, Transposition(0, KeyRange{60, 64}), 0.0); // no F# between C4 and E4
    r.play(0.0);
    r.update(3.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 62@0", "off 62@0"});
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback transposition: drum notes and metronome clicks keep their numbers") {
    Rig r(make_song({AbsTrack("Drums").note(0.0, 0.5, 9, 38)}));
    ClickSound click;
    click.enabled = true;
    r.playback.set_click_sound(click);
    transpose(r, Transposition(7, kRange61), 0.0);
    r.play(0.0);
    r.update(1.0);
    TransportOp accent;
    accent.kind = TransportOp::Kind::Click;
    accent.accent = true;
    accent.beat = 1;
    r.last.clear();
    r.playback.apply(accent, r.last);
    r.piano.feed(r.last);
    const Strings d = describe(r.piano.log);
    CHECK(std::count(d.begin(), d.end(), "on 38@9") == 1);
    CHECK(std::count(d.begin(), d.end(), "off 38@9") == 1);
    CHECK(std::count(d.begin(), d.end(), "on 76@9") == 1); // the accented click
}

TEST_CASE("playback transposition: notes folded onto one key do not stop each other early") {
    // On a one-octave keyboard (C4 to B4) the song's C5 folds down onto the C4 the other track
    // plays.
    Rig r(make_song({AbsTrack("A").note(0.0, 2.0, 0, 60), AbsTrack("B").note(1.0, 3.0, 0, 72)}));
    transpose(r, Transposition(0, KeyRange{60, 71}), 0.0);
    r.play(0.0);
    r.update(2.5);
    CHECK(r.piano.key_down(0, 60)); // the second note still sounds after the first one ended
    r.update(3.5);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback transposition: changing it mid-note stops the note at its old pitch") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 4.0, 0, 60).note(2.0, 3.0, 0, 62)}));
    transpose(r, Transposition(2, kRange61), 0.0);
    r.play(0.0);
    r.update(1.0);
    REQUIRE(r.piano.key_down(0, 62)); // 60 + 2
    transpose(r, Transposition(-3, kRange61), 1.0);
    // The panic: the old key goes off at once, with all-notes-off after it, none left ringing.
    CHECK(describe(r.last) == Strings{"off 62@0", "cc 123=0@0"});
    for (const PlaybackMessage& m : r.last) {
        CHECK(m.at_ns == sec(1.0));
    }
    CHECK_FALSE(r.piano.ringing());
    CHECK(r.playback.sounding_count() == 0);
    // The note in progress is not restarted; the next one comes at the new pitch.
    r.update(2.5);
    CHECK(r.piano.key_down(0, 59)); // 62 - 3
    r.update(5.0);
    CHECK_FALSE(r.piano.ringing());
    const Strings d = note_messages(r.piano.log);
    CHECK(std::count(d.begin(), d.end(), "on 59@0") == 1);
    CHECK(std::count(d.begin(), d.end(), "off 59@0") == 1);
    CHECK(std::count(d.begin(), d.end(), "off 62@0") == 1); // the stuck-note check: one off per on
}

TEST_CASE("playback transposition: the pedal is down again after the change, as the file has it") {
    Rig r(make_song({AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.0, 4.0, 0, 60)}));
    r.play(0.0);
    r.update(1.0);
    REQUIRE(r.piano.controller(0, 64) == 127);
    transpose(r, Transposition(1, kRange61), 1.0);
    CHECK(r.piano.controller(0, 64) == 127);
    CHECK_FALSE(r.piano.key_down(0, 60));
    CHECK_FALSE(r.piano.key_down(0, 61));
}

TEST_CASE("playback transposition: the same transposition again sends nothing") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 4.0, 0, 60)}));
    transpose(r, Transposition(2, kRange61), 0.0);
    r.play(0.0);
    r.update(1.0);
    transpose(r, Transposition(2, kRange61), 1.0);
    CHECK(r.last.empty());
    CHECK(r.piano.key_down(0, 62));
    transpose(r, Transposition(14, kRange61), 1.1); // clamps to 12
    CHECK_FALSE(r.last.empty());
    transpose(r, Transposition(12, kRange61), 1.2); // the same after the clamp
    CHECK(r.last.empty());
}

TEST_CASE("playback transposition: a change while paused sends nothing and applies on play") {
    Rig r(make_song({AbsTrack("Right").note(0.0, 1.0, 0, 60)}));
    transpose(r, Transposition(4, kRange61), 0.0);
    CHECK(r.last.empty());
    r.play(0.0);
    r.update(2.0);
    CHECK(note_messages(r.piano.log) == Strings{"on 64@0", "off 64@0"});
}

TEST_CASE("playback transposition: the shown, played and expected pitch are always the same") {
    std::mt19937 rng(77);
    const KeyRange ranges[] = {kRange61,         kRange88,         kRange49,        kRange76,
                               KeyRange{60, 64}, KeyRange{48, 59}, KeyRange{0, 127}};
    for (int round = 0; round < 60; ++round) {
        // Notes with distinct start times 50 ms apart, any pitch, some on the drum channel.
        Song song;
        std::uniform_int_distribution<int> pitch(0, 127);
        std::uniform_int_distribution<int> channel(0, 15);
        const int count = 40;
        for (int i = 0; i < count; ++i) {
            Note n;
            n.start_s = i * 0.05;
            n.duration_s = 0.3;
            n.start_tick = i * 48;
            n.end_tick = n.start_tick + 288;
            n.pitch = static_cast<std::uint8_t>(pitch(rng));
            n.channel = static_cast<std::uint8_t>(channel(rng));
            n.drum = n.channel == 9;
            n.hand = Hand::Right;
            song.notes.push_back(n);
        }
        song.duration_s = count * 0.05 + 0.5;
        song.end_tick = song.notes.back().end_tick;

        std::uniform_int_distribution<int> shift(-14, 14); // past 12 to include the clamp
        const Transposition t(shift(rng), ranges[round % 7]);

        Rig r(song);
        transpose(r, t, 0.0);
        r.play(0.0);
        r.update(song.duration_s + 1.0);

        NoteWindow window(song);
        NoteWindowOptions options;
        options.ahead_s = 1000.0;
        options.include_drums = true;
        options.transposition = t;
        std::vector<VisibleNote> shown;
        window.query(0.0, options, shown);
        REQUIRE(shown.size() == song.notes.size());

        // The pitch each song note was played at, found by its start time.
        std::vector<int> played(song.notes.size(), -1);
        for (const PlaybackMessage& m : r.piano.log) {
            if (m.kind == MsgKind::NoteOn) {
                const auto i = static_cast<std::size_t>(std::llround(m.song_s / 0.05));
                REQUIRE(i < played.size());
                played[i] = m.data;
            }
        }
        for (const VisibleNote& v : shown) {
            const Note& n = song.notes[v.index];
            const TransposedPitch expected = t.expected_pitch(n);
            CAPTURE(round);
            CAPTURE(v.index);
            CAPTURE(int{n.pitch});
            CAPTURE(t.semitones());
            CHECK(v.pitch == expected.pitch);
            CHECK(v.folded == expected.folded);
            CHECK(v.fold_octaves == expected.fold_octaves);
            CHECK(v.playable == expected.playable);
            CHECK(played[v.index] == expected.pitch); // -1 (not sent) exactly when unplayable
            // And the answer is musically right, whatever the three paths agree on.
            if (n.drum) {
                CHECK(expected.pitch == n.pitch);
            } else if (expected.playable) {
                CHECK(t.keyboard().contains(expected.pitch));
                CHECK((expected.pitch - (n.pitch + t.semitones())) % 12 == 0);
            }
        }
        CHECK_FALSE(r.piano.ringing());
    }
}

// ---- Playback edge cases: pedal on pause, silence before teardown, restoring state at a position,
// and a transposition shared by display, playback and both evaluators -------------------------

namespace {

Song hand_song(const std::vector<std::pair<double, double>>& starts_and_lengths, int pitch = 60) {
    Song song;
    for (const auto& [start, length] : starts_and_lengths) {
        Note n;
        n.start_s = start;
        n.duration_s = length;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.hand = Hand::Right;
        song.notes.push_back(n);
    }
    song.duration_s = 5.0;
    return song;
}

} // namespace

TEST_CASE(
    "playback: a zero- or negative-length note still stops, it never sounds until pause (F6)") {
    for (double length : {0.0, -0.5, std::numeric_limits<double>::quiet_NaN(),
                          std::numeric_limits<double>::infinity()}) {
        CAPTURE(length);
        Rig r(hand_song({{1.0, length}}));
        r.play(0.0);
        r.update(2.0);
        CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 1);
        const std::vector<PlaybackMessage> offs = of_kind(r.piano.log, MsgKind::NoteOff);
        REQUIRE(offs.size() == 1);
        CHECK(offs[0].at_ns <= sec(1.1));
        CHECK(r.playback.sounding_count() == 0);
        CHECK_FALSE(r.piano.ringing());
    }
    // A note with no start time can't be scheduled: it is not sent at all.
    Rig r(hand_song({{std::numeric_limits<double>::quiet_NaN(), 0.5}, {1.0, 0.5}}));
    r.play(0.0);
    r.update(3.0);
    CHECK(of_kind(r.piano.log, MsgKind::NoteOn).size() == 1);
    CHECK_FALSE(r.piano.ringing());
}

TEST_CASE("playback: pause lifts the pedal on a channel that never played a note (F7)") {
    Rig r(make_song(
        {AbsTrack("Right").note(0.0, 1.5, 0, 60), AbsTrack("Pedal").cc(0.0, 1, 64, 127)}));
    r.play(0.0);
    r.update(1.0);
    REQUIRE(r.piano.controller(1, 64) == 127);
    r.pause(1.25);
    CHECK(r.piano.controller(1, 64) == 0);
    CHECK_FALSE(r.piano.ringing());
    // A song change and silence() lift it too.
    r.play(2.0);
    REQUIRE(r.piano.controller(1, 64) == 127);
    std::vector<PlaybackMessage> out;
    r.playback.silence(sec(2.5), out);
    r.piano.feed(out);
    CHECK(r.piano.controller(1, 64) == 0);
}

namespace {

int g_teardown_calls = 0;

} // namespace

TEST_CASE("playback: needs_silence says when silence() must come before teardown (F8)") {
    Song song = make_song({AbsTrack("Right").cc(0.0, 0, 64, 127).note(0.5, 4.0, 0, 60)});
    g_teardown_calls = 0;
    {
        Rig r(song);
        r.playback.set_teardown_check([](const Playback&) { ++g_teardown_calls; });
        CHECK_FALSE(r.playback.needs_silence());
        r.play(0.0);
        CHECK(r.playback.needs_silence()); // the pedal is down
        r.update(1.0);
        CHECK(r.playback.needs_silence());
        std::vector<PlaybackMessage> out;
        r.playback.silence(sec(1.0), out);
        r.piano.feed(out);
        CHECK_FALSE(r.playback.needs_silence());
    }
    CHECK(g_teardown_calls == 0);
    {
        Rig r(song);
        r.playback.set_teardown_check([](const Playback&) { ++g_teardown_calls; });
        r.play(0.0);
        r.update(1.0); // dropped while the note sounds
    }
    CHECK(g_teardown_calls == 1);
}

namespace {

// 200,000 channel events (programs, bends, controllers) on three channels over 2000 s.
// Few channels and controllers, so a seek sends a handful of messages.
Song random_channel_events(std::mt19937& rng) {
    Song song;
    song.duration_s = 2000.0;
    std::uniform_int_distribution<int> channel(0, 2);
    const int controllers[] = {1, 7, 10, 11, 91};
    std::uniform_int_distribution<int> controller_index(0, 4);
    std::uniform_int_distribution<int> value(0, 127);
    std::uniform_int_distribution<int> kind(0, 9);
    const int count = 200000;
    for (int i = 0; i < count; ++i) {
        ChannelEvent e;
        e.time_s = i * 0.01;
        e.channel = static_cast<std::uint8_t>(channel(rng));
        const int k = kind(rng);
        e.kind = k == 0   ? ChannelEventKind::Program
                 : k == 1 ? ChannelEventKind::PitchBend
                          : ChannelEventKind::Control;
        e.number = e.kind == ChannelEventKind::Control
                       ? static_cast<std::uint8_t>(controllers[controller_index(rng)])
                       : 0;
        e.value = static_cast<std::uint16_t>(
            e.kind == ChannelEventKind::PitchBend ? value(rng) * 128 : value(rng));
        song.events.push_back(e);
    }
    return song;
}

} // namespace

TEST_CASE("playback: restoring state at a position uses checkpoints and matches the file (F14)") {
    std::mt19937 rng(14);
    const Song song = random_channel_events(rng);
    Rig r(song);
    r.play(0.0);
    std::uniform_real_distribution<double> where(0.0, 1999.0);
    double now = 0.0;
    std::vector<double> targets;
    std::size_t most_replayed = 0;
    for (int i = 0; i < 300; ++i) {
        now += 0.001;
        targets.push_back(where(rng));
        r.seek(now, targets.back());
        // A seek replays the events since the last checkpoint, not the song from its start
        // (a scan of 150,000 events on average).
        REQUIRE(r.playback.last_restore_replayed() <= Playback::checkpoint_events());
        most_replayed = std::max(most_replayed, r.playback.last_restore_replayed());
    }
    CHECK(most_replayed > 0); // the counter counts

    // The piano holds, at the last target, what the file set at or before it, or the default.
    FakePiano fresh;
    std::array<std::array<int, 120>, 16> cc{};
    std::array<int, 16> program{};
    std::array<int, 16> bend{};
    for (std::size_t ch = 0; ch < 16; ++ch) {
        for (int n = 0; n < 120; ++n) {
            cc[ch][static_cast<std::size_t>(n)] = fresh.controller(static_cast<int>(ch), n);
        }
        program[ch] = fresh.program(static_cast<int>(ch));
        bend[ch] = fresh.bend(static_cast<int>(ch));
    }
    for (const ChannelEvent& e : song.events) {
        if (e.time_s > targets.back()) {
            break;
        }
        switch (e.kind) {
        case ChannelEventKind::Program:
            program[e.channel] = e.value;
            break;
        case ChannelEventKind::PitchBend:
            bend[e.channel] = e.value;
            break;
        case ChannelEventKind::Control:
            cc[e.channel][e.number] = e.value;
            break;
        }
    }
    for (int ch = 0; ch < 16; ++ch) {
        CAPTURE(ch);
        CHECK(r.piano.program(ch) == program[static_cast<std::size_t>(ch)]);
        CHECK(r.piano.bend(ch) == bend[static_cast<std::size_t>(ch)]);
        for (int n = 0; n < 120; ++n) {
            CAPTURE(n);
            CHECK(r.piano.controller(ch, n) ==
                  cc[static_cast<std::size_t>(ch)][static_cast<std::size_t>(n)]);
        }
    }
}

TEST_CASE("transposition source: display, playback and both evaluators change together (F10)") {
    const Song song = make_song(
        {AbsTrack("Right").note(1.0, 1.5, 0, 60).note(3.0, 3.5, 0, 60).note(5.0, 5.5, 0, 60)});
    TranspositionSource source(Transposition(0, kRange61));
    Rig r(song);
    r.playback.follow(&source);
    FlowEvaluator flow(song);
    flow.follow(&source);
    WaitEvaluator wait(song);
    wait.follow(&source);
    NoteWindow window(song);

    const auto pump = [&](double now_s) {
        r.transport.update(sec(now_s));
        std::vector<TransportOp> ops;
        r.transport.take_ops(ops);
        r.last.clear();
        for (const TransportOp& op : ops) {
            r.playback.apply(op, r.last);
            flow.apply(op);
            wait.apply(op);
        }
        r.piano.feed(r.last);
        flow.update(sec(now_s));
        wait.update(sec(now_s));
    };
    const auto press = [](double wall_s, int key) {
        MidiEvent e;
        e.time_ns = sec(wall_s);
        e.type = MidiEventType::NoteOn;
        e.data1 = static_cast<std::uint8_t>(key);
        e.data2 = 90;
        return e;
    };
    const auto shown_at = [&](double song_s) {
        NoteWindowOptions o;
        o.transposition = source.value();
        std::vector<VisibleNote> shown;
        window.query(song_s, o, shown);
        return shown.empty() ? -1 : shown.front().pitch;
    };

    r.transport.play(0);
    pump(0.0);
    flow.apply(press(1.0, 60));
    pump(2.0);
    CHECK(shown_at(2.0) == 60);
    CHECK(std::vector<int>(wait.waiting_keys().begin(), wait.waiting_keys().end()) == std::vector<int>{60});

    source.set(Transposition(2, kRange61), sec(2.0));
    CHECK(shown_at(2.0) == 62);
    flow.apply(press(3.0, 62));
    pump(4.0);
    CHECK(std::vector<int>(wait.waiting_keys().begin(), wait.waiting_keys().end()) == std::vector<int>{62});
    CHECK(r.playback.transposition() == source.value());
    const std::vector<PlaybackMessage> ons = of_kind(r.piano.log, MsgKind::NoteOn);
    REQUIRE(ons.size() == 2);
    CHECK(ons[0].data == 60);
    CHECK(ons[1].data == 62);
    flow.finish();
    const EvaluationSummary& s = flow.summary();
    REQUIRE(s.notes.size() == 2);
    CHECK(s.notes[0].expected_pitch == 60);
    CHECK(s.notes[1].expected_pitch == 62);
    CHECK(s.notes[1].grade == NoteGrade::Perfect);
    CHECK(s.wrong == 0);
    CHECK_FALSE(r.piano.ringing());
}
