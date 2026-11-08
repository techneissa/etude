// SPDX-License-Identifier: GPL-3.0-or-later
// The Progress page's numbers worked out from the runs (progress_view.hpp).
#include "etude/progress_view.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace etude;

namespace {

constexpr std::int32_t kToday = 1000;

// A run of `song` on `day`. `score` 0 is a run from before scores (covered_percent 0 too).
RunRecord run(std::uint64_t song, std::int32_t day, int score, double minutes = 5.0,
              int graded = 20, int hit = -1) {
    static std::int64_t serial = 0;
    RunRecord r;
    r.song_hash = song;
    r.song_title = "Song " + std::to_string(song);
    r.local_day = day;
    r.start_utc_s = day * 86'400LL + ++serial;
    r.end_utc_s = r.start_utc_s + 60;
    r.duration_ms = static_cast<std::int64_t>(minutes * 60'000.0);
    r.score = score;
    r.covered_percent = score > 0 ? 100 : 0;
    r.all = {graded, hit < 0 ? graded : hit};
    return sanitized(r);
}

// A run that named these bars as (measure, missed of 4) pairs.
RunRecord bar_run(std::uint64_t song, std::initializer_list<std::pair<int, int>> bars,
                  int score = 500) {
    RunRecord r = run(song, 900, score, 5.0, 4 * static_cast<int>(bars.size()));
    int missed = 0;
    for (const auto& [measure, miss] : bars) {
        r.measures.push_back({measure, 4, miss, 0});
        missed += miss;
    }
    // At least kMinGradedForBest notes, so the run counts.
    const int graded = std::max(kMinGradedForBest, 4 * static_cast<int>(bars.size()));
    r.all = {graded, graded - missed};
    return sanitized(r);
}

ProgressView view_of(const std::vector<RunRecord>& runs, int goal = 0,
                     std::int32_t today = kToday) {
    const std::vector<ProfileRuns> household{{7, runs}};
    return build_progress_view(household, 7, today, goal);
}

const CalendarCell& cell(const ProgressView& v, std::int32_t day) {
    return v.calendar[v.calendar.size() - 1 - static_cast<std::size_t>(v.today - day)];
}

} // namespace

TEST_CASE("progress view: an empty history gives an empty page") {
    const ProgressView v = view_of({});
    CHECK(v.profile_id == 7);
    CHECK(v.today == kToday);
    CHECK(v.household_size == 1);
    REQUIRE(v.calendar.size() == static_cast<std::size_t>(kCalendarDays));
    CHECK(v.calendar.front().day == kToday - kCalendarDays + 1);
    CHECK(v.calendar.back().day == kToday);
    for (const CalendarCell& c : v.calendar) {
        CHECK(c.level == 0);
        CHECK(c.minutes == 0);
    }
    CHECK(v.me.streak.current == 0);
    CHECK(v.me.songs == 0);
    CHECK(v.me.runs == 0);
    CHECK(v.me.points == 0);
    CHECK(v.me.rank == 0);
    CHECK(v.me.firsts == 0);
    CHECK(v.me.medals.held() == 0);
    CHECK(v.me.medals.title() == 1);
    CHECK(v.me.accuracy.empty());
    CHECK(v.bests.empty());
    CHECK(v.practise.empty());
    CHECK(v.last_song_hash == 0);
    CHECK(v.last_song_title.empty());
    REQUIRE(v.household.size() == 1);
    CHECK(v.household[0] == v.me);
}

TEST_CASE("progress view: one run fills the tiles, the graph and the best row") {
    RunRecord r = run(5, kToday, 600, 7.5, 20, 18);
    r.speed_percent = 80;
    const ProgressView v = view_of({r}, 10);
    CHECK(v.me.songs == 1);
    CHECK(v.me.runs == 1);
    CHECK(v.me.points == 600);
    CHECK(v.me.rank == 1);
    CHECK(v.me.firsts == 0); // alone in the household
    CHECK(v.me.minutes_today == 7);
    CHECK(v.me.minutes_week == 7);
    CHECK(v.me.minutes_month == 7);
    CHECK(v.me.streak.current == 1);
    CHECK(v.me.streak.today_done);
    REQUIRE(v.me.accuracy.size() == 1);
    CHECK(v.me.accuracy[0] == AccuracyPoint{kToday, 90, 80});
    REQUIRE(v.bests.size() == 1);
    CHECK(v.bests[0].song_hash == 5);
    CHECK(v.bests[0].title == "Song 5");
    CHECK(v.bests[0].score == 600);
    CHECK(v.bests[0].accuracy_pct == 90);
    CHECK(v.bests[0].speed_percent == 80);
    CHECK(v.bests[0].day == kToday);
    CHECK(v.bests[0].runs == 1);
    CHECK(v.bests[0].trend == std::vector<int>{600});
    CHECK(v.bests[0].rank == 1);
    CHECK(v.last_song_hash == 5);
    CHECK(v.last_song_title == "Song 5");
    CHECK(v.goal.has_goal);
    CHECK(v.goal.goal_minutes == 10);
    CHECK(v.goal.minutes_today == doctest::Approx(7.5));
    CHECK_FALSE(v.goal.met);
    CHECK(cell(v, kToday).minutes == 7);
    CHECK(v.household[0] == v.me);
}

TEST_CASE("progress view: minutes in the week and the month count every run, rounded down") {
    // 90 s of a run with too few notes still counts as practice.
    const std::vector<RunRecord> runs{run(1, kToday, 400, 1.5, 3), run(1, kToday - 6, 400, 2.0),
                                      run(1, kToday - 7, 400, 4.0), run(1, kToday - 29, 400, 8.0),
                                      run(1, kToday - 30, 400, 16.0)};
    const ProgressView v = view_of(runs);
    CHECK(v.me.minutes_today == 1);
    CHECK(v.me.minutes_week == 3);   // 1.5 + 2 = 3.5
    CHECK(v.me.minutes_month == 15); // + 4 + 8
    CHECK(v.me.runs == 4);           // the run of 3 notes does not count
}

TEST_CASE("progress view: calendar levels against the daily goal") {
    const std::vector<RunRecord> runs{run(1, kToday - 5, 400, 5.0),  // half the goal
                                      run(1, kToday - 4, 400, 10.0), // the goal
                                      run(1, kToday - 3, 400, 19.9), // under twice the goal
                                      run(1, kToday - 2, 400, 20.0), // twice the goal
                                      run(1, kToday - 1, 400, 0.4)}; // under a minute
    const ProgressView v = view_of(runs, 10);
    CHECK(cell(v, kToday).level == 0);
    CHECK(cell(v, kToday - 5).level == 2); // half the goal
    CHECK(cell(v, kToday - 4).level == 3); // the goal
    CHECK(cell(v, kToday - 3).level == 3);
    CHECK(cell(v, kToday - 2).level == 4); // twice the goal
    CHECK(cell(v, kToday - 1).level == 1);
    CHECK(cell(v, kToday - 1).minutes == 0);
    CHECK(cell(v, kToday - 2).minutes == 20);
    CHECK(cell(v, kToday - 6).level == 0);
}

TEST_CASE("progress view: without a goal the steps are those of a 20 minute goal") {
    const std::vector<RunRecord> runs{run(1, kToday - 2, 400, 0.4), run(1, kToday - 1, 400, 90.0)};
    const ProgressView v = view_of(runs, 0);
    CHECK_FALSE(v.goal.has_goal);
    CHECK(cell(v, kToday - 2).level == 1);
    CHECK(cell(v, kToday - 1).level == 4);
    CHECK(cell(v, kToday).level == 0);
}

TEST_CASE("progress view: a run dated after today is left out of everything") {
    const std::vector<RunRecord> runs{run(1, kToday + 1, 900), run(2, kToday, 300)};
    const ProgressView v = view_of(runs);
    CHECK(v.me.runs == 1);
    CHECK(v.me.songs == 1);
    CHECK(v.me.points == 300);
    CHECK(v.me.accuracy.size() == 1);
    REQUIRE(v.bests.size() == 1);
    CHECK(v.bests[0].song_hash == 2);
    CHECK(v.last_song_hash == 2);
    CHECK(v.calendar.back().day == kToday);
    CHECK(v.me.minutes_week == 5);
}

TEST_CASE("progress view: a repeated run counts once") {
    const RunRecord r = run(1, kToday, 400);
    const ProgressView v = view_of({r, r});
    CHECK(v.me.runs == 1);
    CHECK(v.me.minutes_today == 5);
    REQUIRE(v.bests.size() == 1);
    CHECK(v.bests[0].runs == 1);
}

TEST_CASE("progress view: runs too short to count, or without a song, show only as time") {
    const std::vector<RunRecord> runs{run(1, kToday, 400, 5.0, kMinGradedForBest - 1),
                                      run(0, kToday, 400)};
    const ProgressView v = view_of(runs);
    CHECK(v.me.runs == 0);
    CHECK(v.me.songs == 0);
    CHECK(v.me.points == 0);
    CHECK(v.me.minutes_today == 10);
    CHECK(v.bests.empty());
    CHECK(v.last_song_hash == 0);
}

TEST_CASE("progress view: the best row takes the best points, the earlier run on a tie") {
    RunRecord first = run(1, kToday - 5, 700, 5.0, 20, 15);
    first.speed_percent = 60;
    RunRecord tie = run(1, kToday - 3, 700, 5.0, 20, 20);
    const std::vector<RunRecord> runs{first, run(1, kToday - 4, 400), tie, run(1, kToday - 1, 650)};
    const ProgressView v = view_of(runs);
    REQUIRE(v.bests.size() == 1);
    CHECK(v.bests[0].score == 700);
    CHECK(v.bests[0].day == kToday - 5);
    CHECK(v.bests[0].accuracy_pct == 75);
    CHECK(v.bests[0].speed_percent == 60);
    CHECK(v.bests[0].runs == 4);
    CHECK(v.bests[0].trend == std::vector<int>{700, 400, 700, 650});
    // The same as the leaderboard's personal best.
    const PersonalBest best = personal_best(runs, 1, kToday);
    CHECK(best.score == v.bests[0].score);
    CHECK(best.day == v.bests[0].day);
}

TEST_CASE("progress view: bests are sorted by points and cut to kBestSongs") {
    std::vector<RunRecord> runs;
    for (std::uint64_t s = 1; s <= 12; ++s) {
        runs.push_back(run(s, kToday - 1, 100 * static_cast<int>(s)));
    }
    const ProgressView v = view_of(runs);
    REQUIRE(v.bests.size() == static_cast<std::size_t>(kBestSongs));
    CHECK(v.bests.front().song_hash == 12);
    CHECK(v.bests.back().song_hash == 12 - kBestSongs + 1);
    for (std::size_t i = 1; i < v.bests.size(); ++i) {
        CHECK(v.bests[i - 1].score > v.bests[i].score);
    }
    CHECK(v.me.songs == 12); // the tile counts them all
}

TEST_CASE("progress view: songs with equal points are ordered by title") {
    RunRecord b = run(1, kToday, 500);
    b.song_title = "B";
    RunRecord c = run(2, kToday, 500);
    c.song_title = "C";
    RunRecord a = run(3, kToday, 500);
    a.song_title = "A";
    const ProgressView v = view_of({b, c, a});
    REQUIRE(v.bests.size() == 3);
    CHECK(v.bests[0].title == "A");
    CHECK(v.bests[1].title == "B");
    CHECK(v.bests[2].title == "C");
}

TEST_CASE("progress view: the trend keeps the latest runs, oldest first") {
    std::vector<RunRecord> runs;
    for (int i = 1; i <= 10; ++i) {
        runs.push_back(run(1, kToday - 20 + i, 100 * i));
    }
    const ProgressView v = view_of(runs);
    REQUIRE(v.bests.size() == 1);
    CHECK(v.bests[0].runs == 10);
    CHECK(v.bests[0].trend.size() == static_cast<std::size_t>(kBestTrendRuns));
    CHECK(v.bests[0].trend.front() == 400);
    CHECK(v.bests[0].trend.back() == 1000);
}

TEST_CASE("progress view: runs from before scores count in time and accuracy, not in points") {
    const std::vector<RunRecord> runs{run(1, kToday - 3, 0, 5.0, 20, 10),
                                      run(1, kToday - 2, 0, 5.0, 20, 16),
                                      run(2, kToday - 1, 500, 5.0, 20, 20)};
    const ProgressView v = view_of(runs);
    CHECK(v.me.songs == 2);
    CHECK(v.me.runs == 3);
    CHECK(v.me.points == 500);
    CHECK(v.me.rank == 1);
    CHECK(v.me.minutes_week == 15);
    CHECK(v.me.accuracy.size() == 3);
    REQUIRE(v.bests.size() == 2);
    CHECK(v.bests[0].song_hash == 2);
    // The unscored song: the best run by accuracy, no points, no rank, a trend of accuracy.
    CHECK(v.bests[1].song_hash == 1);
    CHECK(v.bests[1].score == 0);
    CHECK(v.bests[1].accuracy_pct == 80);
    CHECK(v.bests[1].day == kToday - 2);
    CHECK(v.bests[1].runs == 2);
    CHECK(v.bests[1].rank == 0);
    CHECK(v.bests[1].trend == std::vector<int>{50, 80});
    CHECK(v.me.medals.tier_of(Achievement::Finisher) == 1); // the scored run over a whole song; the old runs earn none
    CHECK(v.me.medals.value_of(Achievement::Runs) == 1);
}

TEST_CASE("progress view: only unscored runs give no rank and no points") {
    const ProgressView v = view_of({run(1, kToday, 0)});
    CHECK(v.me.points == 0);
    CHECK(v.me.rank == 0);
    CHECK(v.me.runs == 1);
    CHECK(v.me.streak.current == 1); // practice counts for the streak
}

TEST_CASE("progress view: the accuracy graph is the latest runs, oldest first") {
    std::vector<RunRecord> runs;
    for (int i = 0; i < kAccuracyRuns + 5; ++i) {
        runs.push_back(run(static_cast<std::uint64_t>(1 + i % 3), kToday - 100 + i, 100, 1.0, 20,
                           10 + i % 10));
    }
    const ProgressView v = view_of(runs);
    CHECK(v.me.runs == kAccuracyRuns + 5);
    REQUIRE(v.me.accuracy.size() == static_cast<std::size_t>(kAccuracyRuns));
    CHECK(v.me.accuracy.front().day == kToday - 100 + 5);
    CHECK(v.me.accuracy.back().day == kToday - 100 + kAccuracyRuns + 4);
}

TEST_CASE("progress view: the streak counts any practice, the tile and the Streak medal alike") {
    std::vector<RunRecord> runs;
    for (int i = 0; i < 7; ++i) {
        runs.push_back(run(1, kToday - i, 0)); // unscored runs count a day too
    }
    const ProgressView v = view_of(runs);
    CHECK(v.me.streak.current == 7);
    CHECK(v.me.medals.value_of(Achievement::Streak) == 7);
    CHECK(v.me.medals.tier_of(Achievement::Streak) == 3); // 7 days: tier 3
    runs.push_back(run(1, kToday - 7, 300));
    CHECK(view_of(runs).me.streak.current == 8);
    CHECK(view_of(runs).me.medals.value_of(Achievement::Streak) == 8);
}

TEST_CASE("progress view: the Streak medal's count is always the tile's longest streak, freezes included") {
    // Histories with gaps a freeze covers, gaps that end the streak, unscored and scored days, and a
    // streak that is still alive: in every one the medal counts what the tile's rule says.
    const std::vector<std::vector<int>> offsets{
        {0, 1, 2, 3},                            // unbroken, up to today
        {0, 1, 2, 6, 7},                         // a gap of three the freezes cover
        {0, 20, 21, 22, 40},                     // gaps that end it twice
        {1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11},     // alive, today not played
        {0, 2, 4, 6, 8, 10, 12, 14},             // every other day
        {30, 31, 32, 60, 61, 62, 63, 64, 65, 66} // long gaps between streaks
    };
    for (const std::vector<int>& days : offsets) {
        for (const int score : {0, 300}) { // unscored runs and scored runs
            std::vector<RunRecord> runs;
            for (const int back : days) {
                runs.push_back(run(1, kToday - back, score));
            }
            std::reverse(runs.begin(), runs.end()); // oldest first, as the history keeps them
            const ProgressView v = view_of(runs);
            const Streak tile = profile_streak(runs, kToday);
            CAPTURE(score);
            CHECK(v.me.streak == tile);
            CHECK(v.me.medals.value_of(Achievement::Streak) == tile.longest);
            CHECK(tile.current <= tile.longest);
        }
    }
    // The tile's count and the medal's meet while the streak has never broken.
    std::vector<RunRecord> alive;
    for (int back = 5; back >= 0; --back) {
        alive.push_back(run(1, kToday - back, 0));
    }
    const ProgressView v = view_of(alive);
    CHECK(v.me.streak.current == v.me.medals.value_of(Achievement::Streak));
}

TEST_CASE("progress view: ranks and firsts among a household") {
    const std::vector<RunRecord> ana{run(1, 900, 800), run(2, 900, 300), run(3, 900, 500)};
    const std::vector<RunRecord> ben{run(1, 900, 600), run(2, 900, 700), run(3, 900, 500)};
    const std::vector<RunRecord> cy{run(4, 900, 100)};
    const std::vector<ProfileRuns> household{{1, ana}, {2, ben}, {3, cy}};
    const ProgressView a = build_progress_view(household, 1, kToday, 0);
    CHECK(a.household_size == 3);
    CHECK(a.me.points == 1600);
    CHECK(a.me.rank == 2);
    CHECK(a.me.firsts == 2); // song 1, and song 3 shared with ben
    const ProgressView b = build_progress_view(household, 2, kToday, 0);
    CHECK(b.me.points == 1800);
    CHECK(b.me.rank == 1);   // ben leads on points: 1800 against 1600
    CHECK(b.me.firsts == 2); // song 2, and song 3 shared with ana
    const ProgressView c = build_progress_view(household, 3, kToday, 0);
    CHECK(c.me.rank == 3);
    CHECK(c.me.firsts == 1);
    CHECK(c.me.points == 100);
    // The view carries the household's order (best rank first), so the screen does not work it out each frame.
    CHECK(a.household_ranked == household_order(a.household));
    CHECK(a.household_ranked.size() == 3);
    // Ana's song rows carry her place on each song.
    REQUIRE(a.bests.size() == 3);
    CHECK(a.bests[0].song_hash == 1);
    CHECK(a.bests[0].rank == 1);
    CHECK(a.bests[1].song_hash == 3);
    CHECK(a.bests[1].rank == 1);
    CHECK(a.bests[2].song_hash == 2);
    CHECK(a.bests[2].rank == 2);
}

TEST_CASE("progress view: household lines follow the order given and match the page") {
    const std::vector<RunRecord> ana{run(1, 900, 300)};
    const std::vector<RunRecord> ben{run(1, 900, 900), run(2, 900, 100)};
    const std::vector<ProfileRuns> household{{20, ana}, {10, ben}, {30, {}}};
    const ProgressView v = build_progress_view(household, 10, kToday, 0);
    REQUIRE(v.household.size() == 3);
    CHECK(v.household[0].profile_id == 20);
    CHECK(v.household[1].profile_id == 10);
    CHECK(v.household[2].profile_id == 30);
    CHECK(v.me == v.household[1]);
    CHECK(v.household[0].rank == 2);
    CHECK(v.household[1].rank == 1);
    CHECK(v.household[2].rank == 0); // no scored run: no place
    CHECK(v.household[2].firsts == 0);
    CHECK(v.household[0].songs == 1);
}

TEST_CASE("progress view: a profile the household does not hold gives an empty view of that id") {
    const std::vector<RunRecord> ana{run(1, 900, 300)};
    const std::vector<ProfileRuns> household{{20, ana}};
    const ProgressView v = build_progress_view(household, 99, kToday, 15);
    CHECK(v.profile_id == 99);
    CHECK(v.me.profile_id == 99);
    CHECK(v.me.runs == 0);
    CHECK(v.me.rank == 0);
    CHECK(v.bests.empty());
    CHECK(v.practise.empty());
    CHECK(v.calendar.size() == static_cast<std::size_t>(kCalendarDays));
    CHECK(v.goal.goal_minutes == 15);
    CHECK(v.household_size == 1);
    REQUIRE(v.household.size() == 1);
    CHECK(v.household[0].profile_id == 20);
    CHECK(v.household[0].runs == 1);
}

TEST_CASE("progress view: practise names the hottest bar and grows over the bars next to it") {
    const std::vector<RunRecord> runs{bar_run(1, {{3, 3}, {4, 4}, {5, 1}, {9, 2}, {1, 1}, {2, 1}})};
    const ProgressView v = view_of(runs);
    REQUIRE(v.practise.size() == 1);
    CHECK(v.practise[0].song_hash == 1);
    CHECK(v.practise[0].title == "Song 1");
    // Bar 4 is the hottest; 3 and 5 are named next to it, and 2 and 1 next to those. Bar 9 is not
    // next to the group.
    CHECK(v.practise[0].first_measure == 1);
    CHECK(v.practise[0].last_measure == 5);
}

TEST_CASE("progress view: practise keeps a lone bar, and a pickup bar is bar 0") {
    const ProgressView v = view_of({bar_run(1, {{7, 3}, {9, 1}})});
    REQUIRE(v.practise.size() == 1);
    CHECK(v.practise[0].first_measure == 7);
    CHECK(v.practise[0].last_measure == 7);
    const ProgressView p = view_of({bar_run(1, {{0, 4}, {1, 2}})});
    REQUIRE(p.practise.size() == 1);
    CHECK(p.practise[0].first_measure == 0);
    CHECK(p.practise[0].last_measure == 1);
}

TEST_CASE("progress view: practise takes one spot per song, the newest run's, newest first") {
    const std::vector<RunRecord> runs{
        bar_run(5, {{4, 4}}),                       // the fourth spot is not offered
        bar_run(1, {{2, 3}}),                       // an older run of song 1: its bars are not read
        bar_run(2, {{6, 3}}), bar_run(1, {{8, 2}}), // song 1's newest run
        bar_run(3, {{1, 0}}),                       // no bar named: no spot
        bar_run(4, {{5, 4}}),
    };
    const ProgressView v = view_of(runs);
    REQUIRE(v.practise.size() == static_cast<std::size_t>(kPractiseSpots));
    CHECK(v.practise[0].song_hash == 4);
    CHECK(v.practise[1].song_hash == 1);
    CHECK(v.practise[1].first_measure == 8);
    CHECK(v.practise[2].song_hash == 2);
    CHECK(v.last_song_hash == 4);
}

TEST_CASE("progress view: a newest run that names no bar gives no spot for its song") {
    const ProgressView v = view_of({bar_run(1, {{2, 3}}), bar_run(1, {{1, 0}})});
    CHECK(v.practise.empty());
    CHECK(v.last_song_hash == 1);
}

TEST_CASE("progress view: practise reads only the newest kPractiseRuns runs") {
    std::vector<RunRecord> runs{bar_run(99, {{2, 4}})};
    for (int i = 0; i < kPractiseRuns; ++i) {
        runs.push_back(bar_run(1, {{1, 0}})); // no bar named
    }
    CHECK(view_of(runs).practise.empty());
}

TEST_CASE("progress view: practise ignores runs after today and unscored runs still count") {
    const std::vector<RunRecord> runs{bar_run(1, {{3, 3}}, 0), bar_run(2, {{4, 3}}, 400)};
    RunRecord later = bar_run(3, {{5, 4}});
    later.local_day = kToday + 1;
    const ProgressView v = view_of({runs[0], runs[1], later});
    REQUIRE(v.practise.size() == 2);
    CHECK(v.practise[0].song_hash == 2);
    CHECK(v.practise[1].song_hash == 1);
}

TEST_CASE("progress view: the household is listed by rank, those with no scored run last") {
    const auto line = [](std::int64_t id, int rank) {
        ProfileProgress p;
        p.profile_id = id;
        p.rank = rank;
        return p;
    };
    // Profile order Anna, Mum, Ben, Dani; Anna 1st, Ben 2nd, Mum 3rd, Dani with no score.
    const std::vector<ProfileProgress> lines{line(1, 1), line(2, 3), line(3, 2), line(4, 0)};
    const std::vector<std::size_t> order = household_order(lines);
    REQUIRE(order.size() == 4);
    CHECK(order[0] == 0);
    CHECK(order[1] == 2);
    CHECK(order[2] == 1);
    CHECK(order[3] == 3);
    // Equal ranks and equal absences keep the order given.
    const std::vector<ProfileProgress> ties{line(1, 0), line(2, 2), line(3, 2), line(4, 0)};
    const std::vector<std::size_t> kept = household_order(ties);
    CHECK(kept == std::vector<std::size_t>{1, 2, 0, 3});
    CHECK(household_order({}).empty());
}

TEST_CASE("calendar level: the thresholds of the four shades against the goal") {
    constexpr std::int64_t kMin = 60'000;
    constexpr std::int64_t goal = 20 * kMin;
    CHECK(calendar_level(0, goal) == 0);
    CHECK(calendar_level(-5, goal) == 0);
    CHECK(calendar_level(1, goal) == 1);                 // a moment
    CHECK(calendar_level(10 * kMin - 1, goal) == 1);     // just under half
    CHECK(calendar_level(10 * kMin, goal) == 2);         // half
    CHECK(calendar_level(20 * kMin - 1, goal) == 2);     // just under the goal
    CHECK(calendar_level(20 * kMin, goal) == 3);         // the goal
    CHECK(calendar_level(40 * kMin - 1, goal) == 3);     // just under twice
    CHECK(calendar_level(40 * kMin, goal) == 4);         // twice
    CHECK(calendar_level(600 * kMin, goal) == 4);        // and no more steps
    // A short goal moves every step with it; no goal uses 20 minutes.
    CHECK(calendar_level(5 * kMin, 10 * kMin) == 2);
    CHECK(calendar_level(5 * kMin, 0) == 1);
    CHECK(calendar_level(20 * kMin, 0) == 3);
    CHECK(calendar_level(45 * kMin, 0) == 4);
    CHECK(kCalendarLevels == 4);
}
