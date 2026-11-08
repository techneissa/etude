// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/weighted_score.hpp"

#include <algorithm>

namespace etude {

std::optional<double> weighted_accuracy(const RunRecord& run, const SongDifficulty& difficulty) {
    double rated_weight = 0.0;
    int rated_bars = 0;
    for (const RunBar& bar : run.measures) {
        const float stars = difficulty.bar_at(bar.measure);
        if (bar.graded > 0 && stars > 0.0f) {
            rated_weight += static_cast<double>(stars);
            ++rated_bars;
        }
    }
    if (rated_bars == 0) {
        return std::nullopt;
    }
    const double unrated = rated_weight / rated_bars;

    double hit = 0.0;
    double graded = 0.0;
    for (const RunBar& bar : run.measures) {
        if (bar.graded <= 0) {
            continue;
        }
        const float stars = difficulty.bar_at(bar.measure);
        const double weight = stars > 0.0f ? static_cast<double>(stars) : unrated;
        const int bar_hit = std::clamp(bar.graded - bar.missed, 0, bar.graded);
        hit += weight * bar_hit;
        graded += weight * bar.graded;
    }
    if (!(graded > 0.0)) {
        return std::nullopt;
    }
    return hit / graded;
}

} // namespace etude
