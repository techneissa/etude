// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_controller.hpp"

#include "hand_split_job.hpp"
#include "lent_song.hpp"
#include "practice_tables_job.hpp"

#include "etude/look_ahead.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace etude::app {

void SongController::rebuild_evaluators(std::int64_t /*now_ns*/, bool same_song) {
    ++evaluator_rebuilds_;
    FlowOptions f;
    f.window_ms = options_.hit_window_ms;
    f.latency_ms = options_.latency_ms;
    // Only the options changed: the evaluators keep the hands they grade for (a change of hands may
    // be waiting for its tables), so nothing is built here for a long song.
    applied_hands_ = same_song && loaded_ ? applied_hands_ : practice_hands();
    f.hands = applied_hands_;
    f.lookahead_s = options_.lookahead_s;
    f.settle_ms += options_.input_delay_ms;
    WaitOptions w;
    w.hands = applied_hands_;
    w.lookahead_s = options_.lookahead_s;
    w.settle_ms += options_.input_delay_ms;
    const Song empty;
    const Song& song = loaded_ ? song_.song : empty;
    if (prepared_ != nullptr) {
        flow_.set_song(song, f, std::move(prepared_->flow), prepared_fingerprint_);
        wait_.set_song(song, w, std::move(prepared_->wait), prepared_fingerprint_);
        if (wait_.note_table() != flow_.note_table()) {
            wait_.set_song(song, w, flow_.note_table()); // tables made for another song: one copy
        }
    } else if (same_song && loaded_ && flow_.reconfigure(f) && wait_.reconfigure(w)) {
        // Only the options changed: the tables stay (rebuilding them for a hit-window slider would
        // stall every frame of a long song).
    } else {
        flow_.set_song(song, f);
        wait_.set_song(song, w, flow_.note_table()); // both evaluators share one table of notes
    }
    results_seen_ = 0;
    reset_feel(true);
    events_.clear();
    grade_by_note_.assign(song.notes.size(), -1);
    passes_.clear();
}

void SongController::restart_here(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    if (transport_.held()) {
        transport_.release(now_ns);
    }
    transport_.seek(now_ns, transport_.position_s());
    pump(now_ns);
    follow_wait(now_ns);
}

// Forgets the run in progress: it is not an attempt and leaves no record.
void SongController::discard_run() {
    run_active_ = false;
    run_setup_changed_ = false;
    events_.clear();
    passes_.clear();
}

void SongController::start_run_if_needed(std::int64_t now_ns) {
    if (run_active_ || !loaded_) {
        return;
    }
    run_active_ = true;
    run_speed_percent_ = transport_.speed_percent();
    run_semitones_ = semitones_;
    run_lookahead_s_ = options_.lookahead_s;
    run_setup_changed_ = false;
    run_started_ns_ = now_ns;
    run_playing_ns_ = 0;
    passes_.clear();
}

const EvaluationSummary* SongController::summary() const {
    if (!loaded_) {
        return nullptr;
    }
    return mode_ == PracticeMode::Wait ? &wait_.summary() : &flow_.summary();
}

void SongController::collect_grades(std::int64_t now_ns) {
    while (!grades_.empty() && now_ns - grades_.front().at_ns > kGradeShowNs) {
        grades_.erase(grades_.begin());
    }
    const EvaluationSummary* s = summary();
    if (s == nullptr) {
        return;
    }
    for (std::size_t i = results_seen_; i < s->notes.size(); ++i) {
        const NoteResult& r = s->notes[i];
        if (r.note_index < grade_by_note_.size()) {
            grade_by_note_[r.note_index] = static_cast<std::int8_t>(r.grade);
        }
        grades_.push_back({r.note_index, r.grade, r.expected_pitch, r.hand, now_ns});
        // Only Flow Mode's graded presses are timing samples: a Miss has no press, and Wait's
        // error_ns is how long the song waited.
        if (mode_ == PracticeMode::Flow && r.grade != NoteGrade::Missed) {
            feel_.add_press(r.error_ns);
        }
        events_.add(r);
    }
    results_seen_ = s->notes.size();
    const std::span<const int> losses =
        std::span<const int>(s->combo_losses).subspan(std::min(losses_seen_, s->combo_losses.size()));
    const ComboEvents e = feel_.update_combo(s->combo, losses);
    losses_seen_ = s->combo_losses.size();
    if (e.milestone.kind != ComboEventKind::None) {
        combo_milestone_ = e.milestone;
    }
    if (e.loss.kind != ComboEventKind::None) {
        combo_break_ = e.loss;
    }
}

void SongController::reset_feel(bool whole_run) {
    const EvaluationSummary* s = summary();
    const int combo = s != nullptr ? s->combo : 0;
    if (whole_run) {
        feel_.reset(combo);
    } else {
        feel_.rebase_combo(combo);
    }
    losses_seen_ = s != nullptr ? s->combo_losses.size() : 0;
    combo_milestone_ = {};
    combo_break_ = {};
}

