// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/medal_eval.hpp"

#include "etude/leaderboard.hpp"
#include "etude/progress.hpp"

#include <algorithm>
#include <cstdlib>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace etude {

namespace {

constexpr int kComebackGapDays = 8; // seven days without practice, then a run
constexpr int kRegularDaysPerWeek = 3;
constexpr int kSteadyPercent = 85;
constexpr int kTempoPercent = 90;
constexpr int kRepertoirePercent = 80;
constexpr int kMasteredPercent = 95;

bool is_full(const RunRecord& run) { return run.covered_percent >= kMedalFullRunPercent; }

// A section is a run that covers part of the song; it counts when it has as many graded notes as any
// attempt does (`kMinGradedForBest`, also what `eligible` asks).
constexpr int kPerfectSectionMinNotes = kMinGradedForBest;

bool is_perfect_section(const RunRecord& run) {
    return run.covered_percent > 0 && run.covered_percent < kMedalFullRunPercent &&
           run.all.graded >= kPerfectSectionMinNotes && run.all.hit == run.all.graded && run.wrong == 0;
}

// The piece a song is part of (its own, when the library does not group it).
std::uint64_t piece_of(const MedalContext& context, std::uint64_t song_hash) {
    if (context.pieces != nullptr) {
        const auto it = context.pieces->find(song_hash);
        if (it != context.pieces->end()) {
            return it->second;
        }
    }
    return song_hash;
}

// Whole percent of the graded notes hit, rounded down (so 100 is every note).
int percent_of(const RunRecord& run) {
    return run.all.graded > 0 ? run.all.hit * 100 / run.all.graded : 0;
}

// Weeks run Monday to Sunday: 1970-01-01 (day 0) was a Thursday.
std::int64_t week_of(std::int32_t day) {
    const std::int64_t shifted = static_cast<std::int64_t>(day) + 3;
    return shifted >= 0 ? shifted / 7 : -((-shifted + 6) / 7);
}

// The runs that count, in the order they happened.
std::vector<RunRecord> eligible(std::span<const RunRecord> runs, std::int32_t today) {
    std::vector<RunRecord> out;
    for (const std::size_t i : unique_run_indices(runs)) {
        const RunRecord& run = runs[i];
        if (is_scored(run) && run.song_hash != 0 && run.all.graded >= kMinGradedForBest && run.local_day <= today) {
            out.push_back(run);
            out.back().measures.clear(); // nothing here reads the bars
        }
    }
    std::stable_sort(out.begin(), out.end(), [](const RunRecord& a, const RunRecord& b) {
        return a.local_day != b.local_day ? a.local_day < b.local_day : a.start_utc_s < b.start_utc_s;
    });
    return out;
}

} // namespace

int MedalSet::held() const {
    return static_cast<int>(std::count_if(tier.begin(), tier.end(), [](std::uint8_t t) { return t > 0; }));
}

int MedalSet::points() const {
    int sum = 0;
    for (const std::uint8_t t : tier) {
        sum += t;
    }
    return sum;
}

