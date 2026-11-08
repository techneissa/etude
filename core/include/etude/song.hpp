// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "etude/chord.hpp"
#include "etude/chord_spelling.hpp"
#include "etude/hand.hpp"
#include "etude/key_signature.hpp"

namespace etude {

// One note of a song. Times have the tempo map applied; ticks are kept next to them so beats
// (tick / ticks_per_quarter) and bar positions stay exact.
struct Note {
    double start_s = 0.0; // seconds from song start
    double duration_s = 0.0;
    std::uint8_t pitch = 60; // MIDI note number, before transposition
    std::uint8_t velocity = 64;
    Hand hand = Hand::Unknown;
    std::int8_t finger = 0; // 1..5, or 0 when unknown
    int measure = 1;        // bar number (see Measure), for loops and trouble-spot stats

    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0;  // always > start_tick
    std::uint16_t track = 0;    // index of the source track
    std::uint8_t channel = 0;   // 0..15
    bool drum = false;          // channel 10 (index 9): kept for playback, skipped by hands and
                                // evaluation
    bool accompaniment = false; // a part that is not a practice hand (see hand_split.hpp): it
                                // sounds with "song plays along", is never graded and has no hand
    int source_measure = -1;    // bar number in the written score (see Measure::source_number);
                                // -1: the same as `measure`
    int written_measure() const { return source_measure < 0 ? measure : source_measure; }
};

struct TempoChange {
    std::int64_t tick = 0;
    double seconds = 0.0; // time at `tick`, through every earlier change
    std::uint32_t us_per_quarter = 500'000;
};

// Converts between ticks and seconds through every tempo change. `changes` is sorted by tick and
// starts at tick 0; with no tempo event the file plays at 120 BPM.
struct TempoMap {
    int ticks_per_quarter = 480;
    std::vector<TempoChange> changes{TempoChange{}};

    double seconds_at(std::int64_t tick) const;
    // Fractional tick at a time; times before 0 map before tick 0 at the first tempo.
    double tick_at(double seconds) const;
    double beats_at(std::int64_t tick) const {
        return static_cast<double>(tick) / ticks_per_quarter;
    }
};

struct TimeSignature {
    std::int64_t tick = 0;
    std::uint8_t numerator = 4;
    std::uint8_t denominator = 4; // a power of two
};

// One bar. A bar ends where the next one starts.
//
// `number` counts the bars as they are played: 1, 2, 3 in order (a pickup is 0), one per bar of the
// timeline, so a repeated passage has new numbers on each pass. Loops, sections, practice stats and
// the scrubber all key on it, and it never repeats. `source_number` is the bar's number in the
// written score, which is what the printed page shows; it repeats where the score repeats.
struct Measure {
    int number = 1; // 1 for the first full bar; a pickup bar is 0
    std::int64_t start_tick = 0;
    double start_s = 0.0;
    std::uint8_t numerator = 4;
    std::uint8_t denominator = 4;
    int source_number = -1; // -1: the same as `number` (a file with no repeats)

    int written_number() const { return source_number < 0 ? number : source_number; }
};

enum class ChannelEventKind : std::uint8_t { Program, Control, PitchBend };

// A program change, control change or pitch bend of the file, kept so MIDI out can reproduce the
// sound the file asks for. Control changes include the sustain pedal (CC64), volume and
// expression (CC7, CC11) and bank select (CC0, CC32); none of them change a note's length.
struct ChannelEvent {
    double time_s = 0.0; // seconds from song start, tempo map applied
    std::int64_t tick = 0;
    std::uint16_t track = 0;  // index of the source track
    std::uint8_t channel = 0; // 0..15
    ChannelEventKind kind = ChannelEventKind::Control;
    std::uint8_t number = 0; // controller number for Control, 0 for the other kinds
    std::uint16_t value = 0; // program 0..127, controller value 0..127, bend 0..16383 (8192 is
                             // centre)
};

// The notes of one channel in one track, with what a hand split needs to know about them.
struct Part {
    std::uint16_t track = 0;
    std::uint8_t channel = 0;
    std::string name;            // channel name (meta 0x20 prefix) if set, else the track name
    std::string instrument_name; // meta 0x04, may be empty
    std::string instrument_id;   // a score file's instrument id (MuseScore "piano", MusicXML
                                 // "keyboard.piano"), may be empty
    std::uint8_t program = 0;    // first General MIDI program change, 0 (piano) if none
    bool has_program = false;
    bool drum = false;
    bool accompaniment = false; // set with the notes' flag when the hands are prepared
    int note_count = 0;
    Hand hand = Hand::Unknown;
};

// A chord symbol the file writes (MusicXML `<harmony>`): the chord that stands from `tick` until
// the next symbol. `chord.found` is false for a symbol that names no chord ("N.C.") or a kind
// this app cannot name; the stretch then has no chord. The spelling is the file's own (Bb stays
// Bb), on the line of fifths (chord_spelling.hpp).
struct ChordSymbol {
    std::int64_t tick = 0;
    double seconds = 0.0; // time at `tick`, through the tempo map
    Chord chord;
    ChordSpelling spelling;

