// SPDX-License-Identifier: GPL-3.0-or-later
// The sound of a generated exercise (exercises.hpp) for the built-in piano or a MIDI output: each
// note's start and end as a message due at a time. Plain data and arithmetic; the app hands the
// messages to the sound thread, which starts each one at its due time (song_schedule.hpp), so a slow
// frame cannot make a note late.
#pragma once

#include "etude/song.hpp"
#include "etude/song_schedule.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace etude {

// The most messages an exercise's sound may hold: the sound thread's queue is 512 deep, and the
// longest exercise a card plays (two octaves up and down, both hands) is far under it.
inline constexpr std::size_t kMaxExerciseMessages = 400;

// A note-on and a note-off for each note of `song`, due `start_ns` plus the note's time, in due
// order, a note-off before a note-on due at the same time (a repeated key sounds again). Empty for
// a song of more notes than `kMaxExerciseMessages` allows.
std::vector<SongSoundEvent> exercise_sound_events(const Song& song, std::int64_t start_ns);

// Seconds from the first note's start to the last note's end; 0 for no notes.
double exercise_length_s(const Song& song);

// When each note of an exercise sounds, as `exercise_sound_events` times it: from its start for its
// duration (at least `kMinNoteSeconds`), in seconds from the exercise's first start. `semitone` is
// the note's distance above the lowest note of the same hand: for a scale, the tonic.
struct ExerciseSpan {
    std::uint8_t pitch = 60;
    Hand hand = Hand::Unknown;
    std::int8_t finger = 0;
    int semitone = 0;
    double start_s = 0.0;
    double end_s = 0.0;

    bool sounding_at(double t) const { return t >= start_s && t < end_s; }
};
std::vector<ExerciseSpan> exercise_spans(const Song& song);

// Writes the places in `spans` of the notes sounding at second `t` into `out` (at most `out.size()`
// of them) and returns how many it wrote, so a frame finds what lights without allocating.
std::size_t spans_sounding_at(std::span<const ExerciseSpan> spans, double t,
                              std::span<std::uint16_t> out);

} // namespace etude
