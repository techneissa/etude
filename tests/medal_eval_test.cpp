// SPDX-License-Identifier: GPL-3.0-or-later
// What the runs earn: each achievement's rule at its thresholds, the counting rules, the title and
// what is nearest the next tier (medal_eval.hpp).
#include "etude/medal_eval.hpp"

#include "etude/progress.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace etude;

namespace {

constexpr std::int32_t kToday = 20'000;

// 19,926 is a Monday (1970-01-01 was a Thursday: day 4 is the first Monday, and 19,926 = 4 + 7 x 2,846).
constexpr std::int32_t week_start(int week) { return 19'926 + 7 * week; }

RunRecord make(std::int32_t day, int covered = 100, int graded = 20, int hit = -1, std::uint64_t song = 1,
               int score = 600, std::int64_t duration_ms = 60'000) {
    static std::int64_t serial = 0;
    RunRecord r;
    r.song_hash = song;
    r.local_day = day;
    r.start_utc_s = static_cast<std::int64_t>(day) * 86'400 + (++serial) * 100;
    r.end_utc_s = r.start_utc_s + 60;
    r.duration_ms = duration_ms;
    r.covered_percent = covered;
    r.score = score;
    r.speed_percent = 100;
    r.hands = HandFilter::Both;
    r.all = {graded, hit < 0 ? graded : hit};
    return sanitized(r);
}

MedalContext ctx(int goal = 0, std::int64_t created = 0) { return {kToday, goal, created, 0, nullptr}; }

std::int64_t value(const std::vector<RunRecord>& runs, Achievement a, MedalContext c = ctx()) {
    return evaluate_medals(runs, c).value_of(a);
}

} // namespace

TEST_CASE("medals: no runs earn nothing, and the player is a Beginner") {
    const MedalSet none = evaluate_medals({}, ctx());
    CHECK(none.held() == 0);
    CHECK(none.points() == 0);
    CHECK(none.title() == 1);
    for (const std::uint8_t t : none.tier) {
        CHECK(t == 0);
    }
}

TEST_CASE("medals: only scored runs with a song and enough notes, up to today, count, and a repeat counts once") {
    std::vector<RunRecord> runs{make(100, 0), make(101, 100, 7, 7), make(102, 100, 20, 20, 0),
                                make(kToday + 1)};
    CHECK(value(runs, Achievement::Runs) == 0);
    // Nothing but the streak counts them: it counts any practice day, as the tiles do (days 100 to 102).
    const MedalSet set = evaluate_medals(runs, ctx());
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        CHECK(set.tier[i] == (static_cast<Achievement>(i) == Achievement::Streak ? 1 : 0));
    }
    CHECK(set.value_of(Achievement::Streak) == 3);
    runs.push_back(make(103));
    runs.push_back(runs.back()); // the same run twice
    CHECK(value(runs, Achievement::Runs) == 1);
}

TEST_CASE("medals: streak is the longest streak of practice days, forgiving missed days with freezes") {
    std::vector<RunRecord> runs;
    for (const int d : {10, 11, 12, 20, 21, 22, 23, 24, 30}) {
        runs.push_back(make(d));
    }
    // 10..12 build 3 freezes, the 7 missed days 13..19 end it; 20..24 count 5 days with 5 freezes banked,
    // 25..29 spend all five, and day 30 plays on: 6 practice days (the frozen days are not counted).
    CHECK(value(runs, Achievement::Streak) == 6);
    CHECK(evaluate_medals(runs, ctx()).tier_of(Achievement::Streak) == 2); // 6 days: tier 2 (4), tier 3 asks 7
    // Five days in a row and nothing else is five.
    std::vector<RunRecord> five;
    for (const int d : {10, 11, 12, 13, 14}) {
        five.push_back(make(d));
    }
    CHECK(value(five, Achievement::Streak) == 5);
}

