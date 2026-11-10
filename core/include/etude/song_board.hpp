// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/leaderboard.hpp"
#include "etude/runs_chart.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace etude {

// What the "Board" tab of a run's summary page shows for one song: the household's best run of it
// in a period, one line each, and the active profile's own runs of it. Pure, built from the runs of
// every profile (each oldest first) like leaderboard.hpp, which it uses for the order, the ranks and
// the counting rule; nothing here reads a clock.

// One line of the table: a profile's best run of the song in the period. A profile with no run
// in the period has score 0, no accuracy (-1), no speed (0) and no day, and is listed last.
struct SongBoardRow {
    std::int64_t profile_id = 0;
    int rank = 0;           // 1 for the top; equal points share a rank
    int score = 0;          // the best run's points
    int accuracy_pct = -1;  // of that run, 0..100; -1 for none
    int speed_percent = 0;  // of that run; 0 for none
    std::int32_t day = 0;   // the day of that run
    int runs = 0;           // counting runs of the song in the period
    friend bool operator==(const SongBoardRow&, const SongBoardRow&) = default;
};

struct SongBoard {
    std::vector<SongBoardRow> rows; // every profile given, best first
    // The active profile's best runs of the song (any period) and where the page's run stands
    // among them.
    RunsChart chart;
    bool has_run = false;      // the active profile has a counting scored run of the song
    int run_score = 0;         // the points of the latest one
    int personal_best = 0;     // the active profile's best points on the song, all time
    bool is_personal_best = false; // the latest run is more than every one before it
    ScoreVs vs = ScoreVs::NotCounted; // the latest run against the one before it
    int last_score = 0;        // the points of the one before it (0 when none)
    int delta = 0;             // run_score - last_score
    friend bool operator==(const SongBoard&, const SongBoard&) = default;
};

// Week, Month and All time (the order of `Period`) in one pass: the runs of the song are picked out
// of every profile's list once, so the cost is that of the song's runs and not of the history.
std::array<SongBoard, 3> build_song_boards(std::span<const ProfileRuns> household,
                                           std::uint64_t song_hash, std::int32_t today,
                                           std::int64_t active_profile,
                                           const RunRecord* run = nullptr);

} // namespace etude
