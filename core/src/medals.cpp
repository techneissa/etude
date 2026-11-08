// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/medals.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

constexpr std::array<std::string_view, kTierCount> kMaterialNames{"Wood",   "Iron",     "Bronze",  "Silver",
                                                                  "Gold",   "Platinum", "Diamond", "Obsidian"};
constexpr std::array<std::string_view, kTierCount> kTitles{"Beginner", "Student", "Performer", "Soloist",
                                                           "Pianist", "Concert Pianist", "Maestro", "Virtuoso"};

using T = std::array<std::int64_t, kTierCount>;
using A = Achievement;
using C = MedalCategory;
using U = MedalUnit;

// Tier 3 (Bronze) is within the first weeks, Gold (5) takes months, Obsidian (8) is for years of use.
constexpr std::array<AchievementInfo, kAchievementCount> kCatalogue{{
    {A::Streak, C::Streaks, "streak", "Streak", "flame", "Practise {n} in a row", U::Days, T{2, 4, 7, 14, 30, 60, 100, 365}},
    {A::GoalGetter, C::Streaks, "goal", "Goal getter", "calendar-check", "Meet the daily goal on {n}", U::Days, T{3, 7, 15, 30, 60, 120, 250, 500}},
    {A::FullWeek, C::Streaks, "fullweek", "Full week", "calendar-range", "Meet the goal on all 7 days of a week, {n}", U::Times, T{1, 2, 3, 5, 8, 12, 26, 52}},
    {A::Comeback, C::Streaks, "comeback", "Comeback", "rotate-ccw", "Come back to a song after a week away, {n}", U::Times, T{1, 2, 3, 5, 7, 10, 15, 20}},
    {A::OnTarget, C::Accuracy, "ontarget", "On target", "target", "Play a section at 100%, {n}", U::Times, T{1, 3, 10, 25, 50, 100, 250, 500}},
    {A::Sharp, C::Accuracy, "sharp", "Sharp", "crosshair", "Finish a full run at {n}", U::Percent, T{60, 75, 85, 90, 95, 98, 99, 100}},
    {A::SteadyHands, C::Accuracy, "steady", "Steady play", "hand", "{n} in a row at 85% or better", U::FullRuns, T{2, 3, 5, 8, 12, 20, 35, 50}},
    {A::FullTempo, C::Accuracy, "tempo", "Full tempo", "gauge", "{n} at full speed and 90% or better", U::Runs, T{1, 3, 10, 25, 50, 100, 200, 500}},
    {A::PersonalBest, C::Accuracy, "pb", "New best", "star", "Beat your best score on a song, {n}", U::Times, T{1, 3, 5, 10, 25, 50, 100, 250}},
    {A::BigScore, C::Accuracy, "bigscore", "Big score", "zap", "Score {n} in one run", U::Points, T{300, 500, 700, 900, 1100, 1250, 1350, 1400}},
    {A::Repertoire, C::Songs, "repertoire", "Repertoire", "list-music", "Play {n} through at 80% or better", U::Songs, T{1, 3, 5, 10, 20, 35, 60, 100}},
    {A::Mastered, C::Songs, "mastered", "Mastered", "gem", "Play {n} at 95% and full speed", U::Songs, T{1, 2, 3, 5, 8, 15, 30, 50}},
    {A::Explorer, C::Songs, "explorer", "Explorer", "compass", "Try {n}", U::Songs, T{2, 5, 10, 20, 35, 50, 100, 200}},
    {A::OldFriend, C::Songs, "oldfriend", "Old friend", "heart", "Play one song on {n}", U::Days, T{2, 3, 5, 10, 15, 30, 60, 100}},
    {A::LongHaul, C::Songs, "longhaul", "Long haul", "mountain", "Play a piece of {n} through", U::Minutes, T{1, 2, 3, 4, 6, 8, 10, 15}},
    {A::BothHands, C::Songs, "bothhands", "Both hands", "handshake", "{n} with both hands at 85% or better", U::Runs, T{1, 3, 10, 25, 50, 100, 200, 500}},
    {A::Minutes, C::Time, "minutes", "Minutes", "clock", "Practise {n} in all", U::Minutes, T{15, 30, 60, 300, 600, 1500, 3000, 6000}},
    {A::LongSession, C::Time, "session", "Long day", "hourglass", "Practise {n} in one day", U::Minutes, T{10, 20, 30, 45, 60, 90, 120, 180}},
    {A::Runs, C::Time, "runs", "Runs", "repeat", "Complete {n}", U::Runs, T{1, 5, 10, 50, 100, 250, 500, 1000}},
    {A::SectionWork, C::Time, "loops", "Section work", "scissors", "Play a looped section cleanly, {n}", U::Times, T{5, 10, 25, 50, 100, 250, 500, 1000}},
    {A::Regular, C::Time, "regular", "Regular", "calendar-days", "Practise 3 days or more a week for {n}", U::Weeks, T{1, 2, 4, 8, 12, 26, 40, 52}},
    {A::Finisher, C::Milestones, "finisher", "Finisher", "medal", "Play {n} all the way through", U::Runs, T{1, 3, 10, 25, 50, 100, 250, 500}},
    {A::Anniversary, C::Milestones, "anniversary", "Anniversary", "cake", "Keep your profile for {n}", U::Days, T{7, 30, 90, 180, 365, 730, 1095, 1825}},
    {A::Collector, C::Milestones, "collector", "Collector", "sparkles", "Hold {n} other achievements", U::Badges, T{3, 6, 9, 12, 15, 18, 21, 23}},
}};

