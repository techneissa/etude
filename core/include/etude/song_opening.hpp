// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/hand.hpp"
#include "etude/keyboard.hpp"
#include "etude/song.hpp"

#include <cstdint>
#include <vector>

namespace etude {

// A song's opening bars as the library's preview plays them: the notes of its first seconds,
// timed from the first note, and the keys they need.
struct OpeningNote {
    float at = 0.0f;  // seconds from the first note
    float len = 0.0f; // cut where the opening ends
    std::uint8_t key = 60;
    std::uint8_t velocity = 64;
    Hand hand = Hand::Unknown;
};

struct SongOpening {
    std::vector<OpeningNote> notes; // by start
    float length_s = 0.0f;          // the last note's end, at most the seconds asked for
    KeyRange keys{48, 72};          // whole octaves, C to C, at least two, round every note
};

// The notes (no drums) that start within `seconds` of the song's first note.
SongOpening song_opening(const Song& song, double seconds);

} // namespace etude
