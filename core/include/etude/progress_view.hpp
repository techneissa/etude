// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/medal_eval.hpp"
#include "etude/leaderboard.hpp"
#include "etude/progress.hpp"
#include "etude/run_record.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

namespace etude {

// Everything the Progress page shows for one profile, and one line of every profile for its
// Household view, worked out from the runs in one pass. Pure: the app's history thread builds it
// from the database, so the frame thread only reads the answer. Built from the functions of
// progress.hpp, leaderboard.hpp, medal_eval.hpp and results_card.hpp; nothing here reads a clock.
// Runs from before scores count in time, streak, calendar, accuracy and bests-by-accuracy, but not
// in points, ranks or badges (leaderboard.hpp's rule).

inline constexpr int kCalendarWeeks = 34;
inline constexpr int kCalendarDays = kCalendarWeeks * 7;
inline constexpr int kAccuracyRuns = 30;  // the graph's points: the latest counting runs
inline constexpr int kBestSongs = 8;      // songs in "Best per song" (a screen shows what fits)
inline constexpr int kBestTrendRuns = 7;  // points in a song's small trend line
inline constexpr int kPractiseSpots = 3;  // "Practise today" items
inline constexpr int kPractiseRuns = 12;  // recent runs read for them

// progress.hpp's Streak and DailyGoalProgress carry no comparison; the view's `==` needs one.
inline bool operator==(const Streak& a, const Streak& b) {
    return a.current == b.current && a.longest == b.longest && a.today_done == b.today_done &&
           a.at_risk == b.at_risk && a.freezes == b.freezes && a.frozen_days == b.frozen_days;
}
inline bool operator==(const DailyGoalProgress& a, const DailyGoalProgress& b) {
    return a.goal_minutes == b.goal_minutes && a.has_goal == b.has_goal &&
           a.minutes_today == b.minutes_today && a.fraction == b.fraction && a.met == b.met;
}

// The calendar's shades, as on GitHub: 0 nothing, then kCalendarLevels steps by the minutes played
// that day against the daily goal: 1 under half of it (a little practice), 2 under the goal, 3 the
// goal reached, 4 twice the goal or more (a long session). With no goal the steps are those of a
// goal of kCalendarNoGoalMinutes.
inline constexpr int kCalendarLevels = 4;
inline constexpr int kCalendarNoGoalMinutes = 20;
std::uint8_t calendar_level(std::int64_t practised_ms, std::int64_t goal_ms);

// One day of the calendar: the minutes practised and its level (calendar_level).
struct CalendarCell {
    std::int32_t day = 0;
    int minutes = 0; // rounded down
    std::uint8_t level = 0;
    friend bool operator==(const CalendarCell&, const CalendarCell&) = default;
};

// A point of the accuracy and speed graph: one counting run (at least kMinGradedForBest graded
// notes), oldest first.
struct AccuracyPoint {
    std::int32_t day = 0;
    int accuracy_pct = 0; // 0..100 of the practised notes
    int speed_percent = 100;
    friend bool operator==(const AccuracyPoint&, const AccuracyPoint&) = default;
};

// A song's best row: the best run by points (a tie goes to the earlier run), its accuracy and
// speed, how many counting runs the song has, the points of its latest runs for a small line, and
// where this profile stands on the song among the household.
struct SongBestRow {
    std::uint64_t song_hash = 0;
    std::string title;
    int score = 0;
    int accuracy_pct = 0;
    int speed_percent = 100;
    std::int32_t day = 0;
    int runs = 0;
    std::vector<int> trend; // the points of the latest runs (at most kBestTrendRuns), oldest first
    int rank = 0;           // 1 is the best of the household (all time); 0 when it has no score
    friend bool operator==(const SongBestRow&, const SongBestRow&) = default;
};

// A bar or a few adjacent bars to loop, from the trouble bars of a recent run
// (results_card.hpp worst_bars): the hottest bar, grown over the neighbours the run also named.
struct PractiseSpot {
    std::uint64_t song_hash = 0;
    std::string title;
    int first_measure = 1; // Song bar numbers (a pickup bar is 0), both ends included
    int last_measure = 1;
    friend bool operator==(const PractiseSpot&, const PractiseSpot&) = default;
};

// One person's numbers: the four tiles of the page, and a line of the Household view.
struct ProfileProgress {
    std::int64_t profile_id = 0;
    Streak streak;                 // any practice counts
    int minutes_week = 0;          // today and the six days before it
    int minutes_month = 0;         // today and the 29 days before it
    int minutes_today = 0;
    int songs = 0;                 // songs with at least one counting run
    int runs = 0;                  // counting runs
    int points = 0;                // the overall board's score: the sum of the best points per song
    int rank = 0;                  // on the overall board, all time; 0 with no scored run
    int firsts = 0;                // songs this profile is first on (a household of two or more)
    MedalSet medals; // the tiers of every achievement; title() is the overall title
    std::vector<AccuracyPoint> accuracy; // the latest kAccuracyRuns counting runs
    friend bool operator==(const ProfileProgress&, const ProfileProgress&) = default;
};

struct ProgressView {
    std::int64_t profile_id = 0; // whose page it is
    std::int32_t today = 0;
    int household_size = 0;      // profiles with a line in `household`
    ProfileProgress me;          // the same as the profile's line in `household`
    DailyGoalProgress goal;      // of `me`, against the goal given
    std::vector<CalendarCell> calendar; // kCalendarDays days ending today, oldest first
    std::vector<SongBestRow> bests;     // by points, best first, at most kBestSongs
    std::vector<PractiseSpot> practise; // at most kPractiseSpots, the newest run's song first
    // The song of the newest counting run, to carry on with (hash 0: none).
    std::uint64_t last_song_hash = 0;
    std::string last_song_title;
    std::vector<ProfileProgress> household; // every profile given, in the order given
    // `household_order(household)`, worked out with the view so the Household screen does not each frame.
    std::vector<std::size_t> household_ranked;
    friend bool operator==(const ProgressView&, const ProgressView&) = default;
};

// The view of `profile_id` among `household` (every profile's runs, each oldest first as the
// history keeps them). `goal_minutes` is the profile's daily goal (0: none). A profile id that
// `household` does not hold gives an empty view of that id. Runs dated after `today` are left out.
// What the medals need that is the same for every profile: the offset the local days are counted at and
// which songs are versions of one piece.
struct MedalOptions {
    std::int32_t utc_offset_s = 0;
    const PieceMap* pieces = nullptr;
};

ProgressView build_progress_view(std::span<const ProfileRuns> household, std::int64_t profile_id,
                                 std::int32_t today, int goal_minutes, const MedalOptions& medal_options = {});

// The order the Household view lists `lines` in: best rank first (equal ranks and those with no
// scored run, rank 0, which come last, each in the order given). Indices into `lines`.
inline std::vector<std::size_t> household_order(std::span<const ProfileProgress> lines) {
    std::vector<std::size_t> order(lines.size());
    for (std::size_t i = 0; i < order.size(); ++i) {
        order[i] = i;
    }
    const auto key = [&](std::size_t i) { return lines[i].rank > 0 ? lines[i].rank : (1 << 20); };
    std::stable_sort(order.begin(), order.end(),
                     [&](std::size_t a, std::size_t b) { return key(a) < key(b); });
    return order;
}

// The weekday of a day key, Monday 0 .. Sunday 6 (1970-01-01 was a Thursday).
inline int weekday_monday0(std::int32_t day) {
    const int d = static_cast<int>((day + 3) % 7);
    return d < 0 ? d + 7 : d;
}

} // namespace etude
