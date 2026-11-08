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

// PassLog

namespace detail {

void PassLog::reset(PassState current) {
    current.result.pass = 0;
    passes_.assign(1, current);
    base_ = 0;
    reported_ = 0;
}

void PassLog::push(PassState p) {
    p.result.pass = base_ + static_cast<int>(passes_.size());
    passes_.push_back(p);
}

int PassLog::number_at(std::int64_t ns) const {
    for (std::size_t i = passes_.size(); i-- > 0;) {
        if (passes_[i].start_ns <= ns || i == 0) {
            return base_ + static_cast<int>(i);
        }
    }
    return base_;
}

} // namespace detail

// TransportTimeline

void TransportTimeline::apply(const TransportOp& op) {
    if (op.kind == Kind::Click) {
        return;
    }
    Span s;
    s.at_ns = op.at_ns;
    s.end_ns = op.kind == Kind::Move ? std::max(op.end_ns, op.at_ns) : op.at_ns;
    s.from_s = op.kind == Kind::Move ? op.from_s : op.to_s;
    s.to_s = op.to_s;
    s.speed_percent = op.kind == Kind::Move ? op.speed_percent
                      : spans_.empty()      ? 100
                                            : spans_.back().speed_percent;
    spans_.push_back(s);
}

double TransportTimeline::song_at(std::int64_t ns) const {
    if (spans_.empty()) {
        return 0.0;
    }
    // The last span starting at or before `ns`.
    auto it = std::upper_bound(spans_.begin(), spans_.end(), ns,
                               [](std::int64_t t, const Span& s) { return t < s.at_ns; });
    if (it == spans_.begin()) {
        return spans_.front().from_s;
    }
    const Span& s = *(it - 1);
    if (ns >= s.end_ns || s.end_ns == s.at_ns) {
        return s.to_s;
    }
    const double f = static_cast<double>(ns - s.at_ns) / static_cast<double>(s.end_ns - s.at_ns);
    return s.from_s + f * (s.to_s - s.from_s);
}

double TransportTimeline::rate_at(std::int64_t ns) const {
    auto it = std::upper_bound(spans_.begin(), spans_.end(), ns,
                               [](std::int64_t t, const Span& s) { return t < s.at_ns; });
    if (it == spans_.begin()) {
        return 1.0;
    }
    return (it - 1)->speed_percent / 100.0;
}

void TransportTimeline::forget_before(std::int64_t ns) {
    auto it = std::upper_bound(spans_.begin(), spans_.end(), ns,
                               [](std::int64_t t, const Span& s) { return t < s.at_ns; });
    if (it - spans_.begin() > 1) {
        spans_.erase(spans_.begin(), it - 1);
    }
}

// FlowEvaluator

void FlowEvaluator::reset_clock() {
    forget_presses();
    timeline_.clear();
    dead_.clear();
    dead_.push_back({kNever, kForever}); // nothing counts before the first play
    ignore_before_ns_ = kNever;
    grace_ = {0, 0};
    clock_ns_ = kNever;
    moving_ = false;
}

void FlowEvaluator::set_lookahead(double lookahead_s) {
    options_.lookahead_s = sane(lookahead_s, 0.0, 60.0, FlowOptions{}.lookahead_s);
}

void FlowEvaluator::apply_options(const FlowOptions& options) {
    const FlowOptions d;
    options_ = options;
    options_.window_ms = sane(options_.window_ms, 10.0, 500.0, d.window_ms);
    options_.perfect_fraction = sane(options_.perfect_fraction, 0.0, 1.0, d.perfect_fraction);
    options_.good_fraction = std::max(options_.perfect_fraction,
                                      sane(options_.good_fraction, 0.0, 1.0, d.good_fraction));
    options_.latency_ms = sane(options_.latency_ms, -1000.0, 1000.0, d.latency_ms);
    options_.settle_ms = sane(options_.settle_ms, 0.0, 1000.0, d.settle_ms);
    options_.lookahead_s = sane(options_.lookahead_s, 0.0, 60.0, d.lookahead_s);
    if (source_ != nullptr) {
        options_.transposition = source_->value();
        source_version_ = source_->version();
        source_synced_ = true;
    }
    window_ns_ = to_ns(options_.window_ms);
    perfect_ns_ = static_cast<std::int64_t>(
        std::llround(static_cast<double>(window_ns_) * options_.perfect_fraction));
    good_ns_ = static_cast<std::int64_t>(
        std::llround(static_cast<double>(window_ns_) * options_.good_fraction));
    latency_ns_ = to_ns(options_.latency_ms);
    settle_ns_ = to_ns(options_.settle_ms);
}

