// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the song controller tests: a two-hand test song, a fake piano that takes the song's
// sound, a frame runner, and the key events a test presses.
#pragma once

#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <span>
#include <vector>

namespace song_test {

using namespace etude;
using namespace etude::app;

inline constexpr std::int64_t kMs = 1'000'000;
inline constexpr std::int64_t kSec = 1'000'000'000;

inline LoadedSong loaded(Song s, std::uint64_t hash) {
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Test";
    out.hash = hash;
    out.library_path = "test.mid";
    return out;
}

// `bars` bars of 4/4 at 120 BPM (0.5 s a beat): the right hand plays quarter notes C5 D5 E5 F5 ...,
// the left hand holds a C3 per bar.
inline LoadedSong two_hand_song(int bars = 2, std::uint64_t hash = 0x1234) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            Note n;
            n.start_tick = (bar * 4 + beat) * 480;
            n.end_tick = n.start_tick + 400;
            n.pitch = static_cast<std::uint8_t>(72 + beat * 2);
            n.velocity = 80;
            n.hand = Hand::Right;
            s.notes.push_back(n);
        }
        Note l;
        l.start_tick = bar * 4 * 480;
        l.end_tick = l.start_tick + 4 * 480 - 10;
        l.pitch = 48;
        l.velocity = 70;
        l.hand = Hand::Left;
        l.channel = 1;
        l.track = 1;
        s.notes.push_back(l);
    }
    return loaded(std::move(s), hash);
}

// Notes of one hand at the given starts (seconds), 0.5 s long.
inline LoadedSong notes_song(const std::vector<std::pair<double, int>>& list,
                             std::uint64_t hash = 0x77) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (const auto& [start, pitch] : list) {
        Note n;
        n.start_tick = static_cast<std::int64_t>(start * 960.0);
        n.end_tick = n.start_tick + 480;
        n.pitch = static_cast<std::uint8_t>(pitch);
        n.velocity = 80;
        n.hand = Hand::Right;
        s.notes.push_back(n);
    }
    return loaded(std::move(s), hash);
}

// The receiver the app has in front of its sounds: a message starts when its due time comes, a
// CancelUnstarted drops the notes that have not started, and nothing may overflow it or use a
// channel that is not one of the 16.
struct FakePiano {
    DueQueue<4096> queue;

    void event(const SongSoundEvent& e) {
        switch (e.kind) {
        case SongSoundEvent::Kind::Message:
            REQUIRE(e.message.channel < 16);
            REQUIRE(queue.push(e.message));
            break;
        case SongSoundEvent::Kind::CancelUnstarted:
            queue.cancel_unstarted(e.message.at_ns);
            break;
        case SongSoundEvent::Kind::StopAll:
            queue.clear();
            break;
        }
    }
    // Time reaches `now_ns`: every message due by then starts.
    void advance(std::int64_t now_ns) {
        while (!queue.empty() && queue.front().at_ns <= now_ns) {
            queue.pop_front();
        }
    }
};

// Runs frames of ~16 ms from `now` to `until`, sending the song's sound to the piano.
inline void run(SongController& c, FakePiano& piano, std::int64_t& now, std::int64_t until,
                std::span<const MidiEvent> events = {}) {
    for (bool first = true; now < until; first = false) {
        now = std::min(until, now + 16 * kMs);
        c.update(now, first ? events : std::span<const MidiEvent>{});
        std::vector<SongSoundEvent> sound;
        c.take_sound_events(sound);
        for (const SongSoundEvent& e : sound) {
            piano.event(e);
        }
        piano.advance(now);
    }
}

// A press from the typing keys, or (midi_note_on) from a MIDI keyboard, which the octave detector
// reads.
inline MidiEvent press_with(int key, int velocity, std::int64_t at,
                            InputSource source = InputSource::ComputerKeyboard) {
    return {.time_ns = at,
            .type = MidiEventType::NoteOn,
            .data1 = static_cast<std::uint8_t>(key),
            .data2 = static_cast<std::uint8_t>(velocity),
            .source = source};
}
inline MidiEvent note_on(int key, std::int64_t at) { return press_with(key, 90, at); }
inline MidiEvent midi_note_on(int key, std::int64_t at) {
    return press_with(key, 90, at, InputSource::Midi);
}

// The host's side of a frame: events wait in `queue` (the MIDI queue) until the frame drains them
// into `frame`; an action in the frame asks for them through the pull first.
struct Host {
    SongController& c;
    std::vector<MidiEvent> queue;
    std::vector<MidiEvent> frame;

    explicit Host(SongController& controller) : c(controller) {
        c.set_event_pull([this] { drain(); });
    }
    void drain() {
        frame.insert(frame.end(), queue.begin(), queue.end());
        queue.clear();
        c.take_in(frame);
    }
    // The frame's end: drain, update, forget the frame's events.
    void finish_frame(std::int64_t now) {
        drain();
        c.take_in(frame);
        c.update(now, frame);
        frame.clear();
    }
    void frames(std::int64_t& now, std::int64_t until) {
        while (now < until) {
            now = std::min(until, now + 16 * kMs);
            finish_frame(now);
        }
    }
};

} // namespace song_test
