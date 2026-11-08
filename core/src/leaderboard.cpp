// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/leaderboard.hpp"

#include <algorithm>
#include <unordered_map>

namespace etude {

namespace {

bool counts(const RunRecord& run) {
    return is_scored(run) && run.song_hash != 0 && run.all.graded >= kMinGradedForBest;
}

// The counting runs of a profile in the period, repeats left out.
template <typename Visit>
void each_counting_run(std::span<const RunRecord> runs, Period period, std::int32_t today,
                       Visit visit) {
    for (const std::size_t i : unique_run_indices(runs)) {
        const RunRecord& run = runs[i];
        if (counts(run) && run.local_day <= today && in_period(run.local_day, period, today)) {
            visit(i, run);
        }
    }
}

// Orders the rows and gives the ranks.
void rank_rows(std::vector<BoardRow>& rows) {
    std::stable_sort(rows.begin(), rows.end(), [](const BoardRow& a, const BoardRow& b) {
        if (a.score != b.score) {
            return a.score > b.score;
        }
        if (a.runs == 0 || b.runs == 0) {
            return a.runs > b.runs;
        }
        return a.day != b.day ? a.day < b.day : a.profile_id < b.profile_id;
    });
    for (std::size_t i = 0; i < rows.size(); ++i) {
        rows[i].rank = i > 0 && rows[i].score == rows[i - 1].score ? rows[i - 1].rank
                                                                   : static_cast<int>(i) + 1;
    }
}

} // namespace

std::int32_t period_first_day(Period period, std::int32_t today) {
    switch (period) {
    case Period::Week:
        return today - (kWeekDays - 1);
    case Period::Month:
        return today - (kMonthDays - 1);
    case Period::AllTime:
        break;
    }
    return std::numeric_limits<std::int32_t>::min();
}

bool in_period(std::int32_t day, Period period, std::int32_t today) {
    return day >= period_first_day(period, today) && day <= today;
}

std::vector<BoardRow> song_leaderboard(std::span<const ProfileRuns> profiles,
                                       std::uint64_t song_hash, Period period,
                                       std::int32_t today) {
    std::vector<BoardRow> rows;
    for (const ProfileRuns& profile : profiles) {
        BoardRow row;
        row.profile_id = profile.profile_id;
        each_counting_run(profile.runs, period, today, [&](std::size_t, const RunRecord& run) {
            if (run.song_hash != song_hash) {
                return;
            }
            const int points = run.score;
            ++row.runs;
            if (points > row.score) { // a tie keeps the earlier run
                row.score = points;
                row.day = run.local_day;
                row.songs = 1;
            }
        });
        rows.push_back(row);
    }
    rank_rows(rows);
    return rows;
}

std::vector<BoardRow> overall_leaderboard(std::span<const ProfileRuns> profiles, Period period,
                                          std::int32_t today) {
    std::vector<BoardRow> rows;
    for (const ProfileRuns& profile : profiles) {
        struct Best {
            int score = 0;
            std::int32_t day = 0;
        };
        std::unordered_map<std::uint64_t, Best> best;
        BoardRow row;
        row.profile_id = profile.profile_id;
        each_counting_run(profile.runs, period, today, [&](std::size_t, const RunRecord& run) {
            ++row.runs;
            Best& b = best[run.song_hash];
            const int points = run.score;
            if (points > b.score) {
                b = {points, run.local_day};
            }
        });
        for (const auto& [hash, b] : best) {
            row.score += b.score;
            row.day = row.songs == 0 ? b.day : std::max(row.day, b.day); // the day it was reached
            ++row.songs;
        }
        rows.push_back(row);
    }
    rank_rows(rows);
    return rows;
}

PersonalBest personal_best(std::span<const RunRecord> runs, std::uint64_t song_hash,
                           std::int32_t today, Period period) {
    PersonalBest best;
    each_counting_run(runs, period, today, [&](std::size_t i, const RunRecord& run) {
        if (run.song_hash != song_hash) {
            return;
        }
        ++best.runs;
        const int points = run.score;
        if (!best.found || points > best.score) {
            best.found = true;
            best.score = points;
            best.day = run.local_day;
            best.index = i;
        }
    });
    return best;
}

ScoreChange compare_score(std::span<const RunRecord> history, const RunRecord& current) {
    ScoreChange change;
    if (!counts(current)) {
        return change;
    }
    change.score = current.score;
    const RunRecord* last = nullptr;
    int best_before = 0;
    for (std::size_t i = history.size(); i-- > 0;) {
        const RunRecord& r = history[i];
        if (r.song_hash != current.song_hash || !counts(r) || same_run(r, current)) {
            continue;
        }
        if (last == nullptr) {
            last = &r;
        }
        best_before = std::max(best_before, r.score);
    }
    change.personal_best = change.score > best_before;
    if (last == nullptr) {
        change.vs = ScoreVs::First;
        return change;
    }
    change.last_score = last->score;
    change.delta = change.score - change.last_score;
    change.vs = change.delta > 0 ? ScoreVs::Beat : change.delta < 0 ? ScoreVs::Behind : ScoreVs::Tied;
    return change;
}

} // namespace etude
