// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of evaluator.cpp share. Not part of the app's interface.
#pragma once

#include "etude/evaluator.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace etude {

namespace eval_detail {

using Kind = TransportOp::Kind;

inline constexpr std::int64_t kForever = std::numeric_limits<std::int64_t>::max();

inline constexpr std::int64_t kNever = std::numeric_limits<std::int64_t>::min();

inline constexpr double kEpsilonS = 1e-9;

inline const double kNoStrike = std::numeric_limits<double>::quiet_NaN();

inline std::optional<double> strike_or_none(double start_s) {
    return std::isnan(start_s) ? std::nullopt : std::optional<double>(start_s);
}

// Op history older than this behind the decided edge is dropped.
inline constexpr std::int64_t kHistoryNs = 2'000'000'000;

inline std::int64_t to_ns(double ms) { return static_cast<std::int64_t>(std::llround(ms * 1e6)); }

// `x` clamped to [lo, hi]; NaN, which std::clamp passes through, gives `fallback`.
inline double sane(double x, double lo, double hi, double fallback) {
    return std::isnan(x) ? fallback : std::clamp(x, lo, hi);
}

inline bool practised(const Note& n, HandFilter hands) {
    if (n.drum || n.accompaniment) {
        return false;
    }
    switch (hands) {
    case HandFilter::Both:
        return true;
    case HandFilter::Left:
        return n.hand == Hand::Left;
    case HandFilter::Right:
        return n.hand == Hand::Right;
    }
    return false;
}

// A key and the practised notes on it that one press plays.
struct KeyGroup {
    double start_s = 0.0;
    int key = 0;
    int measure = 1;
    std::vector<std::uint32_t> notes;
};

// Groups the practised, playable notes into one entry per key press (notes on one key within
// kUnisonSeconds), sorted by start then key. Starts of unplayable practised notes go to
// `unplayable`.
// `abort` is asked every kAbortEvery notes and between the phases: true ends the work with an empty
// answer (a worker told to quit does not finish a long build).
inline constexpr std::size_t kAbortEvery = 4096;

inline bool stopped(const std::function<bool()>& abort) { return abort && abort(); }

inline std::vector<KeyGroup> key_groups(const std::vector<Note>& notes, HandFilter hands,
                                 const Transposition& tr, std::vector<double>& unplayable,
                                 const std::function<bool()>& abort = {}) {
    struct Item {
        double start_s;
        int key;
        std::uint32_t index;
    };
    std::vector<Item> items;
    unplayable.clear();
    for (std::uint32_t i = 0; i < notes.size(); ++i) {
        if (i % kAbortEvery == 0 && stopped(abort)) {
            return {};
        }
        const Note& n = notes[i];
        if (!practised(n, hands)) {
            continue;
        }
        const TransposedPitch p = tr.expected_pitch(n);
        if (!p.playable || p.pitch < 0 || p.pitch > 127) {
            unplayable.push_back(n.start_s);
            continue;
        }
        items.push_back({n.start_s, p.pitch, i});
    }
    std::sort(unplayable.begin(), unplayable.end());
    if (stopped(abort)) {
        return {};
    }
    std::stable_sort(items.begin(), items.end(), [](const Item& a, const Item& b) {
        return a.key < b.key || (a.key == b.key && a.start_s < b.start_s);
    });
    std::vector<KeyGroup> groups;
    std::size_t counted = 0;
    for (const Item& it : items) {
        if (++counted % kAbortEvery == 0 && stopped(abort)) {
            return {};
        }
        if (!groups.empty() && groups.back().key == it.key &&
            it.start_s - groups.back().start_s <= kUnisonSeconds + kEpsilonS) {
            groups.back().notes.push_back(it.index);
            continue;
        }
        KeyGroup g;
        g.start_s = it.start_s;
        g.key = it.key;
        g.measure = notes[it.index].measure;
        g.notes.push_back(it.index);
        groups.push_back(std::move(g));
    }
    if (stopped(abort)) {
        return {};
    }
    std::stable_sort(groups.begin(), groups.end(), [](const KeyGroup& a, const KeyGroup& b) {
        return a.start_s < b.start_s || (a.start_s == b.start_s && a.key < b.key);
    });
    return groups;
}

// How many unplayable starts lie in [from, to).
inline int count_in(const std::vector<double>& starts, double from, double to) {
    const auto a = std::lower_bound(starts.begin(), starts.end(), from);
    const auto b = std::lower_bound(starts.begin(), starts.end(), to);
    return static_cast<int>(b - a);
}

inline HandStats& hand_stats(EvaluationSummary& s, Hand h) {
    switch (h) {
    case Hand::Left:
        return s.left;
    case Hand::Right:
        return s.right;
    case Hand::Unknown:
        break;
    }
    return s.unknown;
}

inline MeasureStats& stats_for(std::vector<MeasureStats>& v, int measure) {
    auto it = std::lower_bound(v.begin(), v.end(), measure,
                               [](const MeasureStats& m, int n) { return m.measure < n; });
    if (it == v.end() || it->measure != measure) {
        MeasureStats m;
        m.measure = measure;
        it = v.insert(it, m);
    }
    return *it;
}

// Resets the combo to 0, noting the one that was lost.
inline void lose_combo(EvaluationSummary& s) {
    if (s.combo > 0) {
        s.combo_losses.push_back(s.combo);
    }
    s.combo = 0;
}

// Records one final note result in the summary.
inline void record(EvaluationSummary& s, const NoteResult& r) {
    const bool hit = r.grade != NoteGrade::Missed;
    switch (r.grade) {
    case NoteGrade::Perfect:
        ++s.counts.perfect;
        break;
    case NoteGrade::Good:
        ++s.counts.good;
        break;
    case NoteGrade::Early:
        ++s.counts.early;
        break;
    case NoteGrade::Late:
        ++s.counts.late;
        break;
    case NoteGrade::Missed:
        ++s.counts.missed;
        break;
    case NoteGrade::Played:
        ++s.counts.played;
        break;
    }
    for (HandStats* h : {&hand_stats(s, r.hand), &s.all}) {
        ++h->graded;
        h->hit += hit ? 1 : 0;
    }
    MeasureStats& m = stats_for(s.measures, r.measure);
    ++m.graded;
    m.missed += hit ? 0 : 1;
    if (hit) {
        ++s.combo;
        s.longest_combo = std::max(s.longest_combo, s.combo);
    } else {
        lose_combo(s);
    }
    s.notes.push_back(r);
}

// Counts one graded note in a pass: its grade, and the pass's run of hits.
inline void tally(detail::PassState& p, NoteGrade grade) {
    PassResult& r = p.result;
    switch (grade) {
    case NoteGrade::Perfect:
        ++r.perfect;
        break;
    case NoteGrade::Good:
        ++r.good;
        break;
    case NoteGrade::Early:
        ++r.early;
        break;
    case NoteGrade::Late:
        ++r.late;
        break;
    case NoteGrade::Played:
        ++r.played;
        break;
    case NoteGrade::Missed:
        break;
    }
    if (grade == NoteGrade::Missed) {
        p.combo = 0;
    } else {
        ++p.combo;
        r.longest_combo = std::max(r.longest_combo, p.combo);
    }
}

inline void record_wrong(EvaluationSummary& s, int measure) {
    ++s.wrong;
    lose_combo(s);
    ++stats_for(s.measures, measure).wrong;
}

inline bool in_dead(const std::vector<std::array<std::int64_t, 2>>& dead, std::int64_t ns) {
    for (const auto& d : dead) {
        if (ns >= d[0] && ns < d[1]) {
            return true;
        }
    }
    return false;
}

inline void open_dead(std::vector<std::array<std::int64_t, 2>>& dead, std::int64_t from) {
    if (dead.empty() || dead.back()[1] != kForever) {
        dead.push_back({from, kForever});
    }
}

inline void close_dead(std::vector<std::array<std::int64_t, 2>>& dead, std::int64_t at) {
    if (!dead.empty() && dead.back()[1] == kForever) {
        dead.back()[1] = std::max(at, dead.back()[0]);
    }
}

inline void prune_dead(std::vector<std::array<std::int64_t, 2>>& dead, std::int64_t before) {
    dead.erase(std::remove_if(dead.begin(), dead.end(),
                              [&](const auto& d) { return d[1] != kForever && d[1] < before; }),
               dead.end());
}

// Keeps only the pass in progress, numbered 0. It lost results to the reset, so it is never clean.
inline void restart_passes(detail::PassLog& passes) {
    detail::PassState current = passes.size() == 0 ? detail::PassState{} : passes.back();
    current.interrupted = current.interrupted || current.moved;
    current.result = {};
    passes.reset(current);
}

// The pass in progress was cut short (a seek, a song or key change): it ends at `ns` and is never
// reported.
inline void drop_pass(detail::PassState& pass, double end_s, std::int64_t ns) {
    pass.interrupted = true;
    pass.ended = true;
    pass.dropped = true;
    pass.end_s = end_s;
    pass.end_ns = ns;
}

} // namespace eval_detail

} // namespace etude