void SongController::collect_wrongs(std::int64_t now_ns) {
    std::erase_if(wrongs_,
                  [now_ns](const WrongMark& m) { return now_ns - m.at_ns > kWrongFlashNs; });
    // Both evaluators hear every press; only the mode's wrong presses are shown.
    for (const PracticeMode m : {PracticeMode::Flow, PracticeMode::Wait}) {
        wrong_scratch_.clear();
        int dropped = 0;
        if (m == PracticeMode::Flow) {
            flow_.take_wrong_presses(wrong_scratch_);
            dropped = flow_.take_dropped_wrong_presses();
        } else {
            wait_.take_wrong_presses(wrong_scratch_);
            dropped = wait_.take_dropped_wrong_presses();
        }
        if (m != mode_) {
            continue;
        }
        events_.add_dropped(dropped);
        for (const WrongPress& p : wrong_scratch_) {
            events_.add_wrong(p);
            if (wrongs_.size() < kMaxKeptWrongPresses) {
                wrongs_.push_back({p.key, now_ns});
            }
        }
    }
}

bool SongController::press_looks_wrong(int key) const {
    if (!loaded_) {
        return false;
    }
    return mode_ == PracticeMode::Wait ? wait_.press_looks_wrong(key)
                                       : flow_.press_looks_wrong(key);
}

bool SongController::press_pending(int key) const {
    return loaded_ && mode_ == PracticeMode::Wait && wait_.press_pending(key);
}

bool SongController::note_struck(int key, bool key_down, double note_start_s, int grade) const {
    if (!loaded_ || key < 0 || key > 127) {
        return false;
    }
    const std::optional<double> start =
        mode_ == PracticeMode::Wait ? wait_.struck_start(key) : flow_.struck_start(key);
    return etude::note_struck(start, key_down, note_start_s, grade);
}

void SongController::set_profile(std::int64_t id, std::int64_t now_ns) {
    if (id <= 0 || id == profile_id_) {
        return;
    }
    finish_run(now_ns);
    profile_id_ = id;
}

void SongController::finish_run(std::int64_t now_ns) {
    if (!run_active_) {
        return;
    }
    watch_run_setup();
    run_active_ = false;
    if (mode_ == PracticeMode::Flow) {
        flow_.finish();
    } else {
        wait_.settle(now_ns); // the presses made before the end belong to this run
    }
    collect_grades(now_ns);
    collect_wrongs(now_ns); // the wrong presses the end just decided
    std::vector<PassResult> passes;
    if (mode_ == PracticeMode::Wait) {
        wait_.take_passes(passes);
    } else {
        flow_.take_passes(passes);
    }
    passes_.insert(passes_.end(), passes.begin(), passes.end());
    const EvaluationSummary* s = summary();
    // A run where nothing was played (watching, or stepping away) is not an attempt.
    if (s != nullptr && is_attempt(*s)) {
        RunContext ctx;
        ctx.song_hash = song_.hash;
        ctx.song_title = song_.title;
        ctx.mode = mode_ == PracticeMode::Wait ? RunMode::Wait : RunMode::Flow;
        ctx.hands = applied_hands_;
        ctx.speed_percent = transport_.speed_percent();
        ctx.transpose_semitones = semitones_;
        ctx.duration_ms = run_playing_ns_ / 1'000'000;
        const std::size_t practised =
            mode_ == PracticeMode::Wait ? wait_.practised_notes() : flow_.practised_notes();
        // Not sorted here: the history thread orders them (finish_ordering) before it writes them.
        RunEvents events = events_.hand_over(practised);
        events.setup_changed = run_setup_changed_;
        // Scored now, while the song it was played on is still the loaded one: a song opened next
        // is loaded after this run ends.
        ScoreContext score;
        score.song_notes = events.expected_notes;
        score.song_bars = static_cast<int>(song_.song.measures.size());
        score.difficulty_half_stars =
            song_.difficulty != nullptr && !song_.difficulty->empty() ? song_.difficulty->half_stars : 0;
        finished_.push_back({scored(make_run_record(*s, ctx, passes_), score), run_started_ns_,
                             now_ns,
                             song_.absolute_path.empty() ? song_.library_path : song_.absolute_path,
                             std::move(events), profile_id_});
    } else if (s != nullptr && s->all.graded == 0 && s->wrong > 0) {
        ungraded_run_ = true; // pressed keys, but no note was there to grade: no score, no record
    }
    passes_.clear();
    events_.clear();
    flow_.reset_results();
    wait_.reset_results();
    results_seen_ = 0;
    reset_feel(true);
    std::fill(grade_by_note_.begin(), grade_by_note_.end(), static_cast<std::int8_t>(-1));
}

} // namespace etude::app
