// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the song_controller_*_test.cpp files: a two-hand test song, a fake piano that takes the
// song's sound, a frame runner, and the key events a test presses.
#pragma once

#include "mic_inbox.hpp"
#include "song_controller.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <bitset>
#include <cstdint>
#include <span>
#include <utility>
#include <vector>

namespace song_test {

using namespace etude;
using namespace etude::app;

inline constexpr std::int64_t kMs = 1'000'000;
inline constexpr std::int64_t kSec = 1'000'000'000;

// Two bars of 4/4 at 120 BPM (0.5 s a beat): the right hand plays quarter notes C5 D5 E5 F5 ...,
// the left hand holds a C3 per bar.
inline LoadedSong two_hand_song(int bars = 2, bool pedal = false, int right_base = 72,
                         std::uint64_t hash = 0x1234) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int bar = 0; bar < bars; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            Note n;
            n.start_tick = (bar * 4 + beat) * 480;
            n.end_tick = n.start_tick + 400;
            n.pitch = static_cast<std::uint8_t>(right_base + (beat * 2));
            n.velocity = 80;
            n.hand = Hand::Right;
            n.channel = 0;
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
    if (pedal) {
        // The right hand's pedal goes down at 0.5 s and up at 1.5 s.
        for (const auto& [tick, value] : {std::pair{480, 127}, std::pair{1440, 0}}) {
            ChannelEvent e;
            e.tick = tick;
            e.channel = 0;
            e.kind = ChannelEventKind::Control;
            e.number = 64;
            e.value = static_cast<std::uint16_t>(value);
            s.events.push_back(e);
        }
    }
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Test";
    out.hash = hash;
    out.library_path = "test.mid";
    return out;
}

// A MIDI device as the piano sees it: what sounds after the bytes so far. Behind it sits the
// receiver the app has in front of its sounds: a message starts when its due time comes, and a
// CancelUnstarted drops the notes that have not started (DueQueue is the policy both use).
struct FakePiano {
    std::array<std::bitset<128>, 16> sounding{};
    std::bitset<16> pedal;
    int note_ons = 0;
    int note_offs = 0;
    DueQueue<4096> queue;
    // The messages that started in the last advance().
    std::vector<PlaybackMessage> started;
    // Messages dropped by CancelUnstarted before they started.
    std::size_t cancelled = 0;
    // Keys (any channel) that a note-on has started.
    std::bitset<128> ever_started;

    void play(const PlaybackMessage& m) {
        REQUIRE(m.channel < 16);
        switch (m.kind) {
        case PlaybackMessage::Kind::NoteOn:
            if (m.value == 0) {
                sounding[m.channel].reset(m.data);
            } else {
                sounding[m.channel].set(m.data);
                ever_started.set(m.data);
                ++note_ons;
            }
            break;
        case PlaybackMessage::Kind::NoteOff:
            sounding[m.channel].reset(m.data);
            ++note_offs;
            break;
        case PlaybackMessage::Kind::ControlChange:
            if (m.data == 64) {
                pedal.set(m.channel, m.value >= 64);
            } else if (m.data == 123 || m.data == 120) {
                sounding[m.channel].reset();
            }
            break;
        case PlaybackMessage::Kind::ProgramChange:
        case PlaybackMessage::Kind::PitchBend:
            break;
        }
    }
    // The sound receives an event.
    void event(const SongSoundEvent& e) {
        switch (e.kind) {
        case SongSoundEvent::Kind::Message:
            REQUIRE(queue.push(e.message));
            break;
        case SongSoundEvent::Kind::CancelUnstarted:
            cancelled += queue.cancel_unstarted(e.message.at_ns);
            break;
        case SongSoundEvent::Kind::StopAll:
            queue.clear();
            sounding = {};
            pedal.reset();
            break;
        }
    }
    // Time reaches `now_ns`: every message due by then starts.
    void advance(std::int64_t now_ns) {
        started.clear();
        while (!queue.empty() && queue.front().at_ns <= now_ns) {
            started.push_back(queue.front());
            play(queue.front());
            queue.pop_front();
        }
    }
    std::size_t count() const {
        std::size_t n = 0;
        for (const auto& ch : sounding) {
            n += ch.count();
        }
        return n;
    }
    bool silent() const { return count() == 0 && pedal.none(); }
};

