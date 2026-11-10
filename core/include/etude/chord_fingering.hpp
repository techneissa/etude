// SPDX-License-Identifier: GPL-3.0-or-later
// The fingering the Learn chord card suggests for a chord and its inversion, for triads and seventh
// chords only: the one every teaching method gives (1-3-5 for a triad in the right hand, 5-3-1 in
// the left). A chord with no standard fingering (suspended, sixth, added-ninth, ninth, thirteenth
// and power chords) has none, and the card shows none. The fingers do not depend on the root: the
// standard is the same on black and white keys. Pure.
#pragma once

#include "etude/chord.hpp"

#include <array>
#include <optional>

namespace etude {

inline constexpr int kMostChordFingers = 4;

// The fingers of both hands for the chord's notes from the lowest up (1 thumb .. 5 little finger).
// `count` is how many notes the chord has, 3 or 4.
struct ChordFingering {
    int count = 0;
    std::array<int, kMostChordFingers> right{};
    std::array<int, kMostChordFingers> left{};
};

// The standard fingering of `quality` in inversion `inversion` (0 root position, 1 first, ...);
// none for a chord without one or an inversion the chord does not have.
std::optional<ChordFingering> chord_fingering(ChordQuality quality, int inversion);

} // namespace etude
