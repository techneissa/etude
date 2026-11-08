// SPDX-License-Identifier: GPL-3.0-or-later
// The difficulty-weighted score of a run: the plain score (notes hit over notes graded) with each
// bar counted by how hard it is, so a clean pass through hard bars counts more and a slip in an easy
// bar costs more than one in a hard bar. Same scale as the plain score (a share 0..1), so the two
// compare directly. Pure values; the app builds it once when a run ends.
#pragma once

#include "etude/difficulty.hpp"
#include "etude/run_record.hpp"

#include <optional>

namespace etude {

// What one bar of the run is worth: its rating in stars, 1..5, so a five-star bar counts five times
// a one-star bar. A bar the rating has no entry for (none of the run's bars when the song is not
// rated) counts as the mean of the run's rated bars, so it moves the score neither way.
//
//   weighted = sum(weight * hit) / sum(weight * graded), over the run's bars
//
// With every bar of one weight this is exactly hit / graded, the plain score. Empty when the run
// graded nothing in the bars it lists, or when none of them has a rating.
std::optional<double> weighted_accuracy(const RunRecord& run, const SongDifficulty& difficulty);

} // namespace etude
