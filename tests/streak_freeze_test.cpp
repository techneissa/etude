// SPDX-License-Identifier: GPL-3.0-or-later
// The streak with freezes (progress.hpp, streak_with_freezes): every counting day earns a freeze (at most
// five banked), every missed day spends one and the streak holds, the streak ends on a missed day with
// none left; the count is the counting days.
#include "etude/progress.hpp"

#include "progress_fixtures.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;
using etude::testing::make;
using etude::testing::RunSpec;

namespace {

std::vector<RunRecord> played(std::initializer_list<std::int32_t> list, double minutes = 5.0) {
    std::vector<RunRecord> v;
    for (const std::int32_t d : list) {
        RunSpec s;
        s.day = d;
        s.minutes = minutes;
        v.push_back(make(s));
    }
    return v;
}

} // namespace

TEST_CASE("streak freezes: no runs, no streak and nothing banked") {
    const std::vector<RunRecord> none;
    const Streak s = streak_with_freezes(none, 100);
    CHECK(s.current == 0);
    CHECK(s.longest == 0);
    CHECK(s.freezes == 0);
    CHECK_FALSE(s.today_done);
    CHECK_FALSE(s.at_risk);
}

TEST_CASE("streak freezes: the first day starts the streak and banks the first freeze") {
    const Streak s = streak_with_freezes(played({100}), 100);
    CHECK(s.current == 1);
    CHECK(s.longest == 1);
    CHECK(s.freezes == 1);
    CHECK(s.today_done);
    CHECK_FALSE(s.at_risk);
    // A new profile's first run was yesterday: it holds, with today still to play.
    const Streak y = streak_with_freezes(played({99}), 100);
    CHECK(y.current == 1);
    CHECK(y.freezes == 1);
    CHECK(y.at_risk);
}

TEST_CASE("streak freezes: each counting day earns one, up to five") {
    const Streak a = streak_with_freezes(played({1, 2, 3}), 3);
    CHECK(a.current == 3);
    CHECK(a.freezes == 3);
    const Streak capped = streak_with_freezes(played({1, 2, 3, 4, 5, 6, 7, 8}), 8);
    CHECK(capped.current == 8);
    CHECK(capped.freezes == kMaxStreakFreezes);
}

TEST_CASE("streak freezes: a missed day spends one and the streak holds; a frozen day adds nothing to the count") {
    // Three days in a row (3 freezes), then 3 missed days, then back on the fourth: the streak goes on.
    const Streak s = streak_with_freezes(played({1, 2, 3, 7}), 7);
    CHECK(s.current == 4);   // the four days played
    CHECK(s.freezes == 1);   // 3 - 3 + 1 earned on day 7
    CHECK(s.frozen_days == 3);
    CHECK(s.longest == 4);
    CHECK(s.today_done);
}

TEST_CASE("streak freezes: the streak ends on a missed day with no freeze left") {
    // One day, then two missed days: one freeze covers the first, the second ends it.
    const Streak ended = streak_with_freezes(played({1}), 4); // days 2 and 3 missed, today (4) pending
    CHECK(ended.current == 0);
    CHECK(ended.freezes == 0);
    CHECK(ended.longest == 1);
    // And after the break a new day starts a fresh streak with one freeze.
    const Streak fresh = streak_with_freezes(played({1, 5}), 5);
    CHECK(fresh.current == 1);
    CHECK(fresh.freezes == 1);
    CHECK(fresh.longest == 1);
    CHECK(fresh.frozen_days == 0);
}

TEST_CASE("streak freezes: today, still to be played, is not a missed day") {
    // Played on 1, 2, 3; today is 4 and nothing yet: nothing spent, the streak is at risk of a freeze.
    const Streak s = streak_with_freezes(played({1, 2, 3}), 4);
    CHECK(s.current == 3);
    CHECK(s.freezes == 3);
    CHECK_FALSE(s.today_done);
    CHECK(s.at_risk);
    // Tomorrow it costs one if not played: with 3 freezes and today 7 (days 4, 5, 6 missed) it just holds.
    const Streak held = streak_with_freezes(played({1, 2, 3}), 7);
    CHECK(held.current == 3);
    CHECK(held.freezes == 0);
    CHECK(held.at_risk);
    // One day more and it is over.
    CHECK(streak_with_freezes(played({1, 2, 3}), 8).current == 0);
}

