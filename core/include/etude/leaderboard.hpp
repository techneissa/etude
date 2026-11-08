// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/progress.hpp"
#include "etude/run_record.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

namespace etude {

// Scores over many runs: the household leaderboard per song and overall, a person's best and
// whether a run beat the last one, and the badges. Pure functions over lists of RunRecords (the
// run history of each profile, oldest first), like progress.hpp: nothing here reads a clock, a
// file or a database; `today` is a local day key the caller supplies, and runs dated after it
// (saved while the clock ran ahead) are left out.
//
// A run counts when it was scored (a run from before scores stays in the history, the per-song
// bests and the progress, but is not on a board and earns no badge: a fresh start), graded at least
// kMinGradedForBest notes (the rule of the per-song bests: a run stopped after two notes is not an
// achievement), has a song, and is not a repeat of an earlier one. Its points are RunRecord::score.

enum class Period : std::uint8_t {
    Week,    // today and the six days before it
    Month,   // today and the 29 days before it
    AllTime,
};

inline constexpr int kWeekDays = 7;
inline constexpr int kMonthDays = 30;

// The first local day a period holds, for `today`; AllTime reaches back as far as days go.
std::int32_t period_first_day(Period period, std::int32_t today);
// Whether a run dated `day` is in the period ending `today`.
bool in_period(std::int32_t day, Period period, std::int32_t today);

// Whether the run was scored when it ended (covered_percent is set for every run that graded a
// note, even one whose points are 0). Boards and badges count only these.
inline bool is_scored(const RunRecord& run) { return run.covered_percent > 0; }

// One profile's runs.
struct ProfileRuns {
    std::int64_t profile_id = 0;
    std::span<const RunRecord> runs;
    // What the medals need of the person besides the runs: when the profile was made (UTC seconds,
    // 0 when not known) and the daily goal they set (0: none).
    std::int64_t created_utc_s = 0;
    int goal_minutes = 0;
};

// A line of a leaderboard.
struct BoardRow {
    std::int64_t profile_id = 0;
    int score = 0;      // per song: the best run's points; overall: the sum of the best points of each song
    int songs = 0;      // overall: the songs that count (per song: 1 with a score, else 0)
    int runs = 0;       // counting runs in the period
    std::int32_t day = 0; // the day of the best run (overall: of the latest song best)
    int rank = 0;       // 1 for the top; equal scores share a rank
};

// Every profile listed, best first: by points, then the earlier day of the best run, then the
// lower profile id. A profile with nothing in the period is listed last with 0.
std::vector<BoardRow> song_leaderboard(std::span<const ProfileRuns> profiles,
                                       std::uint64_t song_hash, Period period,
                                       std::int32_t today);
std::vector<BoardRow> overall_leaderboard(std::span<const ProfileRuns> profiles, Period period,
                                          std::int32_t today);

// A person's best run of a song in a period: the highest points, a tie going to the earlier run.
struct PersonalBest {
    bool found = false;
    int score = 0;
    std::int32_t day = 0;
    std::size_t index = 0; // into the span given
    int runs = 0;          // counting runs of the song in the period
};
PersonalBest personal_best(std::span<const RunRecord> runs, std::uint64_t song_hash,
                           std::int32_t today, Period period = Period::AllTime);

enum class ScoreVs : std::uint8_t {
    NotCounted, // too few notes graded, or no song: no comparison
    First,      // the first counting run of the song
    Beat,       // more points than the last run
    Tied,
    Behind,
};

// How a run compares with the person's last run of the same song, and with their best.
struct ScoreChange {
    ScoreVs vs = ScoreVs::NotCounted;
    int score = 0;
    int last_score = 0; // 0 when there was no earlier run
    int delta = 0;      // score - last_score
    bool personal_best = false; // more points than every earlier run of the song (or the first)
};

// `history` may already hold `current` (skipped as a repeat of itself) or not. The last run is the
// latest earlier counting run of the song, at any speed and hands: the points already make them
// comparable.
ScoreChange compare_score(std::span<const RunRecord> history, const RunRecord& current);

} // namespace etude