namespace {

void put(MedalSet& out, Achievement a, std::int64_t value) { out.value[static_cast<std::size_t>(a)] = value; }

// Streaks: days and weeks.
void streak_values(MedalSet& out, std::span<const RunRecord> runs, const std::vector<DayPractice>& days,
                   const MedalContext& context) {
    // The streak is the profile's, counted as the tiles count it (any practice, with freezes); the weeks below stay strict.
    put(out, Achievement::Streak, profile_streak(runs, context.today).longest);
    const std::int64_t goal_ms = static_cast<std::int64_t>(context.goal_minutes) * 60'000;
    std::set<std::int32_t> met_days;
    if (goal_ms > 0) {
        for (const DayPractice& d : days) {
            if (d.practised_ms >= goal_ms) {
                met_days.insert(d.day);
            }
        }
    }
    put(out, Achievement::GoalGetter, static_cast<std::int64_t>(met_days.size()));
    std::map<std::int64_t, int> met_in_week;
    for (const std::int32_t day : met_days) {
        ++met_in_week[week_of(day)];
    }
    std::int64_t full_weeks = 0;
    for (const auto& [week, count] : met_in_week) {
        full_weeks += count == 7 ? 1 : 0;
    }
    put(out, Achievement::FullWeek, full_weeks);

}

void accuracy_values(MedalSet& out, const std::vector<RunRecord>& list) {
    std::int64_t on_target = 0;
    std::int64_t sharp = 0;
    std::int64_t steady = 0;
    std::int64_t chain = 0;
    std::int64_t tempo = 0;
    std::int64_t big = 0;
    std::int64_t personal_bests = 0;
    std::unordered_map<std::uint64_t, int> best_score;
    for (const RunRecord& run : list) {
        on_target += is_perfect_section(run) ? 1 : 0;
        big = std::max<std::int64_t>(big, run.score);
        const auto it = best_score.find(run.song_hash);
        if (it == best_score.end()) {
            best_score[run.song_hash] = run.score;
        } else if (run.score > it->second) {
            ++personal_bests;
            it->second = run.score;
        }
        if (!is_full(run)) {
            continue;
        }
        const int pct = percent_of(run);
        sharp = std::max<std::int64_t>(sharp, pct);
        chain = pct >= kSteadyPercent ? chain + 1 : 0;
        steady = std::max(steady, chain);
        tempo += run.speed_percent >= 100 && pct >= kTempoPercent ? 1 : 0;
    }
    put(out, Achievement::OnTarget, on_target);
    put(out, Achievement::Sharp, sharp);
    put(out, Achievement::SteadyHands, steady);
    put(out, Achievement::FullTempo, tempo);
    put(out, Achievement::PersonalBest, personal_bests);
    put(out, Achievement::BigScore, big);

}

// Songs, and what they share with the rest: the number of full runs (the Finisher).
std::int64_t song_values(MedalSet& out, const std::vector<RunRecord>& list, const MedalContext& context) {
    std::unordered_set<std::uint64_t> repertoire;
    std::unordered_set<std::uint64_t> mastered;
    std::unordered_set<std::uint64_t> explored;
    std::unordered_map<std::uint64_t, std::set<std::int32_t>> song_days;
    std::int64_t longest_ms = 0;
    std::int64_t both_hands = 0;
    std::int64_t finished = 0;
    for (const RunRecord& run : list) {
        const std::uint64_t piece = piece_of(context, run.song_hash); // versions of one piece are one song
        explored.insert(piece);
        song_days[piece].insert(run.local_day);
        if (!is_full(run)) {
            continue;
        }
        const int pct = percent_of(run);
        ++finished;
        if (pct >= kRepertoirePercent) {
            repertoire.insert(piece);
        }
        if (pct >= kMasteredPercent && run.speed_percent >= 100) {
            mastered.insert(piece);
        }
        longest_ms = std::max(longest_ms, run.duration_ms);
        both_hands += run.hands == HandFilter::Both && pct >= kSteadyPercent ? 1 : 0;
    }
    // Comeback: a run of a song after seven days or more without playing it counts once (a song, not the day).
    std::int64_t comebacks = 0;
    for (const auto& [hash, set] : song_days) {
        std::int32_t previous = 0;
        bool first = true;
        for (const std::int32_t day : set) {
            comebacks += !first && day - previous >= kComebackGapDays ? 1 : 0;
            previous = day;
            first = false;
        }
    }
    put(out, Achievement::Comeback, comebacks);
    std::int64_t old_friend = 0;
    for (const auto& [hash, set] : song_days) {
        old_friend = std::max<std::int64_t>(old_friend, static_cast<std::int64_t>(set.size()));
    }
    put(out, Achievement::Repertoire, static_cast<std::int64_t>(repertoire.size()));
    put(out, Achievement::Mastered, static_cast<std::int64_t>(mastered.size()));
    put(out, Achievement::Explorer, static_cast<std::int64_t>(explored.size()));
    put(out, Achievement::OldFriend, old_friend);
    put(out, Achievement::LongHaul, longest_ms / 60'000);
    put(out, Achievement::BothHands, both_hands);

    return finished;
}

void time_values(MedalSet& out, const std::vector<RunRecord>& list, const std::vector<DayPractice>& days) {
    std::int64_t total_ms = 0;
    std::int64_t best_day_ms = 0;
    std::int64_t loops = 0;
    std::map<std::int64_t, int> days_in_week;
    for (const DayPractice& d : days) {
        total_ms += d.practised_ms;
        best_day_ms = std::max(best_day_ms, d.practised_ms);
        ++days_in_week[week_of(d.day)];
    }
    for (const RunRecord& run : list) {
        loops += run.clean_passes;
    }
    std::int64_t regular = 0;
    for (const auto& [week, count] : days_in_week) {
        regular += count >= kRegularDaysPerWeek ? 1 : 0;
    }
    put(out, Achievement::Minutes, total_ms / 60'000);
    put(out, Achievement::LongSession, best_day_ms / 60'000);
    put(out, Achievement::Runs, static_cast<std::int64_t>(list.size()));
    put(out, Achievement::SectionWork, loops);
    put(out, Achievement::Regular, regular);

}

} // namespace

MedalSet evaluate_medals(std::span<const RunRecord> runs, const MedalContext& context) {
    MedalSet out;
    const std::vector<RunRecord> list = eligible(runs, context.today);
    const std::vector<DayPractice> days = daily_practice(list);
    streak_values(out, runs, days, context);
    accuracy_values(out, list);
    const std::int64_t finished = song_values(out, list, context);
    time_values(out, list, days);

    // Milestones.
    put(out, Achievement::Finisher, finished);
    if (context.profile_created_utc_s > 0) {
        const std::int64_t created_day = local_day_from_utc(context.profile_created_utc_s, context.utc_offset_s);
        put(out, Achievement::Anniversary, std::max<std::int64_t>(0, context.today - created_day));
    }

    // Tiers, then the Collector, which counts the others.
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        if (static_cast<Achievement>(i) == Achievement::Collector) {
            continue;
        }
        out.tier[i] = static_cast<std::uint8_t>(tier_for(achievement_info(static_cast<Achievement>(i)), out.value[i]));
    }
    put(out, Achievement::Collector, out.held());
    const std::size_t collector = static_cast<std::size_t>(Achievement::Collector);
    out.tier[collector] = static_cast<std::uint8_t>(tier_for(achievement_info(Achievement::Collector), out.value[collector]));
    return out;
}

std::vector<MedalGain> new_medals(const MedalSet& before, const MedalSet& after) {
    std::vector<MedalGain> gained;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        if (after.tier[i] > before.tier[i]) {
            gained.push_back({static_cast<Achievement>(i), after.tier[i]});
        }
    }
    return gained;
}

std::vector<MedalProgress> closest_to_next(const MedalSet& set, std::size_t count) {
    std::vector<MedalProgress> out;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        const AchievementInfo& info = achievement_info(static_cast<Achievement>(i));
        const int tier = set.tier[i];
        const std::int64_t value = set.value[i];
        const std::int64_t next = next_threshold(info, tier);
        if (next <= 0 || value <= 0) {
            continue;
        }
        const std::int64_t from = tier > 0 ? info.tiers[static_cast<std::size_t>(tier - 1)] : 0;
        MedalProgress p;
        p.achievement = info.id;
        p.tier = tier;
        p.value = value;
        p.next = next;
        p.share = std::clamp(static_cast<double>(value - from) / static_cast<double>(next - from), 0.0, 1.0);
        out.push_back(p);
    }
    std::stable_sort(out.begin(), out.end(), [](const MedalProgress& a, const MedalProgress& b) {
        return a.share > b.share;
    });
    if (out.size() > count) {
        out.resize(count);
    }
    return out;
}