void FlowEvaluator::set_song(const Song& song, const FlowOptions& options) {
    apply_options(options);
    notes_ = std::make_shared<const std::vector<Note>>(song.notes);
    measures_ = std::make_shared<const std::vector<Measure>>(song.measures);
    retire_targets();
    build_targets();
    start_song();
}

bool FlowEvaluator::reconfigure(const FlowOptions& options) {
    if (options.hands != options_.hands) {
        return false;
    }
    const Transposition kept = options_.transposition;
    const bool synced = source_synced_;
    const std::uint64_t version = source_version_;
    apply_options(options);
    options_.transposition = kept;
    source_synced_ = synced;
    source_version_ = version;
    start_song();
    return true;
}

void FlowEvaluator::set_song(const Song& song, const FlowOptions& options, Prepared&& prepared) {
    set_song(song, options, std::move(prepared), song_fingerprint(song));
}

void FlowEvaluator::set_song(const Song& song, const FlowOptions& options, Prepared&& prepared,
                             std::uint64_t fingerprint) {
    apply_options(options);
    if (prepared.built_ && prepared.hands_ == options_.hands &&
        prepared.transposition_ == options_.transposition &&
        prepared.fingerprint_ == fingerprint) {
        notes_ = std::move(prepared.notes_);
        measures_ = std::move(prepared.measures_);
        retire_targets();
        targets_ = std::move(prepared.targets_);
        unplayable_s_ = std::move(prepared.unplayable_s_);
        practised_ = prepared.practised_;
        prepared.built_ = false;
    } else {
        notes_ = std::make_shared<const std::vector<Note>>(song.notes);
        measures_ = std::make_shared<const std::vector<Measure>>(song.measures);
        retire_targets();
        build_targets();
    }
    start_song();
}



FlowEvaluator::Prepared FlowEvaluator::prepare(const Song& song, HandFilter hands,
                                               const Transposition& transposition) {
    return prepare(song, hands, transposition, song_fingerprint(song));
}

FlowEvaluator::Prepared FlowEvaluator::prepare(const Song& song, HandFilter hands,
                                               const Transposition& transposition,
                                               std::uint64_t fingerprint) {
    return prepare(std::make_shared<const std::vector<Note>>(song.notes),
                   std::make_shared<const std::vector<Measure>>(song.measures), hands,
                   transposition, fingerprint);
}

FlowEvaluator::Prepared FlowEvaluator::prepare(NoteTable notes, MeasureTable measures,
                                               HandFilter hands, const Transposition& transposition,
                                               std::uint64_t fingerprint, const AbortCheck& abort) {
    Prepared p;
    p.fingerprint_ = fingerprint;
    p.hands_ = hands;
    p.transposition_ = transposition;
    p.targets_ = targets_for(*notes, hands, transposition, p.unplayable_s_, p.practised_, abort);
    if (stopped(abort)) {
        return {}; // an empty answer: nothing was built, and note_count() says so
    }
    p.notes_ = std::move(notes);
    p.measures_ = std::move(measures);
    p.built_ = true;
    return p;
}

void FlowEvaluator::offer_targets(Prepared&& prepared) {
    if (offered_.built_) {
        retired_.push_back(std::move(offered_)); // an offer nobody took: freed off the frame thread
    }
    offered_ = std::move(prepared);
}

// The tables a swap replaces, and an offer that was never taken, are kept for take_retired(): a
// heap vector for each note is milliseconds to free.
void FlowEvaluator::retire_targets() {
    if (!targets_.empty() || !unplayable_s_.empty()) {
        Prepared old;
        old.targets_ = std::move(targets_);
        old.unplayable_s_ = std::move(unplayable_s_);
        retired_.push_back(std::move(old));
    }
    targets_.clear();
    unplayable_s_.clear();
    if (offered_.built_) {
        retired_.push_back(std::move(offered_));
    }
    offered_ = {};
}

void FlowEvaluator::start_song() {
    // What was undecided belongs to the old song. The clock stays as the Transport last said.
    pending_.clear();
    presses_.clear();
    outcomes_.clear();
    unsure_.clear();
    wrong_presses_.clear();
    dropped_wrong_presses_ = 0;
    forget_presses();
    matching_cells_ = 0;
    reserve_working_space();
    if (clock_ns_ != kNever) {
        ignore_before_ns_ = std::max(ignore_before_ns_, clock_ns_);
        grace_ = {clock_ns_, clock_ns_ + window_ns_};
    }
    detail::PassState fresh;
    fresh.interrupted = moving_; // starts mid-song, so it can't be a clean pass
    fresh.moved = moving_;
    passes_.reset(fresh);
    reset_results();
}

