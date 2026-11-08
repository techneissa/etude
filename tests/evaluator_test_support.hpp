// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by the evaluator_*test.cpp files (split from evaluator_test.cpp): the helpers more than one of them uses.
#pragma once

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

namespace evaluator_test_support {

using namespace etude;
using OpKind = TransportOp::Kind;

// A copy of the keys a Wait evaluator is waiting for (waiting_keys() is a view that the next call
// replaces).
inline std::vector<int> keys_of(std::span<const int> keys) { return {keys.begin(), keys.end()}; }

constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }

constexpr std::int64_t ms(double m) { return static_cast<std::int64_t>(std::llround(m * 1e6)); }

struct N {
    double start;
    int pitch;
    Hand hand = Hand::Right;
    double dur = 0.25;
};

// Bars of 2 s each (4/4 at 120 BPM), so a note's bar is floor(start / 2) + 1.
inline Song song_of(const std::vector<N>& list, double tail_s = 2.0) {
    Song s;
    double end = 0.0;
    for (const N& n : list) {
        Note note;
        note.start_s = n.start;
        note.duration_s = n.dur;
        note.pitch = static_cast<std::uint8_t>(n.pitch);
        note.hand = n.hand;
        note.measure = static_cast<int>(std::floor(n.start / 2.0)) + 1;
        note.start_tick = static_cast<std::int64_t>(std::llround(n.start * 960.0));
        note.end_tick = note.start_tick + std::max<std::int64_t>(1, std::llround(n.dur * 960.0));
        s.notes.push_back(note);
        end = std::max(end, n.start + n.dur);
    }
    std::stable_sort(s.notes.begin(), s.notes.end(), [](const Note& a, const Note& b) {
        return a.start_s < b.start_s || (a.start_s == b.start_s && a.pitch < b.pitch);
    });
    s.duration_s = end + tail_s;
    for (int bar = 0; bar * 2.0 < s.duration_s; ++bar) {
        Measure m;
        m.number = bar + 1;
        m.start_s = bar * 2.0;
        m.start_tick = bar * 1920;
        s.measures.push_back(m);
    }
    return s;
}

inline Song song_of(std::initializer_list<N> list, double tail_s = 2.0) {
    return song_of(std::vector<N>(list), tail_s);
}

inline MidiEvent note_on(double wall_s, int key, int vel = 90) {
    MidiEvent e;
    e.time_ns = sec(wall_s);
    e.type = MidiEventType::NoteOn;
    e.data1 = static_cast<std::uint8_t>(key);
    e.data2 = static_cast<std::uint8_t>(vel);
    return e;
}

[[maybe_unused]] inline MidiEvent note_off(double wall_s, int key) {
    MidiEvent e;
    e.time_ns = sec(wall_s);
    e.type = MidiEventType::NoteOff;
    e.data1 = static_cast<std::uint8_t>(key);
    return e;
}

[[maybe_unused]] inline MidiEvent cc(double wall_s, int number, int value) {
    MidiEvent e;
    e.time_ns = sec(wall_s);
    e.type = MidiEventType::ControlChange;
    e.data1 = static_cast<std::uint8_t>(number);
    e.data2 = static_cast<std::uint8_t>(value);
    return e;
}

