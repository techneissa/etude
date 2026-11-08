// SPDX-License-Identifier: GPL-3.0-or-later
// What a melody asks of each hand, read from the song itself: the lowest and highest key of the
// hand's notes, and how many keys that is. The Melodies tiles show the key count under each hand.
#pragma once

#include "etude/song.hpp"

namespace etude {

struct MelodyHandRange {
    int lowest = -1;  // MIDI note, -1 when the hand has no notes
    int highest = -1;

    bool used() const { return lowest >= 0; }
    // Every key from the lowest to the highest, both ends and the black keys between counted.
    int keys() const { return used() ? highest - lowest + 1 : 0; }
};

struct MelodyShape {
    MelodyHandRange left;
    MelodyHandRange right;
};

// The notes the player plays: not drums and not accompaniment. A note with no hand counts for the
// right hand, as the difficulty rating reads it.
MelodyShape melody_shape(const Song& song);

} // namespace etude