// Room for the undecided notes and presses and for deciding one group, sized for a busy passage
// (a few dozen notes of a key within one window), so that playing a song allocates nothing; a
// longer run of one key grows the buffers once.
void FlowEvaluator::reserve_working_space() {
    constexpr std::size_t kNotes = 64;
    constexpr std::size_t kPresses = 128;
    constexpr std::size_t kCells = 2048;
    pending_.reserve(kNotes);
    presses_.reserve(kPresses);
    outcomes_.reserve(kNotes + kPresses);
    unsure_.reserve(kPresses);
    wrong_presses_.reserve(kMaxKeptWrongPresses);
    match_lo_.reserve(kNotes + 1);
    match_hi_.reserve(kNotes + 1);
    match_row_.reserve(kNotes + 2);
    match_cells_.reserve(kCells);
    match_pick_.reserve(kNotes);
    match_used_.reserve(kPresses);
}

std::vector<FlowEvaluator::Target> FlowEvaluator::targets_for(const std::vector<Note>& notes,
                                                              HandFilter hands,
                                                              const Transposition& transposition,
                                                              std::vector<double>& unplayable_s,
                                                              std::size_t& practised,
                                                              const AbortCheck& abort) {
    std::vector<KeyGroup> groups = key_groups(notes, hands, transposition, unplayable_s, abort);
    std::vector<Target> targets;
    targets.reserve(groups.size());
    practised = 0;
    std::size_t counted = 0;
    for (KeyGroup& g : groups) {
        if (++counted % kAbortEvery == 0 && stopped(abort)) {
            return {};
        }
        practised += g.notes.size();
        targets.push_back({g.start_s, g.key, g.measure, std::move(g.notes)});
    }
    return targets;
}

void FlowEvaluator::build_targets() {
    ++builds_;
    targets_ = targets_for(*notes_, options_.hands, options_.transposition, unplayable_s_,
                           practised_);
}

void FlowEvaluator::follow(const TranspositionSource* source) {
    source_ = source;
    source_synced_ = false;
}

void FlowEvaluator::sync_transposition() {
    if (source_ == nullptr || (source_synced_ && source_version_ == source_->version())) {
        return;
    }
    source_synced_ = true;
    source_version_ = source_->version();
    set_transposition(source_->value(), source_->changed_at_ns());
}

void FlowEvaluator::set_transposition(const Transposition& transposition, std::int64_t now_ns) {
    if (transposition == options_.transposition) {
        return;
    }
    drop_undecided(now_ns);
    grace_ = {now_ns, now_ns + window_ns_};
    options_.transposition = transposition;
    if (offered_.built_ && offered_.notes_ == notes_ && offered_.hands_ == options_.hands &&
        offered_.transposition_ == transposition) {
        Prepared taken = std::move(offered_);
        offered_ = {};
        retire_targets();
        targets_ = std::move(taken.targets_);
        unplayable_s_ = std::move(taken.unplayable_s_);
        practised_ = taken.practised_;
    } else {
        retire_targets();
        build_targets();
    }
    detail::PassState& pass = passes_.back();
    pass.interrupted = pass.interrupted || pass.moved;
}

void FlowEvaluator::drop_undecided(std::int64_t at_ns) {
    forget_presses();
    resolve(std::min(at_ns - settle_ns_, clock_ns_));
    for (Outcome& o : outcomes_) {
        apply_outcome(o);
    }
    outcomes_.clear();
    pending_.clear();
    presses_.clear();
    unsure_.clear();
    ignore_before_ns_ = std::max(ignore_before_ns_, at_ns);
}

void FlowEvaluator::reset_results() {
    summary_ = {};
    // Room for one pass of results, so grading a song through allocates nothing; a loop that is
    // played over and over grows the list by doubling, once in a long while.
    summary_.notes.reserve(practised_);
    summary_.combo_losses.reserve(practised_);
    summary_.measures.reserve(measures_->size() + 1);
    restart_passes(passes_);
    for (Pending& p : pending_) {
        p.pass = 0;
    }
    for (Outcome& o : outcomes_) {
        o.pass = 0;
    }
}

NoteGrade FlowEvaluator::grade(std::int64_t error_ns) const {
    const std::int64_t a = error_ns < 0 ? -error_ns : error_ns;
    if (a <= perfect_ns_) {
        return NoteGrade::Perfect;
    }
    if (a <= good_ns_) {
        return NoteGrade::Good;
    }
    return error_ns < 0 ? NoteGrade::Early : NoteGrade::Late;
}

} // namespace etude
