// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/run_summary.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <map>
#include <utility>

namespace etude {

namespace {

// Half away from zero, so a half millisecond never rounds toward a lean it does not have.
int mid_of(int a, int b) {
    const int sum = a + b;
    return sum >= 0 ? (sum + 1) / 2 : -((-sum + 1) / 2);
}

int median_of(std::span<const int> sorted) {
    if (sorted.empty()) {
        return 0;
    }
    const std::size_t n = sorted.size();
    return n % 2 == 1 ? sorted[n / 2] : mid_of(sorted[n / 2 - 1], sorted[n / 2]);
}

// Never 100 while anything went wrong, as a bar's figure is not.
int percent_of(int hit, int graded) {
    if (graded <= 0) {
        return -1;
    }
    const int pct = static_cast<int>(std::lround(100.0 * hit / graded));
    return pct >= 100 && hit < graded ? 99 : pct;
}

TimingSpread timing_of(std::vector<int> errors) {
    TimingSpread t;
    if (errors.empty()) {
        return t;
    }
    std::sort(errors.begin(), errors.end());
    t.samples = static_cast<int>(errors.size());
    const auto first_late = std::upper_bound(errors.begin(), errors.end(), 0);
    const auto first_not_early = std::lower_bound(errors.begin(), errors.end(), 0);
    t.early = static_cast<int>(first_not_early - errors.begin());
    t.late = static_cast<int>(errors.end() - first_late);
    t.median_ms = median_of(errors);
    const std::span<const int> all(errors);
    t.median_early_ms = median_of(all.first(static_cast<std::size_t>(t.early)));
    t.median_late_ms = median_of(all.last(static_cast<std::size_t>(t.late)));
    if (errors.size() >= 2) {
        const std::size_t half = errors.size() / 2;
        t.spread_ms = median_of(all.last(half)) - median_of(all.first(half));
    }
    t.tendency = t.median_ms <= -kTendencyMs  ? Tendency::Early
                 : t.median_ms >= kTendencyMs ? Tendency::Late
                                              : Tendency::Steady;
    return t;
}

bool hand_practised(HandFilter filter, Hand hand) {
    switch (filter) {
    case HandFilter::Left:
        return hand == Hand::Left;
    case HandFilter::Right:
        return hand == Hand::Right;
    case HandFilter::Both:
        break;
    }
    return true;
}

void finish_hands(RunSummary& s) {
    for (std::size_t i = 0; i < s.hands.size(); ++i) {
        SummaryHand& h = s.hands[i];
        h.practised = h.graded > 0 && hand_practised(s.setup.hands, static_cast<Hand>(i));
        h.accuracy_pct = h.practised ? percent_of(h.hit, h.graded) : -1;
    }
    const SummaryHand& left = s.hand(Hand::Left);
    const SummaryHand& right = s.hand(Hand::Right);
    if (left.graded >= kWeakerHandMinNotes && right.graded >= kWeakerHandMinNotes &&
        left.practised && right.practised &&
        std::abs(left.accuracy_pct - right.accuracy_pct) >= kWeakerHandGapPct) {
        s.weaker_hand = left.accuracy_pct < right.accuracy_pct ? Hand::Left : Hand::Right;
    }
}

void name_bars(RunSummary& s, std::span<const RunBar> bars, std::span<const Measure> measures) {
    s.bars = worst_bars(bars, &s.bar_count);
    for (int i = 0; i < s.bar_count; ++i) {
        name_written_bar(s.bars[static_cast<std::size_t>(i)], measures);
    }
}

// The note nearest in time to `ms` among the judged ones (sorted by time), within the reach.
const ReplayNote* nearest_judged(std::span<const ReplayNote* const> judged, std::int32_t ms) {
    const auto after = std::lower_bound(
        judged.begin(), judged.end(), ms,
        [](const ReplayNote* n, std::int32_t t) { return n->time_ms < t; });
    const ReplayNote* best = nullptr;
    std::int64_t best_gap = kWrongSpotReachMs + 1;
    for (auto it = after == judged.begin() ? after : std::prev(after);
         it != judged.end() && it <= after; ++it) {
        const std::int64_t gap = std::abs(static_cast<std::int64_t>((*it)->time_ms) - ms);
        if (gap < best_gap) {
            best_gap = gap;
            best = *it;
        }
    }
    return best;
}

std::vector<WrongSpot> wrong_spots_of(const ReplayTimeline& timeline,
                                      std::span<const Measure> measures) {
    std::vector<const ReplayNote*> judged;
    for (const ReplayNote& n : timeline.notes) {
        if (n.mark != ReplayMark::Wrong) {
            judged.push_back(&n);
        }
    }
    std::map<int, WrongSpot> by_bar; // the first wrong press of a bar names the spot
    for (const ReplayNote& n : timeline.notes) {
        if (n.mark != ReplayMark::Wrong) {
            continue;
        }
        auto [it, fresh] = by_bar.try_emplace(n.measure);
        WrongSpot& spot = it->second;
        ++spot.count;
        if (!fresh) {
            continue;
        }
        spot.measure = n.measure;
        spot.first_ms = n.time_ms;
        spot.key = n.key;
        if (const ReplayNote* near = nearest_judged(judged, n.time_ms)) {
            spot.expected_key = near->key;
        }
        CardBar bar;
        bar.measure = n.measure;
        name_written_bar(bar, measures);
        spot.written = bar.written;
    }
    std::vector<WrongSpot> spots;
    spots.reserve(by_bar.size());
    for (const auto& [measure, spot] : by_bar) {
        spots.push_back(spot);
    }
    std::stable_sort(spots.begin(), spots.end(),
                     [](const WrongSpot& a, const WrongSpot& b) { return a.count > b.count; });
    if (spots.size() > static_cast<std::size_t>(kMaxWrongSpots)) {
        spots.resize(static_cast<std::size_t>(kMaxWrongSpots));
    }
    return spots;
}

RunSummary setup_of(const RunRecord& record) {
    RunSummary s;
    s.setup = {record.mode, record.hands, record.speed_percent, record.transpose_semitones};
    return s;
}

// Everything from the played notes.
RunSummary summary_from_notes(const RunRecord& record, const ReplayTimeline& timeline,
                              const ReplaySong& song) {
    RunSummary s = setup_of(record);
    s.detail = SummaryDetail::Played;
    s.complete = timeline.complete;
    s.looped = timeline.looped;
    s.stopped_early = timeline.stopped_early;
    s.dropped_events = timeline.dropped;
    if (timeline.expected_notes > 0) {
        s.coverage_pct = std::min(
            100, static_cast<int>(std::lround(100.0 * timeline.graded_notes / timeline.expected_notes)));
    }
    std::map<int, RunBar> bars;
    std::vector<int> errors;
    std::array<std::vector<int>, 3> hand_errors;
    for (const ReplayNote& n : timeline.notes) {
        RunBar& bar = bars.try_emplace(n.measure, RunBar{n.measure, 0, 0, 0}).first->second;
        if (n.mark == ReplayMark::Wrong) {
            ++s.wrong;
            ++bar.wrong;
            continue;
        }
        SummaryHand& hand = s.hands[static_cast<std::size_t>(n.hand)];
        ++s.graded;
        ++bar.graded;
        ++hand.graded;
        if (n.mark == ReplayMark::Missed) {
            ++s.missed;
            ++bar.missed;
            ++hand.missed;
            ++s.counts.missed;
            continue;
        }
        ++s.hit;
        ++hand.hit;
        switch (n.mark) {
        case ReplayMark::Perfect:
            ++s.counts.perfect;
            break;
        case ReplayMark::Good:
            ++s.counts.good;
            break;
        case ReplayMark::Early:
            ++s.counts.early;
            ++hand.early;
            break;
        case ReplayMark::Late:
            ++s.counts.late;
            ++hand.late;
            break;
        case ReplayMark::Played:
            ++s.counts.played;
            break;
        case ReplayMark::Missed:
        case ReplayMark::Wrong:
            break;
        }
        if (record.mode == RunMode::Flow) { // Wait Mode has no timing, only a note played
            errors.push_back(n.error_ms);
            hand_errors[static_cast<std::size_t>(n.hand)].push_back(n.error_ms);
        }
    }
    s.accuracy_pct = percent_of(s.hit, s.graded);
    s.timing = timing_of(std::move(errors));
    for (std::size_t i = 0; i < s.hands.size(); ++i) {
        std::sort(hand_errors[i].begin(), hand_errors[i].end());
        s.hands[i].median_ms = median_of(hand_errors[i]);
        s.hands[i].timing_samples = static_cast<int>(hand_errors[i].size());
    }
    finish_hands(s);
    std::vector<RunBar> flat;
    flat.reserve(bars.size());
    for (const auto& [measure, bar] : bars) {
        flat.push_back(bar);
    }
    name_bars(s, flat, song.measures);
    s.played_bars = std::move(flat);
    s.wrong_spots = wrong_spots_of(timeline, song.measures);
    return s;
}

// What the record alone knows.
RunSummary summary_from_record(const RunRecord& record, const ReplaySong& song) {
    RunSummary s = setup_of(record);
    s.graded = record.all.graded;
    s.hit = record.all.hit;
    if (s.graded <= 0) {
        return s; // Nothing
    }
    s.detail = SummaryDetail::RecordOnly;
    s.missed = record.counts.missed;
    s.wrong = record.wrong;
    s.counts = record.counts;
    s.accuracy_pct = percent_of(s.hit, s.graded);
    const RunHand* hands[3] = {&record.unknown, &record.left, &record.right};
    for (std::size_t i = 0; i < s.hands.size(); ++i) {
        s.hands[i].graded = hands[i]->graded;
        s.hands[i].hit = hands[i]->hit;
        s.hands[i].missed = hands[i]->graded - hands[i]->hit;
    }
    finish_hands(s);
    name_bars(s, record.measures, song.measures);
    s.played_bars = record.measures;
    return s;
}

} // namespace

RunSummary summarize_run(const RunRecord& record, const RunEvents& events, const ReplaySong& song) {
    const GhostSetup setup{record.mode, record.hands, record.speed_percent,
                           record.transpose_semitones};
    const ReplayTimeline timeline = build_replay_timeline(events, setup, song);
    return timeline.empty() ? summary_from_record(record, song)
                            : summary_from_notes(record, timeline, song);
}

BarPractice plan_summary_practice(const RunSummary& summary, std::span<const Measure> measures,
                                  double duration_s, BarPracticeKind kind, int max_bars) {
    std::array<int, kMaxCardBars> chosen{};
    const int n = std::clamp(std::min(summary.bar_count, max_bars), 0, kMaxCardBars);
    for (int i = 0; i < n; ++i) {
        chosen[static_cast<std::size_t>(i)] = summary.bars[static_cast<std::size_t>(i)].measure;
    }
    return plan_bar_practice(std::span<const int>(chosen.data(), static_cast<std::size_t>(n)),
                             measures, duration_s, kind);
}

RunReplay build_run_replay(const RunRecord& record, const RunEvents& events,
                           const ReplaySong& song) {
    RunReplay out;
    out.timeline = build_replay_timeline(
        events, {record.mode, record.hands, record.speed_percent, record.transpose_semitones}, song);
    out.summary = out.timeline.empty() ? summary_from_record(record, song)
                                       : summary_from_notes(record, out.timeline, song);
    return out;
}

} // namespace etude