TEST_CASE("streak freezes: a long gap ends the streak even with five banked") {
    const Streak s = streak_with_freezes(played({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 17); // days 11..16: six missed
    CHECK(s.current == 0);
    CHECK(s.longest == 10);
    CHECK(streak_with_freezes(played({1, 2, 3, 4, 5, 6, 7, 8, 9, 10}), 16).current == 10); // five missed: held
    const Streak back = streak_with_freezes(played({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 40}), 40);
    CHECK(back.current == 1);
    CHECK(back.freezes == 1);
    CHECK(back.longest == 10);
}

TEST_CASE("streak freezes: the cap of five holds when more are earned, and a gap of five uses them all") {
    const Streak s = streak_with_freezes(played({1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 16}), 16); // five missed
    CHECK(s.current == 11);
    CHECK(s.freezes == 1); // 5 - 5 + 1
    CHECK(s.frozen_days == 5);
}

TEST_CASE("streak freezes: playing every other day never breaks it (each miss costs what the day before earned)") {
    std::vector<std::int32_t> days;
    for (std::int32_t d = 1; d <= 61; d += 2) {
        days.push_back(d);
    }
    std::vector<RunRecord> runs;
    for (const std::int32_t d : days) {
        RunSpec spec;
        spec.day = d;
        runs.push_back(make(spec));
    }
    const Streak s = streak_with_freezes(runs, 61);
    CHECK(s.current == 31); // the days played, not the calendar days
    CHECK(s.freezes == 1);
    CHECK(s.longest == 31);
    // Every third day does break it, once nothing is banked.
    std::vector<RunRecord> sparse;
    for (std::int32_t d = 1; d <= 31; d += 3) {
        RunSpec spec;
        spec.day = d;
        sparse.push_back(make(spec));
    }
    CHECK(streak_with_freezes(sparse, 31).current < 11);
}

TEST_CASE("streak freezes: the streak rule decides which days count; the strict streak is unchanged") {
    // Days of two minutes do not count when the rule asks for five.
    const std::vector<RunRecord> runs = played({1, 2, 3}, 2.0);
    CHECK(streak_with_freezes(runs, 3, StreakRule::daily_goal(5)).current == 0);
    CHECK(streak_with_freezes(runs, 3).current == 3);
    CHECK(practice_streak(played({1, 3}), 3).current == 1); // strict: the gap ends it
    CHECK(streak_with_freezes(played({1, 3}), 3).current == 2);
}

TEST_CASE("streak freezes: the maintainer's worked example, step by step") {
    // Ten days played (days 1..10): the streak is 10 and the five freezes are all banked.
    std::vector<std::int32_t> days{1, 2, 3, 4, 5, 6, 7, 8, 9, 10};
    const auto run = [&](std::int32_t today) {
        std::vector<RunRecord> v;
        for (const std::int32_t d : days) {
            RunSpec s;
            s.day = d;
            s.minutes = 5.0;
            v.push_back(make(s));
        }
        return streak_with_freezes(v, today);
    };
    const Streak ten = run(10);
    CHECK(ten.current == 10);
    CHECK(ten.freezes == kMaxStreakFreezes);
    // A missed day spends a freeze rather than earning one: one missed day leaves four, the streak holds.
    const Streak one_missed = run(12); // day 11 missed, day 12 not yet played
    CHECK(one_missed.current == 10);
    CHECK(one_missed.freezes == 4);
    // Five missed days (11..15) use all five: the streak stays at 10 and they hold none.
    const Streak five_missed = run(16);
    CHECK(five_missed.current == 10);
    CHECK(five_missed.freezes == 0);
    CHECK(five_missed.at_risk);
    // They play on the sixth day (16): the streak is 11 and they hold one freeze.
    days.push_back(16);
    const Streak sixth = run(16);
    CHECK(sixth.current == 11);
    CHECK(sixth.freezes == 1);
    // The next day (17): 12 and two freezes.
    days.push_back(17);
    const Streak next = run(17);
    CHECK(next.current == 12);
    CHECK(next.freezes == 2);
}

TEST_CASE("streak freezes: one more missed day with none left breaks the streak") {
    // Ten days played, five missed (11..15): held at 10 with 0 freezes. Day 16 missed as well: broken.
    std::vector<RunRecord> v;
    for (std::int32_t d = 1; d <= 10; ++d) {
        RunSpec s;
        s.day = d;
        s.minutes = 5.0;
        v.push_back(make(s));
    }
    const Streak held = streak_with_freezes(v, 16);
    CHECK(held.current == 10);
    CHECK(held.freezes == 0);
    const Streak broken = streak_with_freezes(v, 17); // day 16 passed unplayed
    CHECK(broken.current == 0);
    CHECK(broken.freezes == 0);
    CHECK(broken.longest == 10);
    // Playing again starts a new streak of 1.
    RunSpec s;
    s.day = 17;
    s.minutes = 5.0;
    v.push_back(make(s));
    CHECK(streak_with_freezes(v, 17).current == 1);
}