    friend bool operator==(const ChordSymbol&, const ChordSymbol&) = default;
};

// The most notes any song holds. A bigger file is refused with a message instead of exhausting the
// memory of a small machine; real piano music stays far below it.
inline constexpr std::size_t kMaxSongNotes = 200'000;

// The most program changes, control changes and pitch bends a MIDI file may hold. A file with more
// is refused with a message, like one over the note cap; a pedal or a bend per few ticks of a long
// piece stays far below it.
inline constexpr std::size_t kMaxSongEvents = 1'000'000;

struct Song {
    std::vector<Note> notes; // sorted by start tick, then pitch
    std::vector<Part> parts; // sorted by track, then channel
    // Program changes, controllers and pitch bends, sorted by tick; events at one tick keep the
    // order of the file (track by track).
    std::vector<ChannelEvent> events;
    TempoMap tempo;
    std::vector<TimeSignature> time_signatures; // sorted, first at tick 0
    std::vector<Measure> measures;              // cover every note, sorted
    // The key from each tick on (the file's, or the notes' estimate for a file with none), sorted;
    // empty for a song with too few notes to tell. Filled once by the loaders (complete_keys).
    std::vector<KeyChange> keys;
    // The chord symbols the file writes, sorted by tick; empty for a file with none (a MIDI file,
    // or a score with no `<harmony>`). The harmony timeline (harmony.hpp) uses them in place of
    // detecting chords from the notes.
    std::vector<ChordSymbol> chord_symbols;
    std::int64_t end_tick = 0; // last note end
    double duration_s = 0.0;

    // Bar number at a tick (the bar containing it). Ticks before the first bar get its number.
    int measure_at(std::int64_t tick) const;
    // The same bar's number in the written score (Measure::source_number): the number the printed
    // page shows. Equal to measure_at() unless the score has repeats.
    int written_measure_at(std::int64_t tick) const;
};

// A 64-bit fingerprint of what practice and playback read of a song: each note's start, length,
// pitch, velocity, channel, hand, bar and drum and accompaniment flags, each bar's number and
// start, and each channel event. Two songs that differ in
// any of those have different fingerprints (but for a one in 2^64 clash), whatever their sizes. It
// costs one pass over the notes, about 0.3 ms at 120,000 notes.
std::uint64_t song_fingerprint(const Song& song);

// Hand named by a track or channel name, case-insensitive: "Right", "RH", "R.H.", "Piano R",
// "Right Hand", "Treble" give Right; "Left", "LH", "L.H.", "Piano L", "Bass" give Left. A name
// that names both hands, or neither, gives Unknown.
Hand hand_from_name(std::string_view name);

// Sets each part's hand from its name, and every note of that part to match. Drum parts stay
// Unknown, and so does a part named only "Bass" when its program is a General MIDI bass
// instrument (33..40 in 1-based numbering), since that is a bass guitar, not a left hand.
// Parts and notes with no clear name keep Unknown; the split point decides them later.
void assign_hands_from_names(Song& song);

} // namespace etude
