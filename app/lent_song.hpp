// SPDX-License-Identifier: GPL-3.0-or-later
// The song a SongController has open, lent to the hand split's worker for as long as the worker
// takes to copy it. The controller keeps no second copy of the song for the next split: the worker
// copies the open one, puts the hands the file gave back on the copy and splits that. Whoever
// writes the song (a load, an unload, a split taken over) holds `mutex`, and so does a copy; the
// frame thread only reads the song, as a copy does, so its reads need no lock. std only.
#pragma once

#include "etude/song.hpp"

#include <cstdint>
#include <mutex>

namespace etude::app {

struct LentSong {
    mutable std::mutex mutex;
    const Song* song = nullptr;   // the controller's open song; null when none
    std::uint64_t generation = 0; // up by one with every load and unload, written under `mutex`
};

} // namespace etude::app
