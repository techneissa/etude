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

void WaitEvaluator::reset_clock() {
    forget_presses();
    timeline_.clear();
    inbox_.clear();
    keys_ = {};
    position_s_ = 0.0;
    last_op_ns_ = kNever;
    playing_ = false;
    held_ = false;
    held_since_ns_ = 0;
    restart_ns_ = kNever;
    dead_.clear();
    dead_.push_back({kNever, kForever});
}

void WaitEvaluator::set_lookahead(double lookahead_s, std::int64_t now_ns) {
    settle(now_ns); // what was played before the change is judged under the old reach
    options_.lookahead_s = sane(lookahead_s, 0.0, 60.0, WaitOptions{}.lookahead_s);
}

void WaitEvaluator::apply_options(const WaitOptions& options) {
    const WaitOptions d;
    options_ = options;
    options_.early_ms = sane(options_.early_ms, 0.0, 2000.0, d.early_ms);
    options_.release_slack_ms = sane(options_.release_slack_ms, 0.0, 10000.0, d.release_slack_ms);
    options_.settle_ms = sane(options_.settle_ms, 0.0, 1000.0, d.settle_ms);
    options_.lookahead_s = sane(options_.lookahead_s, 0.0, 60.0, d.lookahead_s);
    if (source_ != nullptr) {
        options_.transposition = source_->value();
        source_version_ = source_->version();
        source_synced_ = true;
    }
    early_ns_ = to_ns(options_.early_ms);
    slack_ns_ = to_ns(options_.release_slack_ms);
    settle_ns_ = to_ns(options_.settle_ms);
}

void WaitEvaluator::set_song(const Song& song, const WaitOptions& options) {
    settle(kForever); // judged against the song they were played to
    apply_options(options);
    notes_ = std::make_shared<const std::vector<Note>>(song.notes);
    retire_chords();
    build_chords();
    start_song();
}

