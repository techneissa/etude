// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_record.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>

namespace etude {

// Which past run can stand as the "ghost" of the run being played: the best earlier run of the same
// song at a setup the player would call the same. The rule is here, pure, so the store and the
// screen cannot disagree about it.
//
// A run is comparable when
// - it is complete (every practised note judged once: RunEvents::complete) and its events were
// kept;
// - it has the same mode, hands and transposition; a transposed run presses other keys, so it is
//   not the same exercise;
// - its speed is within kGhostSpeedTolerancePercent of the speed now, measured against the speed
//   now: 100% accepts 90..110, 80% accepts 72..88, 50% accepts 45..55. Both ends are in.
// A Follow-mode run (the player follows the song's own playing instead of being graded on timing)
// never provides a ghost whatever else matches: ghost_mode_allowed() is the one place that says so,
// and a mode added to RunMode without being listed there does not compile.

inline constexpr int kGhostSpeedTolerancePercent = 10;

// The setup a run was played in, or the one about to start.
struct GhostSetup {
    RunMode mode = RunMode::Flow;
    HandFilter hands = HandFilter::Both;
    int speed_percent = 100;
    int transpose_semitones = 0;

    friend bool operator==(const GhostSetup&, const GhostSetup&) = default;
};

// A finished run that is a candidate: its setup, whether its events are there and complete, and the
// score that ranks it.
struct GhostCandidate {
    std::int64_t run_id = 0;
    GhostSetup setup;
    bool complete = false;
    RunHand all; // the practised notes: graded and hit
};

// Whether a run of this mode may ever be a ghost.
bool ghost_mode_allowed(RunMode mode);

// Whether `run` is a comparable setup to `wanted` (the setup only: see ghost_comparable).
bool ghost_setup_matches(const GhostSetup& wanted, const GhostSetup& run);

// Whether `run` can be the ghost for a run played in `wanted`: the setup matches and it is
// complete.
bool ghost_comparable(const GhostSetup& wanted, const GhostCandidate& run);

// The best comparable candidate: the highest accuracy (hit / graded, compared exactly), a tie going
// to more graded notes, then to the later run (the larger run_id). Runs that graded nothing never
// win. Nullopt when none is comparable. The index is into `candidates`.
std::optional<std::size_t> best_ghost_run(const GhostSetup& wanted,
                                          std::span<const GhostCandidate> candidates);

} // namespace etude
