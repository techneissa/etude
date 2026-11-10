// SPDX-License-Identifier: GPL-3.0-or-later
// What a player's runs have earned: the number each achievement of medals.hpp counts and the tier it
// reaches. Pure functions over the runs (oldest first, as the history keeps them), like progress.hpp:
// the history thread works them out for every profile and caches them in the progress view, and the
// app never evaluates a rule in a frame.
#pragma once

#include "etude/medals.hpp"
#include "etude/run_record.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <unordered_map>
#include <vector>

namespace etude {

// What the rules need besides the runs.
// The piece each song belongs to: a song's content hash to the key of the group of versions it is in
// (the easy and the original arrangement, a joined pair), the same key for every member. A song not in
// the map is its own piece.
using PieceMap = std::unordered_map<std::uint64_t, std::uint64_t>;

struct MedalContext {
    std::int32_t today = 0;          // the local day key; runs dated after it are left out
    int goal_minutes = 0;            // the profile's daily goal (0: none, so no goal medals)
    std::int64_t profile_created_utc_s = 0; // 0: not known, Anniversary counts nothing
    std::int32_t utc_offset_s = 0;   // seconds east of UTC here, the day Anniversary starts from is a local one
    const PieceMap* pieces = nullptr; // versions of one piece count as one song (null: every file is its own)
};

// A run counts for a medal when it is scored (a run from before scores is a fresh start), has a song
// and enough graded notes to be an attempt, and is not dated after today; a repeat of the same run is
// counted once.
inline constexpr int kMedalFullRunPercent = 95; // a run covering this much of the song is a full run

struct MedalSet {
    std::array<std::int64_t, kAchievementCount> value{}; // what each rule counted
    std::array<std::uint8_t, kAchievementCount> tier{};  // 0 .. 8 reached

    std::int64_t value_of(Achievement a) const { return value[static_cast<std::size_t>(a)]; }
    int held() const; // achievements at tier 1 or more
    int points() const; // the sum of the tiers
    int title() const { return title_for_points(points()); } // 1 .. 8

    friend bool operator==(const MedalSet&, const MedalSet&) = default;
};

// The points a category can give (its achievements x 8) and what a set holds of it: the ring's arcs.
int category_points_possible(MedalCategory category);
int category_points(const MedalSet& set, MedalCategory category);

MedalSet evaluate_medals(std::span<const RunRecord> runs, const MedalContext& context);

// The achievements nearest their next tier, the nearest first: the share of the way from the tier
// held to the next one, 0 .. 1 (an achievement at tier 8, or with no progress at all, is left out).
struct MedalProgress {
    Achievement achievement = Achievement::Streak;
    int tier = 0;            // held
    std::int64_t value = 0;
    std::int64_t next = 0;   // what the next tier asks for
    double share = 0.0;      // from the tier held (or from 0) to `next`
};
std::vector<MedalProgress> closest_to_next(const MedalSet& set, std::size_t count);

// The daily goal in a profile's settings text (core profile_settings_text); -1 when it names none (the
// profile then has the goal every profile starts with).
int goal_minutes_of_settings(std::string_view settings_text);

} // namespace etude