int goal_minutes_of_settings(std::string_view text) {
    constexpr std::string_view kKey = "daily_goal_minutes";
    std::size_t at = 0;
    while ((at = text.find(kKey, at)) != std::string_view::npos) {
        const bool line_start = at == 0 || text[at - 1] == '\n';
        std::size_t p = at + kKey.size();
        while (p < text.size() && (text[p] == ' ' || text[p] == '\t')) {
            ++p;
        }
        if (line_start && p < text.size() && text[p] == '=') {
            ++p;
            while (p < text.size() && text[p] == ' ') {
                ++p;
            }
            const std::string digits(text.substr(p, std::min<std::size_t>(6, text.size() - p)));
            char* end = nullptr;
            const long v = std::strtol(digits.c_str(), &end, 10);
            return end != digits.c_str() ? static_cast<int>(std::clamp<long>(v, 0, kMaxDailyGoalMinutes)) : -1;
        }
        at += kKey.size();
    }
    return -1;
}

int category_points_possible(MedalCategory category) {
    int n = 0;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        n += achievement_info(static_cast<Achievement>(i)).category == category ? 1 : 0;
    }
    return n * kTierCount;
}

int category_points(const MedalSet& set, MedalCategory category) {
    int sum = 0;
    for (std::size_t i = 0; i < kAchievementCount; ++i) {
        if (achievement_info(static_cast<Achievement>(i)).category == category) {
            sum += set.tier[i];
        }
    }
    return sum;
}

} // namespace etude
