// SPDX-License-Identifier: GPL-3.0-or-later
// How far along a melody is, from the best score the practice history holds for its file. The same
// rule as the library's: a best accuracy of `kMasteredPermille` or more passes the piece (the
// library calls it mastered), and the best counts only from a run of `kMinGradedForBest` graded
// notes (`summarise_plays`). A path's next piece is its first one not passed.
#pragma once

#include "etude/color.hpp"
#include "etude/library_browse.hpp"

#include <span>

namespace etude {

inline constexpr int kMelodyPassPermille = kMasteredPermille;

enum class MelodyMark : unsigned char {
    New,    // no scored run yet
    Started, // a best below the pass mark
    Passed,
};

// `best_permille` is the best accuracy in thousandths, -1 for none.
MelodyMark melody_mark(int best_permille);

// How much of the way to the pass mark the best score is, 0 to 1 (0 for none, 1 once passed): what
// the tile's border and the ring round Play fill to.
float melody_share(int best_permille);

// The colour of that fill: yellow where it starts, orange half way, green once passed.
RGBA melody_fill_colour(float share);

struct MelodyPathProgress {
    int passed = 0;
    int total = 0;
    int next = -1;       // the place in the path of the first piece not passed, -1 when all are
    bool started = false; // any piece has a score
};

// A path from the best scores of its pieces, in the order they are played.
MelodyPathProgress melody_path_progress(std::span<const int> best_permille);

} // namespace etude
