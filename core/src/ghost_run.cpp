// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/ghost_run.hpp"

#include <cstdlib>

namespace etude {

bool ghost_mode_allowed(RunMode mode) {
    // No default: a new mode (Follow) must be listed here before the build passes.
    switch (mode) {
    case RunMode::Flow:
    case RunMode::Wait:
        return true;
    }
    return false;
}

bool ghost_setup_matches(const GhostSetup& wanted, const GhostSetup& run) {
    if (!ghost_mode_allowed(wanted.mode) || !ghost_mode_allowed(run.mode)) {
        return false;
    }
    if (wanted.mode != run.mode || wanted.hands != run.hands ||
        wanted.transpose_semitones != run.transpose_semitones) {
        return false;
    }
    // |run - wanted| <= wanted * 10%, in whole percents: no float, both ends in.
    const long diff =
        std::labs(static_cast<long>(run.speed_percent) - static_cast<long>(wanted.speed_percent));
    return diff * 100 <= static_cast<long>(wanted.speed_percent) * kGhostSpeedTolerancePercent;
}

bool ghost_comparable(const GhostSetup& wanted, const GhostCandidate& run) {
    return run.complete && ghost_setup_matches(wanted, run.setup);
}

namespace {

// -1 if a is the worse score, 0 if equal, 1 if better: hit / graded compared by cross products.
int compare_accuracy(const RunHand& a, const RunHand& b) {
    const long long left = static_cast<long long>(a.hit) * b.graded;
    const long long right = static_cast<long long>(b.hit) * a.graded;
    return left < right ? -1 : left > right ? 1 : 0;
}

} // namespace

std::optional<std::size_t> best_ghost_run(const GhostSetup& wanted,
                                          std::span<const GhostCandidate> candidates) {
    std::optional<std::size_t> best;
    for (std::size_t i = 0; i < candidates.size(); ++i) {
        const GhostCandidate& c = candidates[i];
        if (c.all.graded <= 0 || !ghost_comparable(wanted, c)) {
            continue;
        }
        if (!best) {
            best = i;
            continue;
        }
        const GhostCandidate& b = candidates[*best];
        const int order = compare_accuracy(c.all, b.all);
        if (order > 0 || (order == 0 && (c.all.graded > b.all.graded ||
                                         (c.all.graded == b.all.graded && c.run_id > b.run_id)))) {
            best = i;
        }
    }
    return best;
}

} // namespace etude
