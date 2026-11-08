// SPDX-License-Identifier: GPL-3.0-or-later
// The achievements' catalogue: eight rising thresholds each, the tier of a value, the material and
// title of a tier, the overall title from medal points, and the words (medals.hpp).
#include "etude/medals.hpp"

#include <doctest/doctest.h>

#include <set>
#include <string>

using namespace etude;

namespace {

const AchievementInfo& info(std::size_t i) { return achievement_info(static_cast<Achievement>(i)); }

} // namespace

TEST_CASE("medals: every achievement has eight thresholds, strictly rising, and a place in the table") {
    std::set<std::string_view> keys;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        const AchievementInfo& a = info(i);
        CAPTURE(a.key);
        CHECK(static_cast<std::size_t>(a.id) == i); // the table is in the enum's order
        CHECK(a.tiers[0] >= 1);
        for (std::size_t t = 1; t < a.tiers.size(); ++t) {
            CHECK(a.tiers[t] > a.tiers[t - 1]);
        }
        CHECK(keys.insert(a.key).second); // keys are unique
        CHECK_FALSE(a.name.empty());
        CHECK_FALSE(a.icon.empty());
        CHECK(a.rule.find("{n}") != std::string_view::npos);
    }
    CHECK(kAchievementCount == 24);
    CHECK(kMaxMedalPoints == 192);
}

TEST_CASE("medals: a value reaches a tier at its threshold and not a step before, for every achievement") {
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        const AchievementInfo& a = info(i);
        CAPTURE(a.key);
        CHECK(tier_for(a, 0) == 0);
        for (int t = 1; t <= kTierCount; ++t) {
            const std::int64_t need = a.tiers[static_cast<std::size_t>(t - 1)];
            CAPTURE(t);
            CHECK(tier_for(a, need) == t);
            CHECK(tier_for(a, need - 1) == t - 1);
            CHECK(tier_for(a, need + 1) >= t);
        }
        CHECK(tier_for(a, a.tiers.back() * 10) == kTierCount);
    }
}

TEST_CASE("medals: the next threshold is the one above the tier held, none at the top") {
    const AchievementInfo& streak = achievement_info(Achievement::Streak);
    CHECK(next_threshold(streak, 0) == 2);
    CHECK(next_threshold(streak, 3) == 14);
    CHECK(next_threshold(streak, 7) == 365);
    CHECK(next_threshold(streak, 8) == 0);
}

TEST_CASE("medals: the eight tiers are wood, iron, bronze, silver, gold, platinum, diamond and obsidian with "
          "their titles") {
    const char* materials[] = {"Wood", "Iron", "Bronze", "Silver", "Gold", "Platinum", "Diamond", "Obsidian"};
    const char* titles[] = {"Beginner", "Student", "Performer", "Soloist", "Pianist", "Concert Pianist", "Maestro", "Virtuoso"};
    for (int t = 1; t <= kTierCount; ++t) {
        CHECK(tier_material_name(t) == materials[t - 1]);
        CHECK(tier_title(t) == titles[t - 1]);
        CHECK(static_cast<int>(tier_material(t)) == t - 1);
    }
    // Out of range is clamped, never out of bounds.
    CHECK(tier_title(0) == "Beginner");
    CHECK(tier_title(99) == "Virtuoso");
}

TEST_CASE("medals: the overall title steps at its shares of the most points, from Beginner to Virtuoso") {
    CHECK(title_points(1) == 0);
    CHECK(title_points(2) == 10);  // 5% of 192, rounded up
    CHECK(title_points(3) == 29);
    CHECK(title_points(4) == 58);
    CHECK(title_points(5) == 96);
    CHECK(title_points(6) == 135);
    CHECK(title_points(7) == 154);
    CHECK(title_points(8) == 173);
    CHECK(title_for_points(0) == 1);
    for (int t = 2; t <= kTierCount; ++t) {
        CHECK(title_for_points(title_points(t)) == t);
        CHECK(title_for_points(title_points(t) - 1) == t - 1);
    }
    CHECK(title_for_points(kMaxMedalPoints) == 8);
    CHECK(title_for_points(10'000) == 8);
    CHECK(title_for_points(-5) == 1);
    for (int t = 2; t <= kTierCount; ++t) {
        CHECK(title_points(t) > title_points(t - 1)); // the steps rise
    }
}

TEST_CASE("medals: thresholds in words, with the unit, thousands and hours") {
    const AchievementInfo& streak = achievement_info(Achievement::Streak);
    CHECK(threshold_words(streak, 1) == "1 day");
    CHECK(threshold_words(streak, 30) == "30 days");
    const AchievementInfo& score = achievement_info(Achievement::BigScore);
    CHECK(threshold_words(score, 1250) == "1,250 points");
    const AchievementInfo& sharp = achievement_info(Achievement::Sharp);
    CHECK(threshold_words(sharp, 95) == "95%");
    const AchievementInfo& minutes = achievement_info(Achievement::Minutes);
    CHECK(threshold_words(minutes, 30) == "30 min");
    CHECK(threshold_words(minutes, 60) == "1 h");
    CHECK(threshold_words(minutes, 90) == "1 h 30 min");
    CHECK(threshold_words(minutes, 6000) == "100 h");
    CHECK(rule_words(streak, 3) == "Practise 7 days in a row");
    CHECK(rule_words(sharp, 5) == "Finish a full run at 95%");
    CHECK(rule_words(minutes, 8) == "Practise 100 h in all");
}

TEST_CASE("medals: Steady play says full runs, everywhere its rule and thresholds are shown") {
    const AchievementInfo& steady = achievement_info(Achievement::SteadyHands);
    CHECK(rule_words(steady, 1) == "2 full runs in a row at 85% or better");
    CHECK(rule_words(steady, 8) == "50 full runs in a row at 85% or better");
    CHECK(threshold_words(steady, 1) == "1 full run");
}
