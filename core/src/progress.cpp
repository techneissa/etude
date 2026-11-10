// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/progress.hpp"

#include <algorithm>
#include <cmath>
#include <map>
#include <tuple>

namespace etude {

namespace {

auto run_key(const RunRecord& r) {
    return std::make_tuple(r.start_utc_s, r.song_hash, static_cast<int>(r.mode),
                           static_cast<int>(r.hands), r.speed_percent, r.transpose_semitones);
}

// `subset` (indices into `runs`, ascending) without the repeats of earlier members, same order.
std::vector<std::size_t> unique_of(std::span<const RunRecord> runs,
                                   const std::vector<std::size_t>& subset) {
    std::vector<std::size_t> order = subset;
    std::sort(order.begin(), order.end(), [&](std::size_t a, std::size_t b) {
        auto ka = run_key(runs[a]);
        auto kb = run_key(runs[b]);
        return ka < kb || (ka == kb && a < b);
    });
    std::vector<char> repeat(runs.size(), 0);
    for (std::size_t i = 1; i < order.size(); ++i) {
        if (run_key(runs[order[i]]) == run_key(runs[order[i - 1]])) {
            repeat[order[i]] = 1;
        }
    }
    std::vector<std::size_t> out;
    out.reserve(subset.size());
    for (std::size_t i : subset) {
        if (!repeat[i]) {
            out.push_back(i);
        }
    }
    return out;
}

std::vector<std::size_t> all_indices(std::size_t n) {
    std::vector<std::size_t> v(n);
    for (std::size_t i = 0; i < n; ++i) {
        v[i] = i;
    }
    return v;
}

// Practice per day over the unique runs, days ascending. Days after `last_day` are left out.
std::vector<DayPractice> day_totals(std::span<const RunRecord> runs, std::int64_t last_day) {
    std::vector<std::size_t> keep = unique_of(runs, all_indices(runs.size()));
    std::vector<std::pair<std::int32_t, std::int64_t>> rows;
    rows.reserve(keep.size());
    for (std::size_t i : keep) {
        if (runs[i].local_day <= last_day) {
            rows.emplace_back(runs[i].local_day, runs[i].duration_ms);
        }
    }
    std::sort(rows.begin(), rows.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::vector<DayPractice> out;
    for (const auto& [day, ms] : rows) {
        if (out.empty() || out.back().day != day) {
            out.push_back({day, 0, 0});
        }
        out.back().practised_ms += ms;
        ++out.back().runs;
    }
    return out;
}

constexpr std::int64_t kNoLimit = INT64_MAX;

std::int64_t goal_ms(int goal_minutes) {
    return std::int64_t{std::clamp(goal_minutes, 0, kMaxDailyGoalMinutes)} * 60'000;
}

} // namespace

bool same_run(const RunRecord& a, const RunRecord& b) { return run_key(a) == run_key(b); }

std::vector<std::size_t> unique_run_indices(std::span<const RunRecord> runs) {
    return unique_of(runs, all_indices(runs.size()));
}

// ---- Daily practice

std::vector<DayPractice> daily_practice(std::span<const RunRecord> runs) {
    return day_totals(runs, kNoLimit);
}

std::vector<DayPractice> recent_days(std::span<const RunRecord> runs, std::int32_t today,
                                     int count) {
    count = std::clamp(count, 0, kMaxRecentDays);
    std::vector<DayPractice> totals = day_totals(runs, today);
    std::vector<DayPractice> out;
    out.reserve(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        std::int32_t day = static_cast<std::int32_t>(std::int64_t{today} - count + 1 + i);
        auto it = std::lower_bound(totals.begin(), totals.end(), day,
                                   [](const DayPractice& d, std::int32_t v) { return d.day < v; });
        if (it != totals.end() && it->day == day) {
            out.push_back(*it);
        } else {
            out.push_back({day, 0, 0});
        }
    }
    return out;
}

DailyGoalProgress daily_goal_progress(std::span<const RunRecord> runs, std::int32_t today,
                                      int goal_minutes) {
    DailyGoalProgress g;
    g.goal_minutes = std::clamp(goal_minutes, 0, kMaxDailyGoalMinutes);
    g.has_goal = g.goal_minutes > 0;
    std::int64_t ms = 0;
    std::vector<DayPractice> totals = day_totals(runs, today);
    if (!totals.empty() && totals.back().day == today) {
        ms = totals.back().practised_ms;
    }
    g.minutes_today = static_cast<double>(ms) / 60'000.0;
    if (g.has_goal) {
        g.fraction =
            std::min(1.0, static_cast<double>(ms) / static_cast<double>(goal_ms(g.goal_minutes)));
        g.met = ms >= goal_ms(g.goal_minutes);
    }
    return g;
}

Streak streak_with_freezes(std::span<const RunRecord> runs, std::int32_t today, const StreakRule& rule) {
    const std::int64_t need = std::max<std::int64_t>(1, rule.min_practised_ms);
    Streak s;
    int count = 0;
    int freezes = 0;
    int frozen = 0;
    std::int64_t last = 0;
    bool any = false;
    for (const DayPractice& d : day_totals(runs, today)) {
        if (d.practised_ms < need) {
            continue;
        }
        if (any) {
            const std::int64_t missed = d.day - last - 1;
            if (missed > freezes) { // a missed day with no freeze left: the streak ends
                count = 0;
                freezes = 0;
                frozen = 0;
            } else {
                freezes -= static_cast<int>(missed);
                frozen += static_cast<int>(missed);
            }
        }
        ++count;
        freezes = std::min(kMaxStreakFreezes, freezes + 1);
        last = d.day;
        any = true;
        s.longest = std::max(s.longest, count);
    }
    if (!any) {
        return s;
    }
    // The days since the last counting one, up to yesterday (today is still to be played).
    const std::int64_t missed = std::max<std::int64_t>(0, std::int64_t{today} - 1 - last);
    if (missed > freezes) {
        return s; // ended: current 0, nothing banked
    }
    s.current = count;
    s.freezes = freezes - static_cast<int>(missed);
    s.frozen_days = frozen + static_cast<int>(missed);
    s.today_done = last == today;
    s.at_risk = !s.today_done;
    return s;
}

Streak profile_streak(std::span<const RunRecord> runs, std::int32_t today) {
    return streak_with_freezes(runs, today, StreakRule::any_practice());
}

// ---- Per-song bests and history

namespace {

std::vector<std::size_t> song_runs(std::span<const RunRecord> runs, std::uint64_t song_hash) {
    std::vector<std::size_t> idx;
    if (song_hash == 0) {
        return idx;
    }
    for (std::size_t i = 0; i < runs.size(); ++i) {
        if (runs[i].song_hash == song_hash) {
            idx.push_back(i);
        }
    }
    return unique_of(runs, idx);
}

// a.hit / a.graded <=> b.hit / b.graded without division: -1, 0 or 1.
int compare_accuracy(const RunHand& a, const RunHand& b) {
    std::int64_t l = std::int64_t{a.hit} * b.graded;
    std::int64_t r = std::int64_t{b.hit} * a.graded;
    return l < r ? -1 : (l > r ? 1 : 0);
}

} // namespace

std::vector<BestEntry> song_bests(std::span<const RunRecord> runs, std::uint64_t song_hash,
                                  int min_graded) {
    min_graded = std::max(1, min_graded);
    struct Best {
        BestEntry entry;
        RunHand hand;
    };
    // One best per mode, hands and exact speed percent. The speed is a whole percent (a typed tempo
    // gives 83, the buttons 85), so a song can have more entries than it did at steps of 5; the
    // library's best per song takes the highest accuracy over all of them (library.cpp), so the
    // player sees one best either way.
    std::map<std::tuple<int, int, int>, Best> best;
    for (std::size_t i : song_runs(runs, song_hash)) {
        const RunRecord& r = runs[i];
        if (r.all.graded < min_graded) {
            continue;
        }
        auto key =
            std::make_tuple(static_cast<int>(r.mode), static_cast<int>(r.hands), r.speed_percent);
        auto [it, fresh] = best.try_emplace(key);
        Best& b = it->second;
        ++b.entry.runs;
        int order = fresh ? 1 : compare_accuracy(r.all, b.hand);
        // Higher accuracy wins; equal accuracy goes to more graded notes, then to the later run
        // (the list is oldest first, so a later one that is not worse replaces).
        if (order > 0 || (order == 0 && r.all.graded >= b.hand.graded)) {
            b.entry.mode = r.mode;
            b.entry.hands = r.hands;
            b.entry.speed_percent = r.speed_percent;
            b.entry.accuracy = r.all.accuracy().value_or(0.0); // graded >= min_graded
            b.entry.graded_notes = r.all.graded;
            b.entry.longest_combo = r.longest_combo;
            b.entry.day = r.local_day;
            b.hand = r.all;
        }
    }
    std::vector<BestEntry> out;
    out.reserve(best.size());
    for (const auto& [key, b] : best) {
        out.push_back(b.entry);
    }
    return out;
}

// ---- Change from the last attempt

AttemptChange compare_with_last_attempt(std::span<const RunRecord> runs, const RunRecord& current) {
    AttemptChange c;
    // Nothing graded returns below, before the accuracy is used.
    c.accuracy = current.all.accuracy().value_or(0.0);
    c.longest_combo = current.longest_combo;
    if (current.song_hash == 0 || current.all.graded <= 0) {
        return c;
    }
    const RunRecord* previous = nullptr;
    bool beats_all = current.all.graded >= kMinGradedForBest;
    for (std::size_t i = runs.size(); i-- > 0;) {
        const RunRecord& r = runs[i];
        if (r.song_hash != current.song_hash || r.mode != current.mode ||
            r.hands != current.hands || r.speed_percent != current.speed_percent ||
            r.all.graded <= 0 || same_run(r, current)) {
            continue;
        }
        if (previous == nullptr) {
            previous = &r;
        }
        if (r.all.graded >= kMinGradedForBest && compare_accuracy(current.all, r.all) <= 0) {
            beats_all = false;
        }
    }
    if (previous == nullptr) {
        return c;
    }
    c.has_previous = true;
    c.previous_accuracy = previous->all.accuracy().value_or(0.0); // graded > 0 here
    c.accuracy_delta = c.accuracy - c.previous_accuracy;
    c.previous_longest_combo = previous->longest_combo;
    c.combo_delta = c.longest_combo - c.previous_longest_combo;
    c.trend = c.accuracy_delta >= kTrendEpsilon    ? Trend::Better
              : c.accuracy_delta <= -kTrendEpsilon ? Trend::Worse
                                                   : Trend::Same;
    c.new_best = beats_all;
    c.previous_day = previous->local_day;
    return c;
}

// ---- Trouble spots

namespace {

struct BarAcc {
    double weight = 0.0; // sum of run weights
    double weighted_rate = 0.0;
    double weighted_missed = 0.0; // the missed share of the rate, weighted
    double weighted_wrong = 0.0;  // the wrong-press share of the rate, weighted
    std::int64_t attempts = 0;
};

} // namespace

TroublePlan trouble_plan(std::span<const RunRecord> runs, std::uint64_t song_hash,
                         const TroubleOptions& options) {
    TroublePlan plan;
    if (song_hash == 0) {
        return plan;
    }
    const std::size_t max_runs = static_cast<std::size_t>(std::clamp(options.max_runs, 1, 1000));
    const double half_life = std::clamp(options.half_life_runs, 0.1, 1000.0);
    const std::int64_t min_attempts = std::max(1, options.min_attempts);
    const double min_heat = std::clamp(options.min_heat, 1e-6, 1.0);
    const std::size_t max_spots = static_cast<std::size_t>(std::clamp(options.max_spots, 0, 100));
    const int max_bars = std::clamp(options.max_loop_bars, 1, 64);

    // The latest runs of the song, newest first. A run with no bars says nothing about any bar and
    // does not age the others.
    std::vector<const RunRecord*> picked;
    for (std::size_t i = runs.size(); i-- > 0 && picked.size() < max_runs;) {
        const RunRecord& r = runs[i];
        if (r.song_hash != song_hash || r.measures.empty()) {
            continue;
        }
        bool repeat = false;
        for (const RunRecord* p : picked) {
            if (same_run(*p, r)) {
                repeat = true;
                break;
            }
        }
        if (!repeat) {
            picked.push_back(&r);
        }
    }
    plan.runs_read = static_cast<int>(picked.size());

    // The bars the runs graded, by measure. A song's measures are consecutive numbers, so a flat
    // table indexed by measure holds them (a map costs a node and a search for each bar of each
    // run: over a long song, milliseconds on a frame); measures far apart (damaged data) use a map.
    int lowest = 0;
    int highest = -1;
    std::int64_t graded_bars = 0;
    for (const RunRecord* r : picked) {
        for (const RunBar& b : r->measures) {
            if (b.graded <= 0) {
                continue;
            }
            if (graded_bars++ == 0) {
                lowest = highest = b.measure;
            } else {
                lowest = std::min(lowest, b.measure);
                highest = std::max(highest, b.measure);
            }
        }
    }
    const std::int64_t span = graded_bars > 0 ? std::int64_t{highest} - lowest + 1 : 0;
    const bool flat = span > 0 && span <= 8 * graded_bars + 64;
    std::vector<BarAcc> table(flat ? static_cast<std::size_t>(span) : 0);
    std::map<int, BarAcc> sparse;
    for (std::size_t age = 0; age < picked.size(); ++age) {
        const double w = std::exp2(-static_cast<double>(age) / half_life);
        for (const RunBar& b : picked[age]->measures) {
            if (b.graded <= 0) {
                continue;
            }
            const double graded = b.graded;
            const double miss_part = b.missed / graded;
            const double wrong_part = kWrongPressWeight * b.wrong / graded;
            const double rate = std::min(1.0, miss_part + wrong_part);
            BarAcc& a = flat ? table[static_cast<std::size_t>(std::int64_t{b.measure} - lowest)]
                             : sparse[b.measure];
            a.weight += w;
            a.weighted_rate += w * rate;
            a.weighted_missed += w * miss_part;
            a.weighted_wrong += w * wrong_part;
            a.attempts += b.graded;
        }
    }
    // In measure order, the bars that were graded.
    std::vector<std::pair<int, const BarAcc*>> acc;
    if (flat) {
        for (std::size_t i = 0; i < table.size(); ++i) {
            if (table[i].attempts > 0) {
                acc.emplace_back(static_cast<int>(lowest + static_cast<std::int64_t>(i)), &table[i]);
            }
        }
    } else {
        for (const auto& [measure, a] : sparse) {
            acc.emplace_back(measure, &a);
        }
    }

    plan.bars.reserve(acc.size());
    std::vector<const BarAcc*> detail;
    detail.reserve(acc.size());
    for (const auto& [measure, a] : acc) {
        BarHeat h;
        h.measure = measure;
        h.attempts = static_cast<int>(std::min<std::int64_t>(a->attempts, INT32_MAX));
        h.reliable = a->attempts >= min_attempts;
        h.heat = h.reliable ? std::clamp(a->weighted_rate / a->weight, 0.0, 1.0) : 0.0;
        plan.bars.push_back(h);
        detail.push_back(a);
    }

    const std::size_t n = plan.bars.size();
    std::vector<char> trouble(n, 0);
    std::vector<std::size_t> seeds;
    for (std::size_t i = 0; i < n; ++i) {
        if (plan.bars[i].reliable && plan.bars[i].heat >= min_heat) {
            trouble[i] = 1;
            seeds.push_back(i);
        }
    }
    std::sort(seeds.begin(), seeds.end(), [&](std::size_t a, std::size_t b) {
        const BarHeat& x = plan.bars[a];
        const BarHeat& y = plan.bars[b];
        if (x.heat != y.heat) {
            return x.heat > y.heat;
        }
        if (x.attempts != y.attempts) {
            return x.attempts > y.attempts;
        }
        return x.measure < y.measure;
    });

    std::vector<char> used(n, 0);
    auto free_neighbour = [&](std::size_t from, std::size_t to) {
        return trouble[to] && !used[to] &&
               plan.bars[to].measure == plan.bars[from].measure + (to > from ? 1 : -1);
    };
    for (std::size_t seed : seeds) {
        if (plan.spots.size() >= max_spots) {
            break;
        }
        if (used[seed]) {
            continue;
        }
        std::size_t lo = seed;
        std::size_t hi = seed;
        used[seed] = 1;
        while (static_cast<int>(hi - lo + 1) < max_bars) {
            const bool left = lo > 0 && free_neighbour(lo, lo - 1);
            const bool right = hi + 1 < n && free_neighbour(hi, hi + 1);
            if (!left && !right) {
                break;
            }
            // The hotter side; an equal pair goes to the later bar.
            const bool go_left =
                left && (!right || plan.bars[lo - 1].heat > plan.bars[hi + 1].heat);
            std::size_t added = go_left ? lo - 1 : hi + 1;
            used[added] = 1;
            (go_left ? lo : hi) = added;
        }

        TroubleSpot spot;
        spot.first_measure = plan.bars[lo].measure;
        spot.last_measure = plan.bars[hi].measure;
        double missed = 0.0;
        double wrong = 0.0;
        std::int64_t attempts = 0;
        for (std::size_t i = lo; i <= hi; ++i) {
            spot.heat = std::max(spot.heat, plan.bars[i].heat);
            attempts += detail[i]->attempts;
            missed += detail[i]->weighted_missed;
            wrong += detail[i]->weighted_wrong;
        }
        spot.attempts = static_cast<int>(std::min<std::int64_t>(attempts, INT32_MAX));
        spot.reason = missed >= wrong ? TroubleReason::MissedNotes : TroubleReason::WrongNotes;
        for (const RunRecord* r : picked) {
            auto it =
                std::lower_bound(r->measures.begin(), r->measures.end(), spot.first_measure,
                                 [](const RunBar& b, int measure) { return b.measure < measure; });
            bool seen = false;
            bool errors = false;
            for (; it != r->measures.end() && it->measure <= spot.last_measure; ++it) {
                if (it->graded > 0) {
                    seen = true;
                    errors = errors || it->missed > 0 || it->wrong > 0;
                }
            }
            spot.runs_seen += seen ? 1 : 0;
            spot.runs_with_errors += errors ? 1 : 0;
        }
        plan.spots.push_back(spot);
    }
    return plan;
}

} // namespace etude