std::string thousands(std::int64_t n) {
    std::string digits = std::to_string(n);
    std::string out;
    for (std::size_t i = 0; i < digits.size(); ++i) {
        if (i > 0 && (digits.size() - i) % 3 == 0) {
            out += ',';
        }
        out += digits[i];
    }
    return out;
}

} // namespace

Material tier_material(int tier) { return static_cast<Material>(std::clamp(tier, 1, kTierCount) - 1); }
std::string_view tier_material_name(int tier) { return kMaterialNames[static_cast<std::size_t>(std::clamp(tier, 1, kTierCount) - 1)]; }
std::string_view tier_title(int tier) { return kTitles[static_cast<std::size_t>(std::clamp(tier, 1, kTierCount) - 1)]; }

std::string_view medal_category_name(MedalCategory category) {
    switch (category) {
    case MedalCategory::Streaks:
        return "Streaks";
    case MedalCategory::Accuracy:
        return "Accuracy";
    case MedalCategory::Songs:
        return "Songs";
    case MedalCategory::Time:
        return "Practice time";
    case MedalCategory::Milestones:
        return "Milestones";
    }
    return {};
}

const AchievementInfo& achievement_info(Achievement a) { return kCatalogue[static_cast<std::size_t>(a)]; }

std::string_view medal_unit_name(MedalUnit unit, std::int64_t n) {
    const bool one = n == 1;
    switch (unit) {
    case MedalUnit::Days:
        return one ? "day" : "days";
    case MedalUnit::Weeks:
        return one ? "week" : "weeks";
    case MedalUnit::Times:
        return one ? "time" : "times";
    case MedalUnit::Percent:
        return "%";
    case MedalUnit::Points:
        return one ? "point" : "points";
    case MedalUnit::Songs:
        return one ? "song" : "songs";
    case MedalUnit::Runs:
        return one ? "run" : "runs";
    case MedalUnit::FullRuns:
        return one ? "full run" : "full runs";
    case MedalUnit::Minutes:
        return "min";
    case MedalUnit::Badges:
        return one ? "achievement" : "achievements";
    }
    return {};
}

int tier_for(const AchievementInfo& a, std::int64_t value) {
    int tier = 0;
    for (int i = 0; i < kTierCount; ++i) {
        if (value >= a.tiers[static_cast<std::size_t>(i)]) {
            tier = i + 1;
        }
    }
    return tier;
}

std::int64_t next_threshold(const AchievementInfo& a, int tier) {
    return tier >= kTierCount ? 0 : a.tiers[static_cast<std::size_t>(std::max(tier, 0))];
}

std::string threshold_words(const AchievementInfo& a, std::int64_t n) {
    if (a.unit == MedalUnit::Percent) {
        return std::to_string(n) + "%";
    }
    if (a.unit == MedalUnit::Minutes && n >= 60) {
        const std::int64_t h = n / 60;
        const std::int64_t m = n % 60;
        return std::to_string(h) + " h" + (m != 0 ? " " + std::to_string(m) + " min" : std::string());
    }
    return thousands(n) + " " + std::string(medal_unit_name(a.unit, n));
}

std::string rule_words(const AchievementInfo& a, int tier) {
    std::string rule(a.rule);
    const std::string words = threshold_words(a, a.tiers[static_cast<std::size_t>(std::clamp(tier, 1, kTierCount) - 1)]);
    const std::size_t at = rule.find("{n}");
    if (at != std::string::npos) {
        rule.replace(at, 3, words);
    }
    return rule;
}

int title_points(int title) {
    const double share = kTitleShare[static_cast<std::size_t>(std::clamp(title, 1, kTierCount) - 1)];
    return static_cast<int>(std::ceil(share * kMaxMedalPoints - 1e-9));
}

int title_for_points(int points) {
    int title = 1;
    for (int t = 2; t <= kTierCount; ++t) {
        if (points >= title_points(t)) {
            title = t;
        }
    }
    return title;
}

} // namespace etude
