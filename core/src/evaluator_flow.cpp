// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/evaluator.hpp"

#include "evaluator_internal.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <utility>

namespace etude {

using namespace eval_detail;

namespace {

// A stable sort that asks the heap for nothing on the short lists a frame sees (std::stable_sort
// takes a buffer: libstdc++ always does). Longer lists use std::stable_sort.
template <class It, class Less>
void stable_sort_short(It first, It last, Less less) {
    constexpr std::ptrdiff_t kShort = 64;
    if (last - first > kShort) {
        std::stable_sort(first, last, less);
        return;
    }
    for (It it = first; it < last; ++it) {
        auto value = std::move(*it);
        It at = it;
        for (; at > first && less(value, *(at - 1)); --at) {
            *at = std::move(*(at - 1));
        }
        *at = std::move(value);
    }
}

} // namespace

void FlowEvaluator::apply(const TransportOp& op) {
    if (op.kind == Kind::Click) {
        return;
    }
    sync_transposition();
    timeline_.apply(op);
    detail::PassState& pass = passes_.back();
    switch (op.kind) {
    case Kind::Play:
        close_dead(dead_, op.at_ns);
        moving_ = true;
        clock_ns_ = std::max(clock_ns_, op.at_ns);
        if (!pass.moved) {
            pass.start_s = op.to_s;
            pass.start_ns = op.at_ns;
        }
        break;
    case Kind::Release:
        moving_ = true;
        clock_ns_ = std::max(clock_ns_, op.at_ns);
        break;
    case Kind::Pause:
    case Kind::End:
        open_dead(dead_, op.at_ns + window_ns_);
        [[fallthrough]];
    case Kind::Hold:
        moving_ = false;
        clock_ns_ = std::max(clock_ns_, op.at_ns);
        break;
    case Kind::Move: {
        clock_ns_ = std::max(clock_ns_, op.end_ns);
        pass.moved = true;
        summary_.unplayable += count_in(unplayable_s_, op.from_s, op.to_s);
        if (op.to_s <= op.from_s) {
            break;
        }
        auto first = std::lower_bound(targets_.begin(), targets_.end(), op.from_s,
                                      [](const Target& t, double s) { return t.start_s < s; });
        const double span = op.to_s - op.from_s;
        const double wall = static_cast<double>(op.end_ns - op.at_ns);
        for (auto it = first; it != targets_.end() && it->start_s < op.to_s; ++it) {
            Pending p;
            p.target = static_cast<std::uint32_t>(it - targets_.begin());
            p.arrival_ns = op.at_ns + static_cast<std::int64_t>(
                                          std::llround((it->start_s - op.from_s) / span * wall));
            if (p.arrival_ns < ignore_before_ns_) {
                continue; // reached before a key or song change: neither hit nor missed
            }
            p.pass = pass.result.pass;
            pending_.push_back(p);
        }
        break;
    }
    case Kind::Wrap: {
        forget_presses(); // the song jumps from under the held keys
        pass.ended = true;
        pass.end_s = op.from_s;
        pass.end_ns = op.at_ns;
        pass.loop_start_s = op.to_s;
        detail::PassState next;
        next.start_s = op.to_s;
        next.start_ns = op.at_ns;
        next.moved = true;
        passes_.push(next);
        break;
    }
    case Kind::Seek: {
        // Decide what is already decided, then drop the rest.
        drop_undecided(op.at_ns);
        if (pass.moved) {
            drop_pass(pass, op.from_s, op.at_ns);
            detail::PassState next;
            next.start_s = op.to_s;
            next.start_ns = op.at_ns;
            passes_.push(next);
        } else {
            pass.interrupted = false;
            pass.start_s = op.to_s;
            pass.start_ns = op.at_ns;
        }
        break;
    }
    case Kind::Click:
        break;
    }
}

bool FlowEvaluator::ignored(std::int64_t ns) const {
    return ns < ignore_before_ns_ || in_dead(dead_, ns);
}

void FlowEvaluator::apply(const MidiEvent& ev) {
    sync_transposition();
    if (ev.synthetic || ev.type != MidiEventType::NoteOn || ev.data2 == 0 || ev.data1 > 127) {
        return;
    }
    const std::int64_t t = ev.time_ns - latency_ns_;
    const auto key = static_cast<std::size_t>(ev.data1);
    if (ignored(ev.time_ns) || t < ignore_before_ns_) {
        struck_start_[key] = kNoStrike;
        wrong_looking_[key] = false;
        return;
    }
    presses_.push_back({ev.data1, t, false, ev.data2, options_.lookahead_s});
    struck_start_[key] = nearest_start(ev.data1, t, struck_start_[key]);
    wrong_looking_[key] = std::isnan(struck_start_[key]) && !excused(presses_.back());
}

void FlowEvaluator::forget_presses() {
    struck_start_.fill(kNoStrike);
    wrong_looking_.fill(false);
}

bool FlowEvaluator::press_looks_wrong(int key) const {
    return key >= 0 && key < 128 && wrong_looking_[static_cast<std::size_t>(key)];
}

double FlowEvaluator::nearest_start(int key, std::int64_t ns, double except_s) const {
    const double at = timeline_.song_at(ns);
    const double window_s = static_cast<double>(window_ns_) * 1e-9 * timeline_.rate_at(ns);
    auto it = std::lower_bound(targets_.begin(), targets_.end(), at - window_s - kEpsilonS,
                               [](const Target& t, double s) { return t.start_s < s; });
    double best = kNoStrike;
    double best_distance = std::numeric_limits<double>::infinity();
    for (; it != targets_.end() && it->start_s <= at + window_s + kEpsilonS; ++it) {
        const double distance = std::fabs(it->start_s - at);
        const bool struck_before = std::fabs(it->start_s - except_s) < kEpsilonS;
        if (it->key == key && !struck_before && distance < best_distance) {
            best = it->start_s;
            best_distance = distance;
        }
    }
    return best;
}

std::optional<double> FlowEvaluator::struck_start(int key) const {
    return key >= 0 && key < 128 ? strike_or_none(struck_start_[static_cast<std::size_t>(key)])
                                 : std::nullopt;
}

void FlowEvaluator::take_wrong_presses(std::vector<WrongPress>& out) {
    out.insert(out.end(), wrong_presses_.begin(), wrong_presses_.end());
    wrong_presses_.clear();
}

int FlowEvaluator::take_dropped_wrong_presses() { return std::exchange(dropped_wrong_presses_, 0); }

void FlowEvaluator::apply_unsure(std::int64_t ns) {
    sync_transposition();
    if (!ignored(ns)) {
        unsure_.push_back(ns - latency_ns_);
    }
}

int FlowEvaluator::measure_at(double song_s) const {
    auto it = std::upper_bound(measures_->begin(), measures_->end(), song_s,
                               [](double s, const Measure& m) { return s < m.start_s; });
    if (it == measures_->begin()) {
        return measures_->empty() ? 1 : measures_->front().number;
    }
    return (it - 1)->number;
}

bool FlowEvaluator::excused(const Press& p) const {
    if (p.time_ns >= grace_[0] && p.time_ns < grace_[1]) {
        return true;
    }
    // The key of a note still to come within the lookahead: pressed early, not wrong.
    const double at = timeline_.song_at(p.time_ns);
    auto it = std::upper_bound(targets_.begin(), targets_.end(), at,
                               [](double s, const Target& t) { return s < t.start_s; });
    for (; it != targets_.end() && it->start_s <= at + p.lookahead_s + kEpsilonS; ++it) {
        if (it->key == p.key) {
            return true;
        }
    }
    return false;
}

// Decides every group of notes and presses that nothing after `frontier` can reach.
//
// Per key, notes and presses sorted by time split into groups wherever two neighbours are more
// than a window apart; no match can cross such a gap. In a closed group the best assignment is
// order-preserving (if note a1 < a2 were matched to presses b2 > b1, swapping keeps both pairs in
// the window and never adds error), so a DP over (notes, presses) finds the most matches with the
// least total |error| (decide_group).
void FlowEvaluator::resolve(std::int64_t frontier) {
    if (pending_.empty() && presses_.empty()) {
        return;
    }
    stable_sort_short(pending_.begin(), pending_.end(), [&](const Pending& a, const Pending& b) {
        const int ka = targets_[a.target].key;
        const int kb = targets_[b.target].key;
        return ka < kb || (ka == kb && a.arrival_ns < b.arrival_ns);
    });
    stable_sort_short(presses_.begin(), presses_.end(), [](const Press& a, const Press& b) {
        return a.key < b.key || (a.key == b.key && a.time_ns < b.time_ns);
    });

    std::size_t ni = 0;
    std::size_t pi = 0;
    while (ni < pending_.size() || pi < presses_.size()) {
        const int key = std::min(ni < pending_.size() ? targets_[pending_[ni].target].key : 1000,
                                 pi < presses_.size() ? presses_[pi].key : 1000);
        std::size_t ne = ni;
        while (ne < pending_.size() && targets_[pending_[ne].target].key == key) {
            ++ne;
        }
        std::size_t pe = pi;
        while (pe < presses_.size() && presses_[pe].key == key) {
            ++pe;
        }
        // Walk this key's items in time order, cutting groups at gaps wider than the window.
        std::size_t a = ni;
        std::size_t b = pi;
        while (a < ne || b < pe) {
            const std::size_t a0 = a;
            const std::size_t b0 = b;
            std::int64_t last = kNever;
            for (;;) {
                const std::int64_t ta = a < ne ? pending_[a].arrival_ns : kForever;
                const std::int64_t tb = b < pe ? presses_[b].time_ns : kForever;
                const std::int64_t t = std::min(ta, tb);
                if (t == kForever || (last != kNever && t - last > window_ns_)) {
                    break;
                }
                last = t;
                if (ta <= tb) {
                    ++a;
                } else {
                    ++b;
                }
            }
            if (last + window_ns_ >= frontier) {
                continue; // still open
            }
            decide_group(a0, a - a0, b0, b - b0);
        }
        ni = ne;
        pi = pe;
    }
    pending_.erase(
        std::remove_if(pending_.begin(), pending_.end(), [](const Pending& p) { return p.done; }),
        pending_.end());
    presses_.erase(
        std::remove_if(presses_.begin(), presses_.end(), [](const Press& p) { return p.done; }),
        presses_.end());
}

// The DP over (i notes, j presses), banded: note i can only match the presses in
// [lower[i], upper[i]), the ones within a window of it, and both bounds grow with i. Row r (the
// first r notes) is stored for j in [lo_r, hi_r] = [lower[r - 1], upper[r - 1]] only:
// - j > hi_r: the presses past hi_r are too late for every note before r, so the cell equals
//   (r, hi_r);
// - j < lo_r is never needed: the fill and the walk back stay at or right of lo_r, where
//   (r, lo_r) equals (r - 1, lo_r) because note r - 1 can't use a press before lo_r.
// Cells hold the same values and the walk makes the same choices as the full table, in
// O(notes * presses per window) time and memory instead of O(notes * presses).
void FlowEvaluator::decide_group(std::size_t a0, std::size_t n, std::size_t b0, std::size_t m) {
    const auto better = [](const MatchCell& x, const MatchCell& y) {
        return x.count > y.count || (x.count == y.count && x.cost < y.cost);
    };
    const auto same = [](const MatchCell& x, const MatchCell& y) {
        return x.count == y.count && x.cost == y.cost;
    };
    // Row bounds, and where each row starts in `cells`: buffers kept between groups, so a group
    // that fits what an earlier one grew them to allocates nothing.
    std::vector<std::size_t>& lo = match_lo_;
    std::vector<std::size_t>& hi = match_hi_;
    std::vector<std::size_t>& row = match_row_;
    lo.assign(n + 1, 0);
    hi.assign(n + 1, 0);
    row.assign(n + 2, 0);
    std::size_t lower = 0;
    std::size_t upper = 0;
    for (std::size_t r = 1; r <= n; ++r) {
        const std::int64_t arrival = pending_[a0 + r - 1].arrival_ns;
        while (lower < m && presses_[b0 + lower].time_ns < arrival - window_ns_) {
            ++lower;
        }
        upper = std::max(upper, lower);
        while (upper < m && presses_[b0 + upper].time_ns <= arrival + window_ns_) {
            ++upper;
        }
        lo[r] = lower;
        hi[r] = upper;
    }
    for (std::size_t r = 0; r <= n; ++r) {
        row[r + 1] = row[r] + (hi[r] - lo[r] + 1);
    }
    std::vector<MatchCell>& cells = match_cells_;
    cells.resize(row[n + 1]);
    cells[0] = MatchCell{}; // row 0: no notes, nothing matched
    const auto get = [&](std::size_t r, std::size_t j) -> MatchCell& {
        return cells[row[r] + std::min(j, hi[r]) - lo[r]];
    };
    const auto feasible = [&](std::size_t r, std::size_t j) { // note r - 1, press j - 1
        return j > lo[r] && j <= hi[r];
    };
    const auto matched = [&](std::size_t r, std::size_t j) {
        MatchCell c = get(r - 1, j - 1);
        const std::int64_t e = presses_[b0 + j - 1].time_ns - pending_[a0 + r - 1].arrival_ns;
        ++c.count;
        c.cost += e < 0 ? -e : e;
        return c;
    };
    for (std::size_t r = 1; r <= n; ++r) {
        get(r, lo[r]) = get(r - 1, lo[r]);
        for (std::size_t j = lo[r] + 1; j <= hi[r]; ++j) {
            MatchCell best{-1, 0};
            if (feasible(r, j)) {
                best = matched(r, j);
            }
            if (better(get(r - 1, j), best)) {
                best = get(r - 1, j);
            }
            if (better(get(r, j - 1), best)) {
                best = get(r, j - 1);
            }
            get(r, j) = best;
        }
    }
    // Walk back the same choices, in the same preference order.
    std::vector<long>& match = match_pick_;
    match.assign(n, -1);
    std::size_t i = n;
    std::size_t j = m;
    while (i > 0 && j > 0) {
        const MatchCell here = get(i, j);
        if (feasible(i, j) && same(matched(i, j), here)) {
            match[i - 1] = static_cast<long>(j - 1);
            --i;
            --j;
        } else if (same(get(i - 1, j), here)) {
            --i;
        } else {
            --j;
        }
    }

    std::vector<char>& used = match_used_;
    used.assign(m, 0);
    for (std::size_t k = 0; k < n; ++k) {
        Pending& p = pending_[a0 + k];
        p.done = true;
        Outcome o;
        o.time_ns = p.arrival_ns;
        o.target = p.target;
        o.pass = p.pass;
        o.arrival_ns = p.arrival_ns;
        if (match[k] >= 0) {
            const std::size_t pj = static_cast<std::size_t>(match[k]);
            used[pj] = 1;
            o.hit = true;
            o.press_ns = presses_[b0 + pj].time_ns;
            o.velocity = presses_[b0 + pj].velocity;
        } else {
            for (const std::int64_t u : unsure_) {
                const std::int64_t d = u - p.arrival_ns;
                if (d >= -window_ns_ && d <= window_ns_) {
                    o.unjudged = true;
                    break;
                }
            }
        }
        outcomes_.push_back(o);
    }
    for (std::size_t k = 0; k < m; ++k) {
        Press& p = presses_[b0 + k];
        p.done = true;
        if (!used[k] && !excused(p)) {
            Outcome o;
            o.time_ns = p.time_ns;
            o.wrong = true;
            o.key = p.key;
            o.velocity = p.velocity;
            o.press_ns = p.time_ns;
            o.pass = passes_.number_at(p.time_ns);
            outcomes_.push_back(o);
        }
    }
}

void FlowEvaluator::apply_outcome(const Outcome& o) {
    detail::PassState& ps = passes_.at(o.pass);
    PassResult& pr = ps.result;
    if (o.wrong) {
        const double song_s = timeline_.song_at(o.press_ns);
        record_wrong(summary_, measure_at(song_s));
        if (wrong_presses_.size() < kMaxKeptWrongPresses) {
            wrong_presses_.push_back({o.key, o.press_ns, o.velocity, song_s});
        } else {
            ++dropped_wrong_presses_;
        }
        ++pr.wrong;
        ps.combo = 0;
        return;
    }
    const Target& t = targets_[o.target];
    if (o.unjudged) {
        summary_.unjudged += static_cast<int>(t.notes.size());
        return;
    }
    const double press_song_s = o.hit ? timeline_.song_at(o.press_ns) : 0.0;
    for (std::uint32_t idx : t.notes) {
        const Note& n = (*notes_)[idx];
        NoteResult r;
        r.note_index = idx;
        r.pass = o.pass;
        r.expected_pitch = t.key;
        r.hand = n.hand;
        r.measure = n.measure;
        r.song_s = n.start_s;
        r.arrival_ns = o.arrival_ns;
        if (o.hit) {
            r.press_ns = o.press_ns;
            r.error_ns = o.press_ns - o.arrival_ns;
            r.grade = grade(r.error_ns);
            r.velocity = o.velocity;
            r.press_song_s = press_song_s;
            r.timing_error_ns = r.error_ns;
        } else {
            r.grade = NoteGrade::Missed;
            ++pr.missed;
        }
        ++pr.graded;
        tally(ps, r.grade);
        record(summary_, r);
    }
}

void FlowEvaluator::apply_outcomes(std::int64_t safe) {
    stable_sort_short(outcomes_.begin(), outcomes_.end(),
                      [](const Outcome& a, const Outcome& b) { return a.time_ns < b.time_ns; });
    std::size_t used = 0;
    while (used < outcomes_.size() && outcomes_[used].time_ns < safe) {
        apply_outcome(outcomes_[used]);
        ++used;
    }
    outcomes_.erase(outcomes_.begin(), outcomes_.begin() + static_cast<std::ptrdiff_t>(used));
}

void FlowEvaluator::update(std::int64_t now_ns) {
    sync_transposition();
    const std::int64_t clock = moving_ ? clock_ns_ : std::max(clock_ns_, now_ns);
    const std::int64_t frontier = std::min(now_ns - settle_ns_, clock);
    resolve(frontier);
    std::int64_t safe = frontier;
    for (const Pending& p : pending_) {
        safe = std::min(safe, p.arrival_ns);
    }
    for (const Press& p : presses_) {
        safe = std::min(safe, p.time_ns);
    }
    apply_outcomes(safe);
    std::erase_if(unsure_, [&](std::int64_t u) { return u < safe - 2 * window_ns_; });
    passes_.report([&](const detail::PassState& p) { return p.end_ns < safe; });
    timeline_.forget_before(safe - window_ns_ - kHistoryNs);
    prune_dead(dead_, safe - window_ns_ - kHistoryNs);
}

void FlowEvaluator::finish() {
    resolve(kForever);
    apply_outcomes(kForever);
    passes_.report([](const detail::PassState&) { return true; });
}

std::size_t FlowEvaluator::practised_notes() const {
    return practised_;
}

void FlowEvaluator::take_passes(std::vector<PassResult>& out) {
    passes_.take([&](const detail::PassState& p) {
        PassResult r = p.result;
        r.start_s = p.start_s;
        r.end_s = p.end_s;
        r.clean = !p.interrupted && p.start_s <= p.loop_start_s + kEpsilonS && r.missed == 0 &&
                  r.wrong == 0;
        out.push_back(r);
    });
}

// WaitEvaluator

} // namespace etude