// What the song has made since the last call, handed to the piano; time is `now`.
inline void feed(SongController& c, FakePiano& piano, std::int64_t now) {
    std::vector<SongSoundEvent> events;
    c.take_sound_events(events);
    for (const SongSoundEvent& e : events) {
        piano.event(e);
    }
    piano.advance(now);
}

// Quit: the piano gets everything and time runs on until the queue is empty.
inline void shutdown_into(SongController& c, FakePiano& piano, std::int64_t now) {
    std::vector<SongSoundEvent> events;
    c.shutdown(now, events);
    for (const SongSoundEvent& e : events) {
        piano.event(e);
    }
    piano.advance(INT64_MAX);
}

// Runs frames of ~16 ms from `now` to `until`, sending the song's sound to the piano.
inline void run(SongController& c, FakePiano& piano, std::int64_t& now, std::int64_t until,
         std::span<const MidiEvent> events = {}) {
    bool first = true;
    while (now < until) {
        now = std::min(until, now + 16 * kMs);
        c.update(now, first ? events : std::span<const MidiEvent>{});
        first = false;
        feed(c, piano, now);
    }
}

inline void drain(SongController& c, FakePiano& piano, std::int64_t now) { feed(c, piano, now); }

inline MidiEvent note_on(int key, std::int64_t at) {
    MidiEvent ev;
    ev.time_ns = at;
    ev.type = MidiEventType::NoteOn;
    ev.data1 = static_cast<std::uint8_t>(key);
    ev.data2 = 90;
    ev.source = InputSource::ComputerKeyboard;
    return ev;
}

// A note from a MIDI keyboard (not the typing keys or the screen's): what the octave detector
// reads.
inline MidiEvent midi_note_on(int key, std::int64_t at) {
    MidiEvent ev = note_on(key, at);
    ev.source = InputSource::Midi;
    return ev;
}

inline MidiEvent note_off(int key, std::int64_t at) {
    MidiEvent ev = note_on(key, at);
    ev.type = MidiEventType::NoteOff;
    ev.data2 = 0;
    return ev;
}


// A click the song handed over: when it is due and the frame time it was handed over at.
struct HandedClick {
    std::int64_t due_ns = 0;
    std::int64_t handed_ns = 0;
    int velocity = 0;
};

// Runs frames of `frame_ms` from `now` to `until`, collecting the click note-ons (and the other
// note-ons' due times) as they are handed over. The piano is fed too, so cancels apply.
struct ClickRun {
    std::vector<HandedClick> clicks;
    std::vector<std::int64_t> note_dues; // song notes, channel 0
    FakePiano piano;
    std::int64_t frame_ns = 16 * kMs;

    // Takes what the song has handed over, as the frame loop does after each update.
    void collect(SongController& c, std::int64_t now) {
        std::vector<SongSoundEvent> events;
        c.take_sound_events(events);
        for (const SongSoundEvent& e : events) {
            if (e.kind == SongSoundEvent::Kind::Message && starts_note(e.message)) {
                if (e.message.click) {
                    clicks.push_back({e.message.at_ns, now, e.message.value});
                } else if (e.message.channel == 0) {
                    note_dues.push_back(e.message.at_ns);
                }
            }
            piano.event(e);
        }
        piano.advance(now);
    }
    void frame(SongController& c, std::int64_t& now) {
        now += frame_ns;
        c.update(now, {});
        collect(c, now);
    }
    void until(SongController& c, std::int64_t& now, std::int64_t end) {
        while (now < end) {
            frame(c, now);
        }
    }
};

inline SongOptions click_options(bool metronome) {
    SongOptions o;
    o.metronome = metronome;
    return o;
}

// The keys Wait Mode shows at the first chord, as (key, hand) pairs.
inline std::vector<std::pair<int, Hand>> first_chord(SongController& c, std::int64_t& now) {
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 500 * kMs);
    std::vector<std::pair<int, Hand>> out;
    for (const SongController::HintKey& k : c.waiting_keys()) {
        out.emplace_back(k.key, k.hand);
    }
    return out;
}

