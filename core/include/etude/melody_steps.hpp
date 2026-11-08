// SPDX-License-Identifier: GPL-3.0-or-later
// Which of the Melodies' five steps a library song fits (the "From your library" row, and the step
// the details card's Add to Melodies suggests). Pure: the facts are what the library scan already
// stores for a song (its rating in half stars, its hands, the keys each hand covers, its bars), so
// nothing is rated when the row is drawn. The cut-offs are calibrated on the real ratings of the
// bundled pieces, each of which already has its step in the manifest (tests/melody_steps_test.cpp
// holds that every one of them lands in its own).
#pragma once

#include "etude/melody_manifest.hpp"

namespace etude {

// No step: the song is not suggested (and the player may still add it by hand).
inline constexpr int kMelodyNoStep = 0;

// What the sort reads of a song.
struct MelodyStepFacts {
    int half_stars = 0; // the rating, 2 to 10; 0 when the song has not been read
    bool left = false;  // the hands that have notes
    bool right = false;
    int left_keys = 0;  // the keys each hand covers, lowest to highest note
    int right_keys = 0;
    int bars = 0;
    int notes = 0;      // the notes the player plays (not drums or accompaniment)
};

// The cut-offs, in half stars and keys, set on the real ratings of the 14 bundled pieces (which span
// only 3 to 6 half stars: the library's rating alone cannot tell step 1 from 2 or 3 from 4, so the
// keys a piece covers decide those).
namespace melody_cut {
// A song longer than this many bars is not sorted (a long easy song rates by its few hard bars).
inline constexpr int kMaxBars = 48;
// A file with fewer notes or bars than this is no piece (a one-note test file rates easy): no step.
inline constexpr int kMinNotes = 8;
inline constexpr int kMinBars = 2;
// The right hand alone: step 1 up to these half stars and keys, step 2 up to the next pair.
inline constexpr int kStep1HalfStars = 3;
inline constexpr int kStep1Keys = 10;
inline constexpr int kStep2HalfStars = 4;
inline constexpr int kStep2Keys = 13;
// Both hands up to this many half stars are step 3 while the two hands together cover at most
// `kStep3Keys` keys, else step 4.
inline constexpr int kStep34HalfStars = 4;
inline constexpr int kStep3Keys = 40;
// Harder than that is step 5, up to this many half stars; a song rated harder is left out.
inline constexpr int kStep5HalfStars = 8;
} // namespace melody_cut

// The step (kMelodyFirstStep to kMelodyLastStep) the song fits, or kMelodyNoStep.
int suggest_melody_step(const MelodyStepFacts& facts);

// What a step asks, in a few words for the picker ("both hands, a simple left hand").
const char* melody_step_words(int step);

} // namespace etude
