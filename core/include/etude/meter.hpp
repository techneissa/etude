// SPDX-License-Identifier: GPL-3.0-or-later
// The beat of a time signature as a player feels it. In a simple meter (4/4, 3/4, 2/2, 3/8) the
// beat is one note of the denominator. In a compound meter (6/8, 9/8, 12/8: eighths in threes) it
// is three of them, a dotted quarter, and the bar has numerator / 3 beats. The metronome clicks,
// the count-in counts and the tempo chip shows these beats.
#pragma once

namespace etude {

inline constexpr bool is_compound_meter(int numerator, int denominator) {
    return denominator == 8 && numerator >= 6 && numerator % 3 == 0;
}

// Beats in a bar of the meter.
inline constexpr int beats_per_bar(int numerator, int denominator) {
    const int n = numerator < 1 ? 1 : numerator;
    return is_compound_meter(numerator, denominator) ? n / 3 : n;
}

// Ticks in one beat.
inline constexpr double ticks_per_beat(double ticks_per_quarter, int numerator, int denominator) {
    const double note = 4.0 * ticks_per_quarter / (denominator < 1 ? 4 : denominator);
    return is_compound_meter(numerator, denominator) ? 3.0 * note : note;
}

// The length of the beat the tempo chip counts, in quarter notes: 1.5 (a dotted quarter) in a
// compound meter, 1 (a quarter) in any other.
inline constexpr double chip_beat_quarters(int numerator, int denominator) {
    return is_compound_meter(numerator, denominator) ? 1.5 : 1.0;
}

} // namespace etude
