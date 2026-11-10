// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "etude/accidental_glyph.hpp"

namespace etude {

struct Song;

// The key of a song, as the file writes it or as its notes estimate it, and the words the key
// label shows. Pure: no clock, no font, no file. The loaders fill `Song::keys` once, on the loader
// thread, so the frame loop only looks the key up.

enum class KeyMode : std::uint8_t { Major, Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian };
inline constexpr int kKeyModeCount = 7;

// The key from `tick` on. `fifths` is the signature (sharps positive, flats negative, -7..7) the
// tonic is spelled from, so "F# minor" and "Gb minor" are told apart; `tonic` is its pitch class
// (C = 0).
struct KeyChange {
    std::int64_t tick = 0;
    double seconds = 0.0; // time at `tick`, through the tempo map
    std::uint8_t tonic = 0;
    KeyMode mode = KeyMode::Major;
    std::int8_t fifths = 0;
    bool estimated = false; // from the notes, not from the file; shown exactly like a written key
    float confidence = 1.0f; // 1 for a key the file writes; the estimate's, 0..1, for the tests

    friend bool operator==(const KeyChange&, const KeyChange&) = default;
};

// Where a mode's tonic sits on the line of fifths relative to its key signature: major 0, minor 3
// (A minor has C major's signature, and A is three fifths past C), dorian 2, phrygian 4, lydian
// -1, mixolydian 1, locrian 5.
int mode_line_shift(KeyMode mode);
// The mode with that shift, major when none has it.
KeyMode mode_from_line_shift(int shift);
// "major", "minor", "dorian" ...
const char* mode_word(KeyMode mode);

// A mode by its name in a score file (MusicXML `<mode>`, MuseScore): major or ionian, minor or
// aeolian, dorian, phrygian, lydian, mixolydian, locrian, in either case. False for anything else
// ("none", an empty or unknown word); `mode` is untouched then.
bool key_mode_from_name(std::string_view name, KeyMode& mode);

// A key from the signature a file writes (clamped to -7..7) and its mode.
KeyChange key_from_signature(std::int64_t tick, int fifths, KeyMode mode);
// A key from a pitch class and a mode, the signature with the fewest accidentals; of two with six
// (F# or Gb major, Eb or D# minor) the tonic's `tie_prefers_flats` decides (F# major, Eb minor).
KeyChange key_from_pitch_class(std::int64_t tick, int tonic_pc, KeyMode mode,
                               bool prefer_flats = false);

// The same key `semitones` higher (or lower): the tonic moves and the signature follows by the
// fewest accidentals; of two spellings with as many, flats when `prefer_flats` or the new tonic is
// a flat black key (`tie_prefers_flats`). 0 is the key.
KeyChange transposed_key(const KeyChange& key, int semitones, bool prefer_flats = false);

// The key in force at `tick`: the last change at or before it, the first one for a tick before it.
// Null for no keys.
const KeyChange* key_at_tick(const std::vector<KeyChange>& keys, std::int64_t tick);
// The same by time, seconds from the song's start.
const KeyChange* key_at_seconds(const std::vector<KeyChange>& keys, double seconds);
// The index of that key, -1 for none.
int key_index_at_seconds(const std::vector<KeyChange>& keys, double seconds);

// Whether black keys are spelled with flats while `key` is played `transpose_semitones` higher: its
// signature has flats. C major, A minor and every other key of no accidentals spell with sharps; so
// does no key at all (null).
bool key_spells_flats(const KeyChange* key, int transpose_semitones = 0);

// What the key label says, in the pieces the renderer draws: "G major" is the tonic "G", no
// accidental, the mode "major"; the hint after it is the signature, "1" with a sharp or "3" with a
// flat, none for C major and A minor. Solfege names the tonic Do Re Mi Fa Sol La Si ("Sol major");
// the accidentals are glyphs (accidental_glyph.hpp), never font text.
struct KeyLabelParts {
    std::string tonic;
    AccidentalKind accidental = AccidentalKind::None;
    std::string mode;
    int signature_count = 0;
    AccidentalKind signature_kind = AccidentalKind::None;

    friend bool operator==(const KeyLabelParts&, const KeyLabelParts&) = default;
};
// `transpose_semitones` is the session's transposition: the label names the key that is played.
KeyLabelParts key_label_parts(const KeyChange& key, bool solfege, int transpose_semitones = 0,
                              bool prefer_flats = false);
// The label as one line of text for logs and tests: "G major 1#", "Eb minor 6b" ("Sol major").
std::string key_label_text(const KeyLabelParts& parts);

// ---- Estimating a key from the notes (Krumhansl-Schmuckler)

// How strongly each pitch class sounds in a piece: the sum of its notes' lengths.
using PitchClassWeights = std::array<double, 12>;

struct KeyEstimate {
    bool valid = false; // false when the weights hold too little to say
    std::uint8_t tonic = 0;
    KeyMode mode = KeyMode::Major; // Major or Minor only
    // Pearson correlation of the weights with the best key's profile (-1..1), and its lead over
    // the best key of another tonic or mode (0..2).
    float correlation = 0.0f;
    float margin = 0.0f;
    // The correlation of C major, to tell a placeholder from a real C major.
    float c_major_correlation = 0.0f;
};

KeyEstimate estimate_key(const PitchClassWeights& weights);
// The weights of a song: every pitched note (drums leave out), by its length in ticks.
PitchClassWeights pitch_class_weights(const Song& song);
KeyEstimate estimate_song_key(const Song& song);

// A file that writes C major at its start (signature 0, major) and nothing else is treated as
// writing no key when the notes lead C major's correlation by at least this much.
inline constexpr float kPlaceholderMargin = 0.10f;
// The estimate needs at least this many notes to be believed.
inline constexpr int kMinNotesForKey = 8;
// ... and at least this many different pitch classes among them.
inline constexpr int kMinPitchClassesForKey = 3;

// Finishes `song.keys` after a loader: sorts and merges what the file wrote, sets each change's
// `seconds`, and, for a file with no key or only a C major placeholder the notes disagree with,
// puts the estimate in as the one key at tick 0. Leaves a file with any other key alone, and a
// song with too few notes without one.
void complete_keys(Song& song);

} // namespace etude
