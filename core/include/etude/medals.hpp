// SPDX-License-Identifier: GPL-3.0-or-later
// The achievements: a catalogue of badges with eight tiers each, a material and a title for every
// tier, and the overall title a player's medals add up to. Pure data and arithmetic over it (no clock,
// no drawing; medal_art.hpp draws the medals). evaluate_medals() (medal_eval.hpp) works out the tiers
// from the practice history.
#pragma once

#include "etude/medal_art.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace etude {

// ---- The eight tiers ------------------------------------------------------------------------------

inline constexpr int kTierCount = 8;

// Tier 1 .. 8: Wood, Iron, Bronze, Silver, Gold, Platinum, Diamond, Obsidian; 0 is no medal yet.
Material tier_material(int tier);        // the material of a tier (1 .. 8; others are clamped)
std::string_view tier_material_name(int tier); // "Wood" ... "Obsidian"
std::string_view tier_title(int tier);         // "Beginner" ... "Virtuoso" (a view of a literal: null-terminated, for drawing)

// ---- The catalogue --------------------------------------------------------------------------------

enum class MedalCategory : std::uint8_t { Streaks, Accuracy, Songs, Time, Milestones };
inline constexpr std::size_t kMedalCategoryCount = 5;

std::string_view medal_category_name(MedalCategory category);

enum class Achievement : std::uint8_t {
    // Streaks
    Streak,       // practise N days in a row
    GoalGetter,   // meet the daily goal on N days
    FullWeek,     // meet the goal on all seven days of a week, N times
    Comeback,     // play again after a week or more away, N times
    // Accuracy
    OnTarget,     // N sections played at 100%
    Sharp,        // a full run at N% accuracy
    SteadyHands,  // N runs in a row at 85% accuracy or better
    FullTempo,    // N runs at 100% speed and 90% accuracy or better
    PersonalBest, // beat your own best score on a song N times
    BigScore,     // N points in one run
    // Songs
    Repertoire,   // N songs played through at 80% accuracy or better
    Mastered,     // N songs at 95% accuracy and 100% speed
    Explorer,     // N different songs
    OldFriend,    // one song on N different days
    LongHaul,     // a piece of N minutes played through
    BothHands,    // N full runs with both hands at 85% accuracy or better
    // Practice time
    Minutes,      // N minutes practised in all
    LongSession,  // N minutes in one day
    Runs,         // N runs
    SectionWork,  // N clean passes of a loop
    Regular,      // practise on 3 or more days of a week, N weeks
    // Milestones
    Finisher,     // N full runs
    Anniversary,  // keep a profile for N days
    Collector,    // hold N other achievements
};

inline constexpr std::size_t kAchievementCount = 24;

// What the number of a tier counts, so a threshold can be said in words ("30 days", "10 h").
enum class MedalUnit : std::uint8_t { Days, Weeks, Times, Percent, Points, Songs, Runs, FullRuns, Minutes, Badges };

struct AchievementInfo {
    Achievement id;
    MedalCategory category;
    std::string_view key;  // stable, for storage and tests
    std::string_view name; // "Streak" (a view of a literal: null-terminated, for drawing)
    std::string_view icon; // a Lucide icon name, which the app maps to its glyph
    std::string_view rule; // "Practise {n} days in a row"; {n} is the tier's number
    MedalUnit unit;
    std::array<std::int64_t, kTierCount> tiers; // what tier 1 .. 8 asks for, strictly rising
};

const AchievementInfo& achievement_info(Achievement a);
std::string_view medal_unit_name(MedalUnit unit, std::int64_t n); // "days", "day", "h"...

// The highest tier (0 .. 8) whose threshold `value` reaches.
int tier_for(const AchievementInfo& a, std::int64_t value);
// What the next tier asks for, or 0 when tier 8 is held.
std::int64_t next_threshold(const AchievementInfo& a, int tier);

// A threshold in words: "7 days", "1 week", "100%", "1,300 points", "10 h" (Minutes read in hours from
// 60), the rule with {n} filled in.
std::string threshold_words(const AchievementInfo& a, std::int64_t n);
std::string rule_words(const AchievementInfo& a, int tier);

// ---- The overall title ---------------------------------------------------------------------------

// A player's medals: the sum of the tiers of all their achievements (reaching tier 5 on a badge means
// holding its five medals, Wood to Gold). The title is the highest of the eight whose share of the most
// medals possible (8 for each achievement) the player has reached.
inline constexpr int kMaxMedalPoints = kTierCount * static_cast<int>(kAchievementCount);

// The share of kMaxMedalPoints each title (1 .. 8) asks for.
inline constexpr std::array<double, kTierCount> kTitleShare{0.0, 0.05, 0.15, 0.30, 0.50, 0.70, 0.80, 0.90};

int title_points(int title);   // the medals a title (1 .. 8) asks for
int title_for_points(int points); // 1 .. 8

} // namespace etude
