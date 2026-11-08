// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song_board.hpp"

#include "etude/progress.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

bool counts_for_board(const RunRecord& run, std::uint64_t song_hash, std::int32_t today) {
    return run.song_hash == song_hash && is_scored(run) && run.all.graded >= kMinGradedForBest &&
           run.local_day <= today;
}

SongBoardRow fill_row(const BoardRow& line, std::span<const RunRecord> runs,
                      std::uint64_t song_hash, Period period, std::int32_t today) {
    SongBoardRow row;
    row.profile_id = line.profile_id;
    row.rank = line.rank;
    row.score = line.score;
    row.runs = line.runs;
    if (line.runs == 0) {
        return row;
    }
    const PersonalBest best = personal_best(runs, song_hash, today, period);
    if (best.found) {
        const RunRecord& run = runs[best.index];
        row.accuracy_pct = static_cast<int>(std::lround(100.0 * run.all.accuracy().value_or(0.0)));
        row.speed_percent = run.speed_percent;
        row.day = run.local_day;
    }
    return row;
}

const ProfileRuns* find_profile(std::span<const ProfileRuns> household, std::int64_t id) {
    for (const ProfileRuns& p : household) {
        if (p.profile_id == id) {
            return &p;
        }
    }
    return nullptr;
}

// The profile's counting runs of the song, oldest first, as the chart reads them, and the index of
// the one the page is about (-1 for none: a `run` that does not count, or no run at all). With a
// `run` that is not in the history yet the list ends with it; without one the page is about the
// latest counting run.
RunsChart chart_of(const ProfileRuns& me, std::uint64_t song_hash, std::int32_t today,
                   const RunRecord* run) {
    std::vector<ChartRun> counting;
    int current = -1;
    for (const std::size_t i : unique_run_indices(me.runs)) {
        const RunRecord& r = me.runs[i];
        if (counts_for_board(r, song_hash, today)) {
            counting.push_back({r.score, r.local_day, false});
            if (run != nullptr && r == *run) {
                current = static_cast<int>(counting.size()) - 1;
            }
        }
    }
    if (run == nullptr) {
        current = static_cast<int>(counting.size()) - 1;
    } else if (current < 0 && counts_for_board(*run, song_hash, today)) {
        counting.push_back({run->score, run->local_day, false});
        current = static_cast<int>(counting.size()) - 1;
    }
    return build_runs_chart(counting, current);
}

// The active profile's chart and the comparison of the run (its latest, or the one given) with
// the one before.
void fill_runs(SongBoard& board, const ProfileRuns& me, std::uint64_t song_hash, std::int32_t today,
               const RunRecord* run) {
    board.chart = chart_of(me, song_hash, today, run);
    // The best the profile has done on the song, whatever the card's run was.
    board.personal_best = personal_best(me.runs, song_hash, today).score;
    const RunRecord* about = run;
    if (about == nullptr) {
        for (const std::size_t i : unique_run_indices(me.runs)) {
            if (counts_for_board(me.runs[i], song_hash, today)) {
                about = &me.runs[i];
            }
        }
    }
    if (about == nullptr || !counts_for_board(*about, song_hash, today)) {
        return;
    }
    // Against the runs before it, though a later one may be in the history.
    std::span<const RunRecord> before = me.runs;
    const auto at = std::find(me.runs.begin(), me.runs.end(), *about);
    if (at != me.runs.end()) {
        before = me.runs.first(static_cast<std::size_t>(at - me.runs.begin()));
    }
    const ScoreChange change = compare_score(before, *about);
    board.has_run = true;
    board.run_score = about->score;
    board.personal_best = std::max(board.personal_best, about->score);
    board.is_personal_best = change.personal_best;
    board.vs = change.vs;
    board.last_score = change.last_score;
    board.delta = change.delta;
}

// Every profile's runs of the song, nothing else: what the boards read.
struct SongRuns {
    std::vector<std::vector<RunRecord>> lists;
    std::vector<ProfileRuns> profiles;
};

SongRuns runs_of_song(std::span<const ProfileRuns> household, std::uint64_t song_hash) {
    SongRuns out;
    out.lists.reserve(household.size());
    for (const ProfileRuns& p : household) {
        std::vector<RunRecord>& list = out.lists.emplace_back();
        for (const RunRecord& r : p.runs) {
            if (r.song_hash == song_hash) {
                list.push_back(r);
            }
        }
    }
    for (std::size_t i = 0; i < household.size(); ++i) {
        out.profiles.push_back({household[i].profile_id, out.lists[i]});
    }
    return out;
}

SongBoard board_of(std::span<const ProfileRuns> mine, std::uint64_t song_hash, Period period,
                   std::int32_t today, std::int64_t active_profile, const RunRecord* run) {
    SongBoard board;
    for (const BoardRow& line : song_leaderboard(mine, song_hash, period, today)) {
        const ProfileRuns* p = find_profile(mine, line.profile_id);
        board.rows.push_back(fill_row(line, p != nullptr ? p->runs : std::span<const RunRecord>(),
                                      song_hash, period, today));
    }
    if (const ProfileRuns* me = find_profile(mine, active_profile)) {
        fill_runs(board, *me, song_hash, today, run);
    }
    return board;
}

} // namespace

SongBoard build_song_board(std::span<const ProfileRuns> household, std::uint64_t song_hash,
                           Period period, std::int32_t today, std::int64_t active_profile,
                           const RunRecord* run) {
    const SongRuns mine = runs_of_song(household, song_hash);
    return board_of(mine.profiles, song_hash, period, today, active_profile, run);
}

std::array<SongBoard, 3> build_song_boards(std::span<const ProfileRuns> household,
                                           std::uint64_t song_hash, std::int32_t today,
                                           std::int64_t active_profile, const RunRecord* run) {
    const SongRuns mine = runs_of_song(household, song_hash);
    std::array<SongBoard, 3> out;
    for (const Period period : {Period::Week, Period::Month, Period::AllTime}) {
        out[static_cast<std::size_t>(period)] =
            board_of(mine.profiles, song_hash, period, today, active_profile, run);
    }
    return out;
}

} // namespace etude
