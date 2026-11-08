// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/progress_view.hpp"

#include "etude/results_card.hpp"

#include <algorithm>
#include <cmath>
#include <unordered_map>

namespace etude {

namespace {

constexpr std::size_t kNone = static_cast<std::size_t>(-1);

// A run that counts for accuracy, songs, runs and bests-by-accuracy (scored or not).
bool counting(const RunRecord& run, std::int32_t today) {
    return run.song_hash != 0 && run.all.graded >= kMinGradedForBest && run.local_day <= today;
}

// Indices of the counting runs, repeats left out, oldest first.
std::vector<std::size_t> counting_indices(std::span<const RunRecord> runs, std::int32_t today) {
    std::vector<std::size_t> out;
    for (const std::size_t i : unique_run_indices(runs)) {
        if (counting(runs[i], today)) {
            out.push_back(i);
        }
    }
    return out;
}

int accuracy_pct(const RunRecord& run) {
    return static_cast<int>(std::lround(100.0 * run.all.accuracy().value_or(0.0)));
}

// Whether `a`'s accuracy is higher than `b`'s, compared without rounding.
bool more_accurate(const RunRecord& a, const RunRecord& b) {
    return static_cast<std::int64_t>(a.all.hit) * b.all.graded >
           static_cast<std::int64_t>(b.all.hit) * a.all.graded;
}

std::int64_t minutes_of(const std::vector<DayPractice>& days, std::size_t last_n) {
    std::int64_t ms = 0;
    for (std::size_t i = days.size() - std::min(last_n, days.size()); i < days.size(); ++i) {
        ms += days[i].practised_ms;
    }
    return ms / 60'000;
}

// The counting runs of one song, oldest first, and the one that is its best.
struct SongRuns {
    std::vector<std::size_t> runs;
    std::size_t best = kNone;
    bool best_scored = false;
    int scored_runs = 0;
};

// Points rule: the best scored run by points, a tie to the earlier run. A song with no scored run:
// the best by accuracy, a tie to more graded notes, then to the later run.
void note_run(SongRuns& song, std::span<const RunRecord> runs, std::size_t i) {
    const RunRecord& run = runs[i];
    song.runs.push_back(i);
    if (is_scored(run)) {
        ++song.scored_runs;
        if (!song.best_scored || run.score > runs[song.best].score) {
            song.best = i;
            song.best_scored = true;
        }
        return;
    }
    if (song.best_scored) {
        return;
    }
    if (song.best == kNone || more_accurate(run, runs[song.best]) ||
        (!more_accurate(runs[song.best], run) && run.all.graded >= runs[song.best].all.graded)) {
        song.best = i;
    }
}

using SongMap = std::unordered_map<std::uint64_t, SongRuns>;

SongMap group_by_song(std::span<const RunRecord> runs, const std::vector<std::size_t>& indices) {
    SongMap songs;
    for (const std::size_t i : indices) {
        note_run(songs[runs[i].song_hash], runs, i);
    }
    return songs;
}

// The accuracy graph and the tiles that need only this profile's runs.
ProfileProgress profile_tiles(const ProfileRuns& profile, const std::vector<std::size_t>& indices,
                              std::size_t song_count, std::int32_t today, const MedalOptions& medal_options) {
    ProfileProgress out;
    out.profile_id = profile.profile_id;
    out.streak = profile_streak(profile.runs, today);
    const auto days = recent_days(profile.runs, today, kMonthDays);
    out.minutes_today = static_cast<int>(minutes_of(days, 1));
    out.minutes_week = static_cast<int>(minutes_of(days, static_cast<std::size_t>(kWeekDays)));
    out.minutes_month = static_cast<int>(minutes_of(days, static_cast<std::size_t>(kMonthDays)));
    out.songs = static_cast<int>(song_count);
    out.runs = static_cast<int>(indices.size());
    out.medals = evaluate_medals(profile.runs, {today, profile.goal_minutes, profile.created_utc_s,
                                                medal_options.utc_offset_s, medal_options.pieces});
    const std::size_t from =
        indices.size() -
        std::min<std::size_t>(indices.size(), static_cast<std::size_t>(kAccuracyRuns));
    for (std::size_t k = from; k < indices.size(); ++k) {
        const RunRecord& run = profile.runs[indices[k]];
        out.accuracy.push_back({run.local_day, accuracy_pct(run), run.speed_percent});
    }
    return out;
}

// Each profile's best points per song, from the scored counting runs (the boards' rule).
using ScoreMap = std::unordered_map<std::uint64_t, int>;

ScoreMap best_points(const ProfileRuns& profile, const std::vector<std::size_t>& indices) {
    ScoreMap best;
    for (const std::size_t i : indices) {
        const RunRecord& run = profile.runs[i];
        if (is_scored(run)) {
            int& points = best[run.song_hash];
            points = std::max(points, run.score);
        }
    }
    return best;
}

// Songs each profile is first on: the top score of a song with a score, shared when equal (the
// boards give equal scores one rank). Empty counts unless two or more profiles are compared.
std::vector<int> count_firsts(const std::vector<ScoreMap>& points) {
    std::vector<int> firsts(points.size(), 0);
    if (points.size() < 2) {
        return firsts;
    }
    ScoreMap top;
    for (const ScoreMap& one : points) {
        for (const auto& [hash, score] : one) {
            top[hash] = std::max(top[hash], score);
        }
    }
    for (std::size_t p = 0; p < points.size(); ++p) {
        for (const auto& [hash, score] : points[p]) {
            if (score > 0 && score == top[hash]) {
                ++firsts[p];
            }
        }
    }
    return firsts;
}

const BoardRow* row_of(const std::vector<BoardRow>& rows, std::int64_t profile_id) {
    for (const BoardRow& row : rows) {
        if (row.profile_id == profile_id) {
            return &row;
        }
    }
    return nullptr;
}

std::vector<CalendarCell> build_calendar(std::span<const RunRecord> runs, std::int32_t today,
                                         int goal_minutes) {
    const std::int64_t goal_ms =
        std::int64_t{std::clamp(goal_minutes, 0, kMaxDailyGoalMinutes)} * 60'000;
    std::vector<CalendarCell> out;
    for (const DayPractice& day : recent_days(runs, today, kCalendarDays)) {
        out.push_back({day.day, static_cast<int>(day.practised_ms / 60'000),
                       calendar_level(day.practised_ms, goal_ms)});
    }
    return out;
}

// Points of the latest scored runs of a song (accuracy for a song with none), oldest first.
std::vector<int> trend_of(const SongRuns& song, std::span<const RunRecord> runs) {
    std::vector<int> out;
    for (std::size_t k = song.runs.size();
         k-- > 0 && out.size() < static_cast<std::size_t>(kBestTrendRuns);) {
        const RunRecord& run = runs[song.runs[k]];
        if (song.scored_runs == 0) {
            out.push_back(accuracy_pct(run));
        } else if (is_scored(run)) {
            out.push_back(run.score);
        }
    }
    std::reverse(out.begin(), out.end());
    return out;
}

struct RankedSong {
    std::uint64_t hash = 0;
    const SongRuns* song = nullptr;
    int score = 0;
    const std::string* title = nullptr;
};

std::vector<SongBestRow> build_bests(std::span<const ProfileRuns> household, const ProfileRuns& me,
                                     const SongMap& songs, std::int32_t today) {
    std::vector<RankedSong> ranked;
    ranked.reserve(songs.size());
    for (const auto& [hash, song] : songs) {
        const RunRecord& best = me.runs[song.best];
        ranked.push_back({hash, &song, song.best_scored ? best.score : 0,
                          &me.runs[song.runs.back()].song_title});
    }
    const std::size_t keep =
        std::min<std::size_t>(ranked.size(), static_cast<std::size_t>(kBestSongs));
    std::partial_sort(ranked.begin(), ranked.begin() + static_cast<std::ptrdiff_t>(keep),
                      ranked.end(), [](const RankedSong& a, const RankedSong& b) {
                          if (a.score != b.score) {
                              return a.score > b.score;
                          }
                          return *a.title != *b.title ? *a.title < *b.title : a.hash < b.hash;
                      });
    std::vector<SongBestRow> out;
    for (std::size_t k = 0; k < keep; ++k) {
        const RankedSong& r = ranked[k];
        const RunRecord& best = me.runs[r.song->best];
        SongBestRow row;
        row.song_hash = r.hash;
        row.title = *r.title;
        row.score = r.score;
        row.accuracy_pct = accuracy_pct(best);
        row.speed_percent = best.speed_percent;
        row.day = best.local_day;
        row.runs = static_cast<int>(r.song->runs.size());
        row.trend = trend_of(*r.song, me.runs);
        if (r.score > 0) {
            const auto board = song_leaderboard(household, r.hash, Period::AllTime, today);
            const BoardRow* mine = row_of(board, me.profile_id);
            row.rank = mine != nullptr && mine->score > 0 ? mine->rank : 0;
        }
        out.push_back(std::move(row));
    }
    return out;
}

// The hottest bar of a run and the adjacent bars (measure +-1, again and again) the run also named.
PractiseSpot spot_of(const RunRecord& run, const CardBar* bars, int count) {
    PractiseSpot spot;
    spot.song_hash = run.song_hash;
    spot.title = run.song_title;
    spot.first_measure = spot.last_measure = bars[0].measure;
    const auto named = [&](int measure) {
        return std::any_of(bars, bars + count,
                           [&](const CardBar& bar) { return bar.measure == measure; });
    };
    while (named(spot.first_measure - 1)) {
        --spot.first_measure;
    }
    while (named(spot.last_measure + 1)) {
        ++spot.last_measure;
    }
    return spot;
}

// One spot per distinct song among the newest counting runs; a song's newest run is the only one
// read, and a run that names no bar gives no spot.
std::vector<PractiseSpot> build_practise(std::span<const RunRecord> runs,
                                         const std::vector<std::size_t>& indices) {
    std::vector<PractiseSpot> out;
    std::vector<std::uint64_t> seen;
    const std::size_t read =
        std::min<std::size_t>(indices.size(), static_cast<std::size_t>(kPractiseRuns));
    for (std::size_t k = 0; k < read && out.size() < static_cast<std::size_t>(kPractiseSpots);
         ++k) {
        const RunRecord& run = runs[indices[indices.size() - 1 - k]];
        if (std::find(seen.begin(), seen.end(), run.song_hash) != seen.end()) {
            continue;
        }
        seen.push_back(run.song_hash);
        int count = 0;
        const auto bars = worst_bars(run, &count);
        if (count > 0) {
            out.push_back(spot_of(run, bars.data(), count));
        }
    }
    return out;
}

} // namespace

std::uint8_t calendar_level(std::int64_t practised_ms, std::int64_t goal_ms) {
    if (practised_ms <= 0) {
        return 0;
    }
    if (goal_ms <= 0) {
        goal_ms = static_cast<std::int64_t>(kCalendarNoGoalMinutes) * 60'000;
    }
    if (2 * practised_ms < goal_ms) {
        return 1;
    }
    if (practised_ms < goal_ms) {
        return 2;
    }
    return practised_ms < 2 * goal_ms ? 3 : 4;
}

ProgressView build_progress_view(std::span<const ProfileRuns> household, std::int64_t profile_id,
                                 std::int32_t today, int goal_minutes, const MedalOptions& medal_options) {
    ProgressView view;
    view.profile_id = profile_id;
    view.today = today;
    view.household_size = static_cast<int>(household.size());

    const auto overall = overall_leaderboard(household, Period::AllTime, today);
    std::vector<ScoreMap> points;
    std::vector<std::vector<std::size_t>> indices;
    std::vector<SongMap> songs;
    for (const ProfileRuns& profile : household) {
        indices.push_back(counting_indices(profile.runs, today));
        songs.push_back(group_by_song(profile.runs, indices.back()));
        points.push_back(best_points(profile, indices.back()));
    }
    const std::vector<int> firsts = count_firsts(points);

    const ProfileRuns* mine = nullptr;
    std::size_t mine_at = 0;
    for (std::size_t p = 0; p < household.size(); ++p) {
        // The goal of the profile asked for is the one given; the others' are what their settings say.
        ProfileRuns counted = household[p];
        if (counted.profile_id == profile_id) {
            counted.goal_minutes = goal_minutes;
        }
        ProfileProgress line = profile_tiles(counted, indices[p], songs[p].size(), today, medal_options);
        if (const BoardRow* row = row_of(overall, household[p].profile_id);
            row != nullptr && row->songs > 0) {
            line.points = row->score;
            line.rank = row->rank;
        }
        line.firsts = firsts[p];
        if (mine == nullptr && household[p].profile_id == profile_id) {
            mine = &household[p];
            mine_at = p;
            view.me = line;
        }
        view.household.push_back(std::move(line));
    }

    const ProfileRuns none{profile_id, {}};
    const ProfileRuns& me = mine != nullptr ? *mine : none;
    if (mine == nullptr) {
        view.me = profile_tiles(none, {}, 0, today, medal_options);
    }
    view.household_ranked = household_order(view.household);
    view.goal = daily_goal_progress(me.runs, today, goal_minutes);
    view.calendar = build_calendar(me.runs, today, goal_minutes);
    if (mine != nullptr) {
        view.bests = build_bests(household, me, songs[mine_at], today);
        view.practise = build_practise(me.runs, indices[mine_at]);
        if (!indices[mine_at].empty()) {
            const RunRecord& newest = me.runs[indices[mine_at].back()];
            view.last_song_hash = newest.song_hash;
            view.last_song_title = newest.song_title;
        }
    }
    return view;
}

} // namespace etude