TEST_CASE("medals: goal getter counts days that reach the goal; none without a goal") {
    std::vector<RunRecord> runs;
    runs.push_back(make(10, 100, 20, -1, 1, 600, 10 * 60'000));
    runs.push_back(make(11, 100, 20, -1, 1, 600, 5 * 60'000));
    runs.push_back(make(11, 100, 20, -1, 2, 600, 5 * 60'000)); // two runs add up to 10 minutes
    runs.push_back(make(12, 100, 20, -1, 1, 600, 9 * 60'000)); // short of it
    CHECK(value(runs, Achievement::GoalGetter, ctx(10)) == 2);
    CHECK(value(runs, Achievement::GoalGetter, ctx(0)) == 0);
}

TEST_CASE("medals: a full week is seven days of the goal from Monday to Sunday") {
    const std::int32_t monday = week_start(0);
    std::vector<RunRecord> runs;
    for (int d = 0; d < 7; ++d) {
        runs.push_back(make(monday + d, 100, 20, -1, 1, 600, 15 * 60'000));
    }
    for (int d = 1; d < 7; ++d) { // the next week: Tuesday to Sunday, six days
        runs.push_back(make(monday + 7 + d, 100, 20, -1, 1, 600, 15 * 60'000));
    }
    for (int d = 3; d < 10; ++d) { // seven days across two weeks: not a week
        runs.push_back(make(monday + 14 + d, 100, 20, -1, 1, 600, 15 * 60'000));
    }
    CHECK(value(runs, Achievement::FullWeek, ctx(15)) == 1);
}

TEST_CASE("medals: a comeback is a run of a song after seven or more days without playing it") {
    const std::vector<RunRecord> runs{make(10), make(17), make(25), make(33), make(34)};
    // The one song: 10 -> 17 is six days away (not enough); 17 -> 25: seven; 25 -> 33: seven.
    CHECK(value(runs, Achievement::Comeback) == 2);
    CHECK(value(std::vector<RunRecord>{make(10), make(18)}, Achievement::Comeback) == 1);
    CHECK(value(std::vector<RunRecord>{make(10), make(17)}, Achievement::Comeback) == 0);
    // It is the song's own gap: playing other songs in between does not end it, and a song met for the
    // first time after a long break is no comeback.
    CHECK(value(std::vector<RunRecord>{make(10, 100, 20, -1, 1), make(12, 100, 20, -1, 2), make(18, 100, 20, -1, 1)},
                Achievement::Comeback) == 1);
    CHECK(value(std::vector<RunRecord>{make(10, 100, 20, -1, 2), make(30, 100, 20, -1, 1)}, Achievement::Comeback) == 0);
    // Two songs each come back once: two comebacks; several runs on the day back count once.
    CHECK(value(std::vector<RunRecord>{make(10, 100, 20, -1, 1), make(10, 100, 20, -1, 2), make(20, 100, 20, -1, 1),
                                       make(20, 100, 20, -1, 1), make(21, 100, 20, -1, 2)},
                Achievement::Comeback) == 2);
}

TEST_CASE("medals: on target is a section with every note right and no wrong press") {
    RunRecord clean = make(10, 50, 20, 20);
    RunRecord wrong = make(11, 50, 20, 20);
    wrong.wrong = 1;
    const RunRecord missed = make(12, 50, 20, 19);
    const RunRecord tiny = make(13, 50, 8, 8);
    const RunRecord small = make(14, 50, 7, 7);  // too few notes: not even a counting run
    const RunRecord whole = make(15, 100, 20, 20); // a full run is not a section
    CHECK(value({clean, wrong, missed, tiny, small, whole}, Achievement::OnTarget) == 2);
}

TEST_CASE("medals: sharp is the best whole-percent accuracy of a full run, rounded down") {
    CHECK(value({make(10, 100, 20, 19)}, Achievement::Sharp) == 95);
    CHECK(value({make(10, 100, 200, 199)}, Achievement::Sharp) == 99); // 99.5 stays 99
    CHECK(value({make(10, 100, 20, 20)}, Achievement::Sharp) == 100);
    CHECK(value({make(10, 94, 20, 20)}, Achievement::Sharp) == 0); // a section is no full run
    CHECK(value({make(10, 100, 20, 17), make(11, 100, 20, 18)}, Achievement::Sharp) == 90);
}

TEST_CASE("medals: steady hands is the longest chain of full runs at 85% or better") {
    std::vector<RunRecord> runs;
    int day = 10;
    for (const int hit : {18, 18, 17, 20, 20, 20, 15, 20}) { // 90 90 85 100 100 100 75 100
        runs.push_back(make(day++, 100, 20, hit));
    }
    CHECK(value(runs, Achievement::SteadyHands) == 6);
    // A run that is not a full one is left out of the chain: it neither extends it nor breaks it.
    CHECK(value({make(10, 100, 20, 20), make(11, 50, 20, 20), make(12, 100, 20, 20)}, Achievement::SteadyHands) == 2);
}

TEST_CASE("medals: full tempo needs full speed and 90% on a full run") {
    RunRecord slow = make(10, 100, 20, 20);
    slow.speed_percent = 99;
    RunRecord fast = make(11, 100, 20, 18);
    fast.speed_percent = 120;
    const RunRecord sloppy = make(12, 100, 20, 17);
    CHECK(value({slow, fast, sloppy}, Achievement::FullTempo) == 1);
}

TEST_CASE("medals: a personal best is a run that beats an earlier run of the song, never the first") {
    std::vector<RunRecord> runs{make(10, 100, 20, 20, 1, 500), make(11, 100, 20, 20, 1, 600),
                                make(12, 100, 20, 20, 1, 550), make(13, 100, 20, 20, 1, 600),
                                make(14, 100, 20, 20, 1, 700), make(15, 100, 20, 20, 2, 300)};
    CHECK(value(runs, Achievement::PersonalBest) == 2);
}

TEST_CASE("medals: big score is the highest points of a run") {
    CHECK(value({make(10, 100, 20, 20, 1, 640), make(11, 100, 20, 20, 1, 1180)}, Achievement::BigScore) == 1180);
}

TEST_CASE("medals: the song rules count songs, not runs") {
    std::vector<RunRecord> runs;
    runs.push_back(make(10, 100, 20, 17, 1)); // 85%
    runs.push_back(make(11, 100, 20, 17, 1));
    runs.push_back(make(12, 100, 20, 16, 2)); // 80%
    runs.push_back(make(13, 100, 20, 15, 3)); // 75%: not in the repertoire
    runs.push_back(make(14, 60, 20, 20, 4));  // a section only
    RunRecord mastered = make(15, 100, 20, 20, 5);
    runs.push_back(mastered);
    RunRecord slow = make(16, 100, 20, 20, 6);
    slow.speed_percent = 80;
    runs.push_back(slow);
    CHECK(value(runs, Achievement::Repertoire) == 4); // songs 1, 2, 5, 6
    CHECK(value(runs, Achievement::Mastered) == 1);   // song 5 only: 6 is too slow
    CHECK(value(runs, Achievement::Explorer) == 6);
}

TEST_CASE("medals: an old friend is the most days one song was played on") {
    std::vector<RunRecord> runs{make(10, 100, 20, 20, 1), make(10, 100, 20, 20, 1), make(11, 100, 20, 20, 1),
                                make(12, 100, 20, 20, 1), make(10, 100, 20, 20, 2), make(11, 100, 20, 20, 2)};
    CHECK(value(runs, Achievement::OldFriend) == 3);
}

TEST_CASE("medals: a long haul is the longest full run in whole minutes") {
    std::vector<RunRecord> runs{make(10, 100, 20, 20, 1, 600, 150'000), make(11, 100, 20, 20, 1, 600, 359'000),
                                make(12, 50, 20, 20, 1, 600, 900'000)}; // a section does not count
    CHECK(value(runs, Achievement::LongHaul) == 5);
}

TEST_CASE("medals: both hands counts full runs with both hands at 85% or better") {
    RunRecord right = make(10, 100, 20, 20);
    right.hands = HandFilter::Right;
    CHECK(value({make(11, 100, 20, 17), make(12, 100, 20, 16), right}, Achievement::BothHands) == 1);
}

TEST_CASE("medals: practice time, a long day, runs and loops add up") {
    std::vector<RunRecord> runs;
    runs.push_back(make(10, 100, 20, 20, 1, 600, 20 * 60'000));
    runs.push_back(make(10, 100, 20, 20, 2, 600, 30 * 60'000));
    runs.push_back(make(11, 100, 20, 20, 1, 600, 10 * 60'000));
    runs.back().clean_passes = 4;
    runs.front().clean_passes = 3;
    CHECK(value(runs, Achievement::Minutes) == 60);
    CHECK(value(runs, Achievement::LongSession) == 50);
    CHECK(value(runs, Achievement::Runs) == 3);
    CHECK(value(runs, Achievement::SectionWork) == 7);
}

TEST_CASE("medals: regular counts weeks with practice on three days or more") {
    const std::int32_t monday = week_start(0);
    std::vector<RunRecord> runs;
    for (const int d : {0, 2, 4}) { // three days of the first week
        runs.push_back(make(monday + d));
    }
    for (const int d : {0, 1}) { // two days of the second
        runs.push_back(make(monday + 7 + d));
    }
    for (const int d : {5, 6, 7}) { // Saturday, Sunday and the next Monday: two weeks, neither has three
        runs.push_back(make(monday + 14 + d));
    }
    CHECK(value(runs, Achievement::Regular) == 1);
}

TEST_CASE("medals: finisher counts full runs, anniversary the days since the profile was made") {
    CHECK(value({make(10, 100), make(11, 95), make(12, 94)}, Achievement::Finisher) == 2);
    const std::int64_t created = static_cast<std::int64_t>(kToday - 40) * 86'400 + 100;
    CHECK(value({}, Achievement::Anniversary, ctx(0, created)) == 40);
    CHECK(value({}, Achievement::Anniversary, ctx(0, 0)) == 0); // creation not known
    CHECK(value({}, Achievement::Anniversary, ctx(0, static_cast<std::int64_t>(kToday + 5) * 86'400)) == 0);
}

TEST_CASE("medals: every threshold of a rule gives its tier (runs and finisher, one run a day)") {
    for (int t = 1; t <= kTierCount; ++t) {
        const std::int64_t need = achievement_info(Achievement::Runs).tiers[static_cast<std::size_t>(t - 1)];
        std::vector<RunRecord> runs;
        for (std::int64_t i = 0; i < need; ++i) {
            runs.push_back(make(static_cast<std::int32_t>(1000 + i % 500), 100, 20, 20, 1 + static_cast<std::uint64_t>(i)));
        }
        CAPTURE(t);
        CHECK(evaluate_medals(runs, ctx()).tier_of(Achievement::Runs) == t);
        runs.pop_back();
        CHECK(evaluate_medals(runs, ctx()).tier_of(Achievement::Runs) == t - 1);
    }
}

TEST_CASE("medals: the collector counts the other achievements held, and never itself") {
    std::vector<RunRecord> runs{make(10, 100, 20, 20, 1, 700, 20 * 60'000)};
    const MedalSet set = evaluate_medals(runs, ctx());
    int others = 0;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        others += static_cast<Achievement>(i) != Achievement::Collector && set.tier[i] > 0 ? 1 : 0;
    }
    CHECK(set.value_of(Achievement::Collector) == others);
    CHECK(others >= 3);
    CHECK(set.tier_of(Achievement::Collector) >= 1);
}

TEST_CASE("medals: a medal is never lost: more history only raises the tiers") {
    std::vector<RunRecord> runs;
    MedalSet before;
    for (int d = 0; d < 40; ++d) {
        runs.push_back(make(1000 + d, 100, 20, 18 + d % 3, 1 + static_cast<std::uint64_t>(d % 5), 500 + d * 10, 12 * 60'000));
        const MedalSet now = evaluate_medals(runs, ctx(10));
        for (std::size_t i = 0; i < kAchievementCount; ++i) {
            CHECK(now.tier[i] >= before.tier[i]);
        }
        before = now;
    }
}

TEST_CASE("medals: new medals are the tiers that rose, and the nearest to the next tier come first") {
    MedalSet a;
    MedalSet b;
    a.tier[static_cast<std::size_t>(Achievement::Streak)] = 2;
    b.tier[static_cast<std::size_t>(Achievement::Streak)] = 4;
    b.tier[static_cast<std::size_t>(Achievement::Runs)] = 1;
    a.tier[static_cast<std::size_t>(Achievement::Sharp)] = 5;
    b.tier[static_cast<std::size_t>(Achievement::Sharp)] = 5;
    const std::vector<MedalGain> gained = new_medals(a, b);
    REQUIRE(gained.size() == 2);
    CHECK(gained[0].achievement == Achievement::Streak);
    CHECK(gained[0].tier == 4);
    CHECK(gained[1].achievement == Achievement::Runs);

    MedalSet set;
    const auto put = [&](Achievement x, std::int64_t v) {
        set.value[static_cast<std::size_t>(x)] = v;
        set.tier[static_cast<std::size_t>(x)] = static_cast<std::uint8_t>(tier_for(achievement_info(x), v));
    };
    put(Achievement::Streak, 12);    // tier 3 (7), next 14: (12 - 7) / (14 - 7)
    put(Achievement::Runs, 4);       // tier 1 (1), next 5: 3/4
    put(Achievement::BigScore, 1400); // tier 8: left out
    put(Achievement::Minutes, 0);    // nothing yet: left out
    const std::vector<MedalProgress> near = closest_to_next(set, 5);
    REQUIRE(near.size() == 2);
    CHECK(near[0].achievement == Achievement::Runs);
    CHECK(near[0].share == doctest::Approx(0.75));
    CHECK(near[1].achievement == Achievement::Streak);
    CHECK(near[1].share == doctest::Approx(5.0 / 7.0));
    CHECK(near[1].next == 14);
    CHECK(closest_to_next(set, 1).size() == 1);
}

TEST_CASE("medals: the daily goal is read from a profile's settings text") {
    CHECK(goal_minutes_of_settings("[app]\ndaily_goal_minutes = 20\nbar_style = dock\n") == 20);
    CHECK(goal_minutes_of_settings("[app]\ndaily_goal_minutes=0\n") == 0);
    CHECK(goal_minutes_of_settings("[app]\nbar_style = dock\n") == -1);
    CHECK(goal_minutes_of_settings("[app]\nmy_daily_goal_minutes = 9\n") == -1); // another key
    CHECK(goal_minutes_of_settings("[app]\ndaily_goal_minutes = 9999\n") == 720); // clamped
    CHECK(goal_minutes_of_settings("") == -1);
}

TEST_CASE("medals: the categories split the 192 points, and a set's points are the sum of its categories'") {
    int possible = 0;
    for (std::size_t c = 0; c < kMedalCategoryCount; ++c) {
        possible += category_points_possible(static_cast<MedalCategory>(c));
    }
    CHECK(possible == kMaxMedalPoints);
    MedalSet set;
    set.tier[static_cast<std::size_t>(Achievement::Streak)] = 3;
    set.tier[static_cast<std::size_t>(Achievement::Sharp)] = 5;
    set.tier[static_cast<std::size_t>(Achievement::Runs)] = 8;
    CHECK(category_points(set, MedalCategory::Streaks) == 3);
    CHECK(category_points(set, MedalCategory::Accuracy) == 5);
    CHECK(category_points(set, MedalCategory::Time) == 8);
    CHECK(category_points(set, MedalCategory::Songs) == 0);
    int sum = 0;
    for (std::size_t c = 0; c < kMedalCategoryCount; ++c) {
        sum += category_points(set, static_cast<MedalCategory>(c));
    }
    CHECK(sum == set.points());
}

TEST_CASE("medals: versions of one piece count as one song (Explorer, Repertoire, Mastered, Old friend, Comeback)") {
    constexpr std::uint64_t kEasy = 11;
    constexpr std::uint64_t kOriginal = 12;
    constexpr std::uint64_t kOther = 20;
    const PieceMap pieces{{kEasy, kEasy}, {kOriginal, kEasy}}; // the two arrangements are one piece
    MedalContext grouped = ctx();
    grouped.pieces = &pieces;
    const std::vector<RunRecord> runs{make(10, 100, 20, -1, kEasy), make(10, 100, 20, -1, kOriginal),
                                      make(11, 100, 20, -1, kEasy), make(12, 100, 20, -1, kOriginal),
                                      make(40, 100, 20, -1, kOther)};
    // Without the grouping every file is a song.
    CHECK(value(runs, Achievement::Explorer) == 3);
    CHECK(value(runs, Achievement::Repertoire) == 3);
    CHECK(value(runs, Achievement::Mastered) == 3);
    CHECK(value(runs, Achievement::OldFriend) == 2);
    // With it the piece is one song: the other piece makes two, and the piece was played on three days.
    CHECK(value(runs, Achievement::Explorer, grouped) == 2);
    CHECK(value(runs, Achievement::Repertoire, grouped) == 2);
    CHECK(value(runs, Achievement::Mastered, grouped) == 2);
    CHECK(value(runs, Achievement::OldFriend, grouped) == 3);
    // Coming back to the piece through its other version after a week counts; without the grouping the
    // other file is a song met for the first time.
    const std::vector<RunRecord> away{make(10, 100, 20, -1, kEasy), make(20, 100, 20, -1, kOriginal)};
    CHECK(value(away, Achievement::Comeback) == 0);
    CHECK(value(away, Achievement::Comeback, grouped) == 1);
}

TEST_CASE("medals: Anniversary counts local days: a profile made just after midnight in UTC+2") {
    constexpr std::int32_t kMade = 19'990; // the local day it was made
    constexpr std::int32_t kPlus2 = 2 * 3600;
    // 23:30 UTC on day 19,989 is 01:30 on day 19,990 in UTC+2.
    const std::int64_t created = static_cast<std::int64_t>(19'989) * 86'400 + 23 * 3600 + 30 * 60;
    MedalContext c{kMade, 0, created, kPlus2};
    CHECK(value({}, Achievement::Anniversary, c) == 0); // made today
    c.today = kMade + 1;
    CHECK(value({}, Achievement::Anniversary, c) == 1);
    c.today = kMade + 7;
    CHECK(value({}, Achievement::Anniversary, c) == 7);
    // Counted by the UTC day it would be a day more (the mix the offset removes).
    MedalContext utc{kMade, 0, created, 0};
    CHECK(value({}, Achievement::Anniversary, utc) == 1);
}