inline MidiEvent press_with(int key, int velocity, std::int64_t at) {
    MidiEvent ev = note_on(key, at);
    ev.data2 = static_cast<std::uint8_t>(velocity);
    return ev;
}

// Notes of one hand at the given starts, 0.5 s long.
inline LoadedSong notes_song(const std::vector<std::pair<double, int>>& list, std::uint64_t hash = 0x77) {
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
    REQUIRE(finish_song(s));
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Test";
    out.hash = hash;
    out.library_path = "test.mid";
    return out;
}

// The host's side of a frame: events wait in `queue` (the MIDI queue) until the frame drains them
// into `frame`; an action in the frame asks for them through the pull first.
struct Host {
    SongController& c;
    std::vector<MidiEvent> queue;
    std::vector<MidiEvent> frame;
    bool pull = true;
    // A fake microphone: the analysis thread's decided pitch events wait in `mic_queue`, and the
    // host takes them through a MicInbox, as the app does. `mic_in_pull` false is the old wiring,
    // where only the frame's own drain took them.
    std::vector<MicEventIn> mic_queue;
    MicInbox mic;
    bool mic_in_pull = true;
    bool mic_before_midi = false; // the wrong order: the microphone taken above the MIDI drain
    bool both = false;            // Both mode: a MIDI press goes to the microphone's merge
    std::int64_t mic_now = 0;
    std::vector<std::int64_t> unsure_given; // the onsets handed to input_unsure, in order
    std::vector<MidiEvent> given;           // every event handed to the song, for counting
    int presses_of(int key) const {
        int n = 0;
        for (const MidiEvent& ev : given) {
            n += ev.type == MidiEventType::NoteOn && ev.data1 == key ? 1 : 0;
        }
        return n;
    }

    explicit Host(SongController& controller, bool pull_events = true)
        : c(controller), pull(pull_events) {
        if (pull) {
            c.set_event_pull([this] { drain(); });
        }
    }
    void take_mic() {
        std::vector<MidiEvent> out;
        std::vector<UnsureMark> marks;
        mic.take(
            [this](MicEventIn& in) {
                if (mic_queue.empty()) {
                    return false;
                }
                in = mic_queue.front();
                mic_queue.erase(mic_queue.begin());
                return true;
            },
            mic_now, out, marks);
        frame.insert(frame.end(), out.begin(), out.end());
        given.insert(given.end(), out.begin(), out.end());
        for (const UnsureMark& m : marks) {
            unsure_given.push_back(m.onset_ns);
            c.input_unsure(m.onset_ns);
        }
    }
    void drain() {
        if (mic_in_pull && mic_before_midi) {
            take_mic();
        }
        for (const MidiEvent& ev : queue) {
            if (both && ev.type == MidiEventType::NoteOn) {
                mic.bridge().note_midi_press(ev.data1, ev.time_ns);
            }
        }
        frame.insert(frame.end(), queue.begin(), queue.end());
        given.insert(given.end(), queue.begin(), queue.end());
        queue.clear();
        if (mic_in_pull && !mic_before_midi) {
            take_mic();
        }
        c.take_in(frame);
    }
    // The frame's end: drain, update, forget the frame's events.
    void finish_frame(std::int64_t now) {
        mic_now = now;
        drain();
        take_mic(); // the frame's own drain takes the microphone's notes in either wiring
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

// A controller whose tables are built on the worker whatever the song's size.
inline void make_long(SongController& c) { c.set_inline_table_notes(0); }

// The worker's answer taken by an update: the change is in force when this returns.
inline void settle_tables(SongController& c, std::int64_t& now) {
    c.wait_tables();
    c.update(now += 16 * kMs, {});
}

inline std::vector<int> wait_keys(SongController& c, std::int64_t& now) {
    std::vector<int> keys;
    for (const auto& [key, hand] : first_chord(c, now)) {
        keys.push_back(key);
    }
    return keys;
}


} // namespace song_test