// A Transport and an evaluator driven like the app's frame loop: each frame updates the clock,
// feeds its ops, then the MIDI events stamped up to now, then updates the evaluator. Play starts
// at wall time 1 s, so at 100% a note at song time s arrives at wall time 1 + s.
template <typename Evaluator, typename Options> struct Rig {
    Song song;
    Transport transport;
    Evaluator eval;
    std::vector<MidiEvent> inbox; // sorted by stamp before each frame
    std::int64_t now = 0;
    std::int64_t frame = 16'666'667;
    std::vector<WaitDirective> directives;
    std::vector<TransportOp> frame_ops; // the frame's ops, kept between frames

    Rig(const Song& s, const Options& o) : song(s) {
        transport.set_song(song);
        eval.set_song(song, o);
    }

    void send(const MidiEvent& e) { inbox.push_back(e); }
    void play_at(double wall_s, int speed = 100) {
        transport.set_speed_percent(sec(wall_s), speed);
        transport.play(sec(wall_s));
        now = sec(wall_s);
        flush_ops();
    }
    void flush_ops() {
        std::vector<TransportOp> ops;
        transport.take_ops(ops);
        for (const TransportOp& op : ops) {
            eval.apply(op);
        }
    }
    void step(std::int64_t to) {
        now = to;
        transport.update(now);
        flush_ops();
        std::stable_sort(inbox.begin(), inbox.end(), [](const MidiEvent& a, const MidiEvent& b) {
            return a.time_ns < b.time_ns;
        });
        std::size_t used = 0;
        while (used < inbox.size() && inbox[used].time_ns <= now) {
            eval.apply(inbox[used]);
            ++used;
        }
        inbox.erase(inbox.begin(), inbox.begin() + static_cast<std::ptrdiff_t>(used));
        after_update();
    }
    void after_update();
    void run_until(double wall_s) {
        const std::int64_t end = sec(wall_s);
        while (now + frame <= end) {
            step(now + frame);
        }
        if (now < end) {
            step(end);
        }
    }
};

using FlowRig = Rig<FlowEvaluator, FlowOptions>;

using WaitRig = Rig<WaitEvaluator, WaitOptions>;

template <> inline void FlowRig::after_update() { eval.update(now); }

template <> inline void WaitRig::after_update() {
    const WaitDirective d = eval.update(now);
    directives.push_back(d);
    if (d.hold_now) {
        transport.hold(now);
    }
    if (d.release) {
        transport.release(now);
    }
    if (d.arm_hold) {
        transport.hold_at(now, d.hold_at_s);
    }
    flush_ops();
}

[[maybe_unused]] inline int hits(const EvaluationSummary& s) {
    return s.counts.perfect + s.counts.good + s.counts.early + s.counts.late;
}

// A song with what a frame of grading meets: single notes, a run of one key inside one window
// chain, chords and a held bass, in both hands, with the player a little early or late on each
// note, one note missed and a wrong key now and then.
struct PlayedSong {
    Song song;
    std::vector<MidiEvent> presses; // wall time order
};

// `clean`: every note pressed, at or up to 30 ms before its time, and no wrong key: what Wait Mode
// can follow to the end of the song.
inline PlayedSong played_song(bool clean = false) {
    std::vector<N> list;
    PlayedSong out;
    std::mt19937 rng(7);
    std::uniform_real_distribution<double> jitter(-0.030, clean ? 0.0 : 0.030);
    double t = 0.5;
    for (int phrase = 0; phrase < 24; ++phrase) {
        for (int i = 0; i < 12; ++i) { // a run on one key, 60 ms apart: one group
            list.push_back({t, 60, Hand::Right, 0.05});
            t += 0.060;
        }
        for (int i = 0; i < 4; ++i) { // a chord and its bass
            list.push_back({t, 64, Hand::Right, 0.3});
            list.push_back({t, 67, Hand::Right, 0.3});
            list.push_back({t, 48, Hand::Left, 0.6});
            t += 0.4;
        }
        t += 0.3;
    }
    out.song = song_of(list);
    int index = 0;
    for (const Note& n : out.song.notes) {
        ++index;
        if (!clean && index % 17 == 0) {
            continue; // missed
        }
        out.presses.push_back(note_on(1.0 + n.start_s + jitter(rng), n.pitch));
        if (!clean && index % 11 == 0) {
            out.presses.push_back(note_on(1.0 + n.start_s + 0.01, n.pitch + 1)); // a wrong key
        }
    }
    std::stable_sort(out.presses.begin(), out.presses.end(),
                     [](const MidiEvent& a, const MidiEvent& b) { return a.time_ns < b.time_ns; });
    return out;
}

} // namespace evaluator_test_support
