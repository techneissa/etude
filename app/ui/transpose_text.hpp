// SPDX-License-Identifier: GPL-3.0-or-later
// The words of the transposition control and the octave-shift card, with no ImGui in them. Each writes into the caller's buffer
// (never more than `n` bytes, always terminated): the card draws every frame and allocates
// nothing. The minus is U+2212, as the speed readout's is.
#pragma once

#include <cstddef>
#include <cstdio>

namespace etude::app::ui::text {

// Room for one piece (a shift, an octave move, a count) and for two of them with the dot between:
// the largest an int prints is 11 characters, which GCC counts against the buffer it is given.
inline constexpr std::size_t kShort = 24;
inline constexpr std::size_t kLong = 64;

inline constexpr const char* kMinusSign = "\xE2\x88\x92";

// A shift in semitones: "0", "+2", "-5" (with the true minus).
inline void shift(char* out, std::size_t n, int semitones) {
    if (semitones == 0) {
        std::snprintf(out, n, "0");
    } else {
        std::snprintf(out, n, "%s%d", semitones < 0 ? kMinusSign : "+",
                      semitones < 0 ? -semitones : semitones);
    }
}

// A whole-octave move: "0", "+1 oct", "-2 oct".
inline void octaves(char* out, std::size_t n, int count) {
    if (count == 0) {
        std::snprintf(out, n, "0");
    } else {
        std::snprintf(out, n, "%s%d oct", count < 0 ? kMinusSign : "+", count < 0 ? -count : count);
    }
}

// What a shift leaves off the keyboard: "all fit", or "14 outside".
inline void outside(char* out, std::size_t n, int count) {
    if (count <= 0) {
        std::snprintf(out, n, "all fit");
    } else {
        std::snprintf(out, n, "%d outside", count);
    }
}

// The card for a keyboard whose own octave button shifted what it sends: "Keyboard shifted +1
// oct?".
inline void keyboard_shifted(char* out, std::size_t n, int octave_count) {
    char move[kShort];
    octaves(move, sizeof move, octave_count);
    std::snprintf(out, n, "Keyboard shifted %s?", move);
}

// The two together, as the toast and the screen reader say it: "-1 oct · all fit", "0 · 14
// outside".
inline void option(char* out, std::size_t n, int octave_count, int outside_count) {
    char move[kShort];
    char left[kShort];
    octaves(move, sizeof move, octave_count);
    outside(left, sizeof left, outside_count);
    std::snprintf(out, n, "%s \xC2\xB7 %s", move, left);
}

} // namespace etude::app::ui::text
