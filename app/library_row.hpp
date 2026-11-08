// SPDX-License-Identifier: GPL-3.0-or-later
// The "From your library" row of the Melodies page: library songs sorted into the five steps by what
// the library scan already stores for them (core melody_steps.hpp), and the songs the player added by
// hand (melody_added.hpp) at the step they chose. Built when the library's songs, their ratings or the
// player's added list changed, never from a file: nothing is rated here. One tile stands for a piece
// whatever versions of it the library holds.
#pragma once

#include "library_catalogue.hpp"

#include "etude/melody_added.hpp"
#include "etude/melody_steps.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude::app {

// A step shows at most this many songs the sort chose (the player's own additions all show).
inline constexpr std::size_t kLibraryRowPerStep = 3;

// While the library is being read the row is made at most this often (seconds).
inline constexpr double kLibraryRowEveryS = 1.0;

struct LibraryRowSong {
    std::size_t index = 0; // the catalogue's place for it when the row was built
    std::uint64_t hash = 0;
    int step = 0;          // kMelodyFirstStep to kMelodyLastStep
    bool added = false;    // the player put it here
};

// An added song whose file was edited: it has a new hash and the list should follow it.
struct LibraryRowFollow {
    std::uint64_t old_hash = 0;
    std::uint64_t new_hash = 0;
    std::string path;
};

struct LibraryRow {
    std::vector<LibraryRowSong> songs;          // by step, the player's additions first in each
    std::size_t sorted = 0;                     // pieces that fit a step (or were added), shown or not
    std::vector<std::uint64_t> hidden;          // added hashes the library has no song for now
    std::vector<LibraryRowFollow> followed;     // added songs found by their path under a new hash
};

// What the sort reads of a song of the catalogue.
MelodyStepFacts step_facts(const CatalogueSong& song);

// The step song `song` is suggested for: the player's own choice when it is in `added`, else the
// sort's; kMelodyNoStep for none.
int step_for(const CatalogueSong& song, const MelodyAdded& added);

// Builds the row from the catalogue's songs that are read. An added song is found by its hash, else by
// its path (then it is in `followed`), else it is in `hidden`. A file held twice (one content hash)
// counts once; a piece with versions shows one tile: the version the player added, else the one
// practised most, then the easiest.
void build_library_row(const LibraryCatalogue& catalogue, const MelodyAdded& added, LibraryRow& out);

// The hashes of `added` the library has no song for (neither by hash nor by path).
std::vector<std::uint64_t> hidden_added(const LibraryCatalogue& catalogue, const MelodyAdded& added);

} // namespace etude::app
