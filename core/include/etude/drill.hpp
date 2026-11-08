// SPDX-License-Identifier: GPL-3.0-or-later
// A drill the Learn cards play: a scale or an arpeggio, made as an exercise (exercises.hpp) at the
// tempo the card shows, for Listen (the piano plays it) and Practice (the normal player plays it, with
// finger numbers on the falling notes, graded like any song). Pure: the drill's settings in, a song and the title
// the practice history keeps it under out.
#pragma once

#include "etude/chord.hpp"
#include "etude/exercises.hpp"
#include "etude/keyboard.hpp"
#include "etude/scales.hpp"
#include "etude/song.hpp"

#include <cstdint>
#include <optional>
#include <string>

namespace etude {

enum class DrillKind : std::uint8_t {
    Scale,    // the scale card's Scale: the scale's notes in order
    Arpeggio, // the scale's triad (the scale card's Arpeggio) or the chord card's chord, note by note
};

// The tempo steps the card offers, in beats per minute (an eighth note is one note of a drill).
inline constexpr int kDrillMinBpm = 40;
inline constexpr int kDrillMaxBpm = 200;
inline constexpr int kDrillBpmStep = 5;
inline constexpr int kDrillDefaultBpm = 80;

struct DrillSpec {
    DrillKind kind = DrillKind::Scale;
    int root_pc = 0;                                  // 0..11, C = 0
    ScaleType scale = ScaleType::Major;               // Scale; Arpeggio from the scale card
    std::optional<ChordQuality> chord;                // Arpeggio from the chord card: its quality
    int inversion = 0;                                // the chord's inversion (0 for a scale)
    int octaves = 1;                                  // 1 or 2
    Direction direction = Direction::Up;
    HandsChoice hands = HandsChoice::Together;
    int bpm = kDrillDefaultBpm;                       // kDrillMinBpm .. kDrillMaxBpm
};

struct Drill {
    bool ok = false;
    std::string message; // English, shown as a toast, when ok is false
    std::string title;   // "F major scale, RH, 2 oct": what the practice history calls the drill
    std::uint64_t hash = 0; // the drill's identity in the history: the same drill, the same hash
    Song song;           // empty unless ok
};

// The chord an arpeggio of `type`'s scale plays: the scale's first, third and fifth. Empty for a scale
// with no triad on its tonic (the chromatic, whose third is not one note, and any whose tonic has no
// third or fifth a triad could use).
std::optional<ChordQuality> scale_triad(ScaleType type);

// A tempo made whole, in range and on the step: `bpm` clamped and rounded to `kDrillBpmStep`.
int drill_tempo(int bpm);

// The song and title for `spec` placed on `range`. A scale with no triad asked as an arpeggio, a chord
// with no arpeggio (the ninth chords) and a keyboard too small for the drill answer ok = false with a
// message saying which, never an exception.
Drill make_drill(const DrillSpec& spec, KeyRange range = kRange61);

} // namespace etude