bool WaitEvaluator::reconfigure(const WaitOptions& options) {
    if (options.hands != options_.hands) {
        return false;
    }
    settle(kForever); // judged against the options they were played under
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

void WaitEvaluator::set_song(const Song& /*song*/, const WaitOptions& options, NoteTable notes) {
    settle(kForever);
    apply_options(options);
    notes_ = std::move(notes);
    retire_chords();
    build_chords();
    start_song();
}

void WaitEvaluator::set_song(const Song& song, const WaitOptions& options, Prepared&& prepared) {
    set_song(song, options, std::move(prepared), song_fingerprint(song));
}

void WaitEvaluator::set_song(const Song& song, const WaitOptions& options, Prepared&& prepared,
                             std::uint64_t fingerprint) {
    settle(kForever);
    apply_options(options);
    if (prepared.built_ && prepared.hands_ == options_.hands &&
        prepared.transposition_ == options_.transposition &&
        prepared.fingerprint_ == fingerprint) {
        notes_ = std::move(prepared.notes_);
        retire_chords();
        chords_ = std::move(prepared.chords_);
        unplayable_s_ = std::move(prepared.unplayable_s_);
        practised_ = prepared.practised_;
        bars_ = prepared.bars_;
        prepared.built_ = false;
    } else {
        notes_ = std::make_shared<const std::vector<Note>>(song.notes);
        retire_chords();
        build_chords();
    }
    start_song();
}



WaitEvaluator::Prepared WaitEvaluator::prepare(const Song& song, HandFilter hands,
                                               const Transposition& transposition) {
    return prepare(song, hands, transposition, song_fingerprint(song));
}

WaitEvaluator::Prepared WaitEvaluator::prepare(const Song& song, HandFilter hands,
                                               const Transposition& transposition,
                                               std::uint64_t fingerprint) {
    return prepare(std::make_shared<const std::vector<Note>>(song.notes), hands, transposition,
                   fingerprint);
}

WaitEvaluator::Prepared WaitEvaluator::prepare(NoteTable notes, HandFilter hands,
                                               const Transposition& transposition,
                                               std::uint64_t fingerprint, const AbortCheck& abort) {
    Prepared p;
    p.fingerprint_ = fingerprint;
    p.hands_ = hands;
    p.transposition_ = transposition;
    p.chords_ = chords_for(*notes, hands, transposition, p.unplayable_s_, p.practised_, p.bars_,
                           abort);
    if (stopped(abort)) {
        return {};
    }
    p.notes_ = std::move(notes);
    p.built_ = true;
    return p;
}

void WaitEvaluator::offer_targets(Prepared&& prepared) {
    if (offered_.built_) {
        retired_.push_back(std::move(offered_));
    }
    offered_ = std::move(prepared);
}

// As FlowEvaluator::retire_targets.
void WaitEvaluator::retire_chords() {
    if (!chords_.empty() || !unplayable_s_.empty()) {
        Prepared old;
        old.chords_ = std::move(chords_);
        old.unplayable_s_ = std::move(unplayable_s_);
        retired_.push_back(std::move(old));
    }
    chords_.clear();
    unplayable_s_.clear();
    if (offered_.built_) {
        retired_.push_back(std::move(offered_));
    }
    offered_ = {};
}

void WaitEvaluator::start_song() {
    waiting_keys_.reserve(128); // a chord holds each key once, so waiting_keys() never grows it
    wrong_presses_.clear();
    dropped_wrong_presses_ = 0;
    wrong_presses_.reserve(kMaxKeptWrongPresses);
    forget_presses();
    // The clock and the keys stay as they are: wait for the first chord at or after the position,
    // with every key down pressed again.
    restart_at(position_s_, last_op_ns_);
    detail::PassState fresh;
    fresh.interrupted = playing_; // starts mid-song, so it can't be a clean pass
    fresh.moved = playing_;
    passes_.reset(fresh);
    reset_results();
}

std::vector<WaitEvaluator::Chord> WaitEvaluator::chords_for(const std::vector<Note>& notes,
                                                            HandFilter hands,
                                                            const Transposition& transposition,
                                                            std::vector<double>& unplayable_s,
                                                            std::size_t& practised,
                                                            std::size_t& bars,
                                                            const AbortCheck& abort) {
    std::vector<Chord> chords;
    practised = 0;
    std::size_t counted = 0;
    for (KeyGroup& g : key_groups(notes, hands, transposition, unplayable_s, abort)) {
        if (++counted % kAbortEvery == 0 && stopped(abort)) {
            return {};
        }
        practised += g.notes.size();
        if (chords.empty() || g.start_s - chords.back().start_s > kChordSeconds + kEpsilonS) {
            Chord c;
            c.start_s = g.start_s;
            c.measure = g.measure;
            chords.push_back(std::move(c));
        }
        std::vector<ChordKey>& keys = chords.back().keys;
        auto it = std::find_if(keys.begin(), keys.end(),
                               [&](const ChordKey& k) { return k.key == g.key; });
        if (it == keys.end()) {
            keys.push_back({g.key, std::move(g.notes)});
        } else {
            it->notes.insert(it->notes.end(), g.notes.begin(), g.notes.end());
        }
    }
    bars = 1;
    int last_bar = -1;
    for (Chord& c : chords) {
        std::sort(c.keys.begin(), c.keys.end(),
                  [](const ChordKey& a, const ChordKey& b) { return a.key < b.key; });
        bars += c.measure != last_bar ? 1 : 0;
        last_bar = c.measure;
    }
    return chords;
}

void WaitEvaluator::build_chords() {
    ++builds_;
    chords_ = chords_for(*notes_, options_.hands, options_.transposition, unplayable_s_, practised_,
                         bars_);
}

void WaitEvaluator::follow(const TranspositionSource* source) {
    source_ = source;
    source_synced_ = false;
}

void WaitEvaluator::sync_transposition() {
    if (source_ == nullptr || (source_synced_ && source_version_ == source_->version())) {
        return;
    }
    source_synced_ = true;
    source_version_ = source_->version();
    set_transposition(source_->value(), source_->changed_at_ns());
}

void WaitEvaluator::set_transposition(const Transposition& transposition, std::int64_t now_ns) {
    if (transposition == options_.transposition) {
        return;
    }
    settle(now_ns); // what was played before the change is judged under the old mapping
    options_.transposition = transposition;
    if (offered_.built_ && offered_.notes_ == notes_ && offered_.hands_ == options_.hands &&
        offered_.transposition_ == transposition) {
        Prepared taken = std::move(offered_);
        offered_ = {};
        retire_chords();
        chords_ = std::move(taken.chords_);
        unplayable_s_ = std::move(taken.unplayable_s_);
        practised_ = taken.practised_;
        bars_ = taken.bars_;
    } else {
        retire_chords();
        build_chords();
    }
    restart_at(position_s_, now_ns);
    detail::PassState& pass = passes_.back();
    pass.interrupted = pass.interrupted || pass.moved;
}

void WaitEvaluator::reset_results() {
    summary_ = {};
    // Room for one pass of results (see FlowEvaluator::reset_results).
    summary_.notes.reserve(practised_);
    summary_.combo_losses.reserve(practised_);
    summary_.measures.reserve(bars_);
    restart_passes(passes_);
    obligations_.clear(); // they point into summary_.notes
}

std::size_t WaitEvaluator::practised_notes() const {
    return practised_;
}

double WaitEvaluator::waiting_for_s() const {
    return next_ < chords_.size() ? chords_[next_].start_s : -1.0;
}

std::span<const int> WaitEvaluator::waiting_keys() const {
    waiting_keys_.clear();
    if (next_ < chords_.size()) {
        for (const ChordKey& k : chords_[next_].keys) {
            waiting_keys_.push_back(k.key);
        }
    }
    return waiting_keys_;
}

} // namespace etude
