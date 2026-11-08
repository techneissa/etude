// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/keyboard.hpp"
#include "etude/song.hpp"
#include "etude/transpose.hpp"

#include <array>
#include <optional>
#include <span>

namespace etude {

// What every transposition would leave off the keyboard: for each shift of -12..+12 semitones, how
// many of the song's notes to play land outside the keyboard once shifted (the notes the
// transposition then folds by an octave and marks). Drums and accompaniment parts are not counted,
// as in drawn_key_range. Worked out in one pass over the notes, so once per song and keyboard,
// never per frame.
inline constexpr int kShiftFitCount = 2 * kMaxTransposeSemitones + 1;

struct ShiftFits {
    KeyRange keyboard{kRange88};
    int notes = 0; // notes counted
    // The song's own lowest and highest pitch among them (before any shift); -1 when there are
    // none.
    int lowest = -1;
    int highest = -1;
    std::array<int, kShiftFitCount> outside{}; // index: shift + kMaxTransposeSemitones

    // Notes still outside at `semitones` (0 beyond -12..+12 is not a shift this app has: -1).
    int outside_at(int semitones) const {
        if (semitones < -kMaxTransposeSemitones || semitones > kMaxTransposeSemitones) {
            return -1;
        }
        return outside[static_cast<std::size_t>(semitones + kMaxTransposeSemitones)];
    }
    bool all_fit(int semitones) const { return notes > 0 && outside_at(semitones) == 0; }
};

ShiftFits shift_fits(std::span<const Note> notes, KeyRange keyboard);

// The whole-octave shift to propose for a song, given the transposition now in force: the
// transposition (-12..+12, a whole number of octaves from `current`) that leaves the fewest notes
// outside, when that is fewer than now and the notes outside now are at least
// kOfferMinOutsideNotes and at least kOfferMinOutsideShare of the song's. Equal counts are broken
// by the smaller move from `current`, then the smaller shift, then the lower one: a tie in every
// respect (a song as far under the keyboard as over it) goes down. None when nothing better exists,
// so a song with one stray note never raises a card, however short the song.
inline constexpr int kOfferMinOutsideNotes = 2;
inline constexpr double kOfferMinOutsideShare = 0.05;
std::optional<int> suggest_octave_shift(const ShiftFits& fits, int current);

} // namespace etude
