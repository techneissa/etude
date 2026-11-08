// SPDX-License-Identifier: GPL-3.0-or-later
// Shared by run_replay_test.cpp and run_summary_test.cpp: a four-bar song and the events of a run
// played on it.
#pragma once

#include "etude/run_events.hpp"
#include "etude/run_record.hpp"
#include "etude/song.hpp"

#include <cstdint>
#include <vector>

namespace replay_test {

using namespace etude;

// Four bars of two seconds each. Per bar b (0-based), three notes:
//   index 3b    right hand, key 72, at the bar's start, half a second long
//   index 3b+1  right hand, key 74, one second in, half a second long
//   index 3b+2  left hand, key 48, at the bar's start, a second long
// The notes are in this order, not sorted by time: the events name them by index.
struct Fixture {
    Song song;
    std::vector<Measure> measures;
    std::vector<Note> notes;
};

inline Fixture make_fixture(int bars = 4) {
    Fixture f;
    for (int b = 0; b < bars; ++b) {
        const double start = 2.0 * b;
        Measure m;
        m.number = b + 1;
        m.start_s = start;
        f.measures.push_back(m);
        const auto add = [&](double at, double len, std::uint8_t pitch, Hand hand) {
            Note n;
            n.start_s = at;
            n.duration_s = len;
            n.pitch = pitch;
            n.hand = hand;
            n.measure = b + 1;
            f.notes.push_back(n);
        };
        add(start, 0.5, 72, Hand::Right);
        add(start + 1.0, 0.5, 74, Hand::Right);
        add(start, 1.0, 48, Hand::Left);
    }
    f.song.notes = f.notes;
    f.song.measures = f.measures;
    f.song.duration_s = 2.0 * bars;
    return f;
}

inline RunEvent press(int note, int song_ms, int error_ms, NoteGrade grade, int key,
                      Hand hand, int velocity = 80) {
    RunEvent e;
    e.kind = RunEventKind::Press;
    e.note_index = note;
    e.song_ms = song_ms;
    e.error_ms = error_ms;
    e.grade = grade;
    e.key = static_cast<std::uint8_t>(key);
    e.hand = hand;
    e.velocity = static_cast<std::uint8_t>(velocity);
    return e;
}

inline RunEvent miss(int note, int song_ms, int key, Hand hand) {
    RunEvent e;
    e.kind = RunEventKind::Missed;
    e.note_index = note;
    e.song_ms = song_ms;
    e.key = static_cast<std::uint8_t>(key);
    e.hand = hand;
    e.grade = NoteGrade::Missed;
    return e;
}

inline RunEvent wrong(int song_ms, int key, int velocity = 70) {
    RunEvent e;
    e.kind = RunEventKind::Wrong;
    e.song_ms = song_ms;
    e.key = static_cast<std::uint8_t>(key);
    e.velocity = static_cast<std::uint8_t>(velocity);
    return e;
}

// A perfect press of note `i` of the fixture, on the note's start, at its own key.
inline RunEvent perfect(const Fixture& f, int i, int error_ms = 0) {
    const Note& n = f.notes[static_cast<std::size_t>(i)];
    return press(i, static_cast<int>(n.start_s * 1000.0) + error_ms, error_ms, NoteGrade::Perfect,
                 n.pitch, n.hand);
}

inline RunEvents run_of(std::vector<RunEvent> events, int expected = 0, bool complete = false) {
    RunEvents r;
    r.events = std::move(events);
    r.expected_notes = expected;
    r.complete = complete;
    return r;
}

inline RunRecord record_of(RunMode mode = RunMode::Flow, HandFilter hands = HandFilter::Both,
                           int speed = 100, int transpose = 0) {
    RunRecord r;
    r.mode = mode;
    r.hands = hands;
    r.speed_percent = speed;
    r.transpose_semitones = transpose;
    return r;
}

} // namespace replay_test
