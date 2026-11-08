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

SongLoadPlan SongController::load_plan() const {
    return {hands_, source_.value(), options_.lookahead_s};
}

SongLoadPlan SongController::load_plan_for(const SessionState& state) const {
    const SessionState saved = state.clamped();
    const int semitones = std::clamp(saved.transpose_semitones, -kMaxTransposeSemitones,
                                     kMaxTransposeSemitones);
    return {saved.practice_hands, Transposition(semitones, keyboard_), options_.lookahead_s};
}

PreparedSong SongController::prepare(const Song& song, const SongLoadPlan& plan,
                                     std::uint64_t fingerprint) {
    PreparedSong p;
    // Once: the tables below carry it. A caller that already took it passes it in.
    p.fingerprint = fingerprint != 0 ? fingerprint : song_fingerprint(song);
    p.present = etude::hands_present(song);
    p.hands = clamp_hands(plan.hands, p.present);
    p.transposition = plan.transposition;
    p.densest_window = etude::densest_window(song, plan.ahead_s);
    const auto notes = std::make_shared<const std::vector<Note>>(song.notes); // one table of notes
    const auto measures = std::make_shared<const std::vector<Measure>>(song.measures);
    p.flow = FlowEvaluator::prepare(notes, measures, p.hands, p.transposition, p.fingerprint);
    p.wait = WaitEvaluator::prepare(notes, p.hands, p.transposition, p.fingerprint);
    p.window.build(song);
    p.playback = Playback::prepare(song, p.fingerprint);
    p.valid = true;
    return p;
}

void SongController::load(LoadedSong song, std::int64_t now_ns) {
    load(std::move(song), now_ns, PreparedSong{});
}

void SongController::load(LoadedSong song, std::int64_t now_ns, PreparedSong&& prepared) {
    pump(now_ns);
    stop_audition(now_ns);
    finish_run(now_ns);
    cancel_countdown();
    // A song opened while a menu is up (the library's) waits for Play: the song that was
    // playing when the menu opened was the one to resume.
    resume_after_menu_ = false;
    loop_a_.reset();
    loop_b_.reset();
    loop_passes_.clear();
    ++load_serial_;
    ++notes_serial_;
    drop_tables();
    split_job_->cancel(); // a split made for the old song is of no use to this one
    split_point_ = applied_split_ = kAutoSplit;
    suggested_split_ = 0;
    split_seconds_ = 0.0;
    splits_applied_ = 0;
    ++split_serial_;
    never_offer_ = false; // the sync brings the song's saved choice back
    offer_settled_by_choice_ = false;
    {
        // A worker may be copying the old song for a split: it finishes first.
        const std::lock_guard lent(lent_->mutex);
        song_ = std::move(song);
        lent_->song = &song_.song;
        ++lent_->generation;
    }
    loaded_ = true;
    // Tables made ahead fit this song when they were built from it, note for note.
    // The loader took the song's fingerprint on its thread; a song built without one gets it here.
    if (song_.fingerprint == 0) {
        song_.fingerprint = song_fingerprint(song_.song);
    }
    const std::uint64_t fingerprint = song_.fingerprint;
    const bool ahead = prepared.valid && prepared.fingerprint == fingerprint;
    playback_tables_ =
        ahead ? std::move(prepared.playback) : Playback::prepare(song_.song, fingerprint);
    playback_fingerprint_ = fingerprint;
    playback_.set_song(song_.song, now_ns, scratch_, playback_tables_, fingerprint);
    enqueue_reactive(scratch_, now_ns, true);
    sound_cursor_.reset();
    audition_pb_.drop_song(now_ns, scratch_); // it takes the tables when an audition starts
    audition_stale_ = true;
    enqueue_plain(scratch_);
    audition_tp_.set_song(song_.song);
    audition_tp_.take_ops(audition_ops_);
    audition_ops_.clear();
    transport_.set_song(song_.song);
    transport_.set_speed_percent(now_ns, 100);
    acc_parts_ = etude::accompaniment_parts(song_.song);
    acc_muted_.clear();
    if (playback_.accompaniment_muted()) {
        playback_.set_accompaniment_muted(false, now_ns, scratch_); // a new song starts sounding
    }
    count_in_bars_ = std::clamp(options_.default_count_in_bars, 0, kMaxCountInBars);
    grid_.set_song(song_.song);
    if (ahead) {
        prepared_fingerprint_ = fingerprint;
        window_ = std::move(prepared.window);
        densest_window_ = prepared.densest_window;
        prepared_ = &prepared;
    } else {
        window_.build(song_.song);
        densest_window_ = etude::densest_window(song_.song, options_.lookahead_s);
    }
    grades_.clear();
    wrongs_.clear();
    wrongs_.reserve(kMaxKeptWrongPresses);
    wrong_scratch_.reserve(kMaxKeptWrongPresses);
    read_hands_scope(true);
    rebuild_evaluators(now_ns);
    prepared_ = nullptr;
    apply_mutes(now_ns);
    pump(now_ns);
}

void SongController::unload(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    pump(now_ns);
    stop_audition(now_ns);
    finish_run(now_ns);
    cancel_countdown();
    resume_after_menu_ = false;
    loop_a_.reset();
    loop_b_.reset();
    loop_passes_.clear();
    ++load_serial_;
    ++notes_serial_;
    split_job_->cancel();
    split_point_ = applied_split_ = kAutoSplit;
    suggested_split_ = 0;
    ++split_serial_;
    never_offer_ = false;
    offer_settled_by_choice_ = false;
    const Song empty;
    playback_.drop_song(now_ns, scratch_);
    enqueue_reactive(scratch_, now_ns, true);
    sound_cursor_.reset();
    audition_pb_.drop_song(now_ns, scratch_);
    audition_stale_ = true;
    playback_tables_ = {};
    enqueue_plain(scratch_);
    audition_tp_.set_song(empty);
    audition_tp_.take_ops(audition_ops_);
    audition_ops_.clear();
    transport_.set_song(empty);
    grid_.set_song(empty);
    acc_parts_.clear();
    acc_muted_.clear();
    loaded_ = false;
    drop_tables();
    {
        const std::lock_guard lent(lent_->mutex);
        song_ = LoadedSong{};
        lent_->song = nullptr;
        ++lent_->generation;
    }
    window_.build(empty);
    densest_window_ = 0;
    grades_.clear();
    wrongs_.clear();
    read_hands_scope(true);
    rebuild_evaluators(now_ns);
    pump(now_ns);
}

bool SongController::tables_inline() const {
    return !loaded_ || song_.song.notes.size() <= inline_table_notes_;
}

bool SongController::tables_pending() const {
    return loaded_ && (applied_hands_ != practice_hands() || !(source_.value() == wanted_));
}

void SongController::hold_tables(bool on) {
    if (tables_job_ == nullptr) {
        tables_job_ = std::make_unique<PracticeTablesJob>();
    }
    tables_job_->hold(on);
}

void SongController::wait_tables() {
    if (tables_job_ != nullptr) {
        tables_job_->wait_idle();
    }
}

void SongController::drop_tables() {
    ++tables_serial_;
    tables_asked_.reset();
    if (tables_job_ != nullptr) {
        tables_job_->cancel();
    }
}

void SongController::request_tables() {
    if (tables_job_ == nullptr) {
        tables_job_ = std::make_unique<PracticeTablesJob>();
    }
    TablesRequest request;
    request.serial = tables_serial_;
    request.hands = practice_hands();
    request.transposition = wanted_;
    request.fingerprint = playback_fingerprint_;
    request.notes = flow_.note_table();
    request.measures = flow_.measure_table();
    tables_asked_ = TablesAsked{request.hands, request.transposition};
    tables_job_->request(std::move(request));
}

// The evaluators grade for `applied_hands_` and the transposition in `source_`; the hands and the
// key asked for come into force together once the worker has their tables, so the notes, the sound
// and the grading change on the same frame and no press is judged against tables of another key.
// An answer built for anything but what is asked for now, or for another song, is dropped.
void SongController::update_tables(std::int64_t now_ns) {
    if (!tables_pending()) {
        return;
    }
    const HandFilter hands = practice_hands();
    if (tables_inline()) {
        // A small song, or one that replaced the long song a change was asked of.
        pump(now_ns);
        const bool hands_change = applied_hands_ != hands;
        if (hands_change) {
            finish_run(now_ns);
        }
        source_.set(wanted_, now_ns);
        pump(now_ns);
        if (hands_change) {
            rebuild_evaluators(now_ns);
            restart_here(now_ns);
        }
        return;
    }
    if (tables_job_ != nullptr) {
        if (std::optional<TablesResult> done = tables_job_->take()) {
            if (done->serial == tables_serial_ && done->hands == hands) {
                // The newest answer the worker has made, so never older than one installed. A key
                // a tap or two behind (a stepper held faster than a build lasts) is installed
                // too: the keys follow the taps as the worker finishes, the last one lands when
                // the taps stop. The newest request is still asked for below.
                if (tables_asked_ && tables_asked_->hands == done->hands &&
                    tables_asked_->transposition == done->transposition) {
                    tables_asked_.reset();
                }
                install_tables(std::move(*done), now_ns);
                if (!tables_pending()) {
                    return;
                }
            } else {
                // Built for other hands or another song: freed on the worker, not here.
                tables_job_->discard(std::move(done->flow), std::move(done->wait));
            }
        }
    }
    if (!tables_asked_ || tables_asked_->hands != hands || !(tables_asked_->transposition == wanted_)) {
        request_tables();
    }
}

void SongController::install_tables(TablesResult&& tables, std::int64_t now_ns) {
    pump(now_ns);
    const bool hands_change = applied_hands_ != tables.hands;
    const bool key_change = !(source_.value() == tables.transposition);
    if (hands_change) {
        // A run that began while the tables were built was graded for the hands asked away from:
        // it is dropped, and a fresh one goes on from here when the song is playing, as it would
        // have begun had the change been in force when Play was pressed.
        const bool playing_run = run_active_ && transport_.playing();
        discard_run();
        if (key_change) {
            source_.set(tables.transposition, now_ns);
            pump(now_ns);
        }
        PreparedSong ahead;
        ahead.flow = std::move(tables.flow);
        ahead.wait = std::move(tables.wait);
        prepared_ = &ahead;
        prepared_fingerprint_ = playback_fingerprint_;
        rebuild_evaluators(now_ns);
        prepared_ = nullptr;
        // Tables the evaluators did not take (made for something else by now) are freed there too.
        if (ahead.flow.note_count() > 0 || ahead.wait.note_count() > 0) {
            tables_job_->discard(std::move(ahead.flow), std::move(ahead.wait));
        }
        retire_tables();
        apply_mutes(now_ns);
        restart_here(now_ns);
        if (playing_run) {
            start_run_if_needed(now_ns);
        }
    } else if (key_change) {
        // The evaluators pick the change up from the source at their next event; they find their
        // targets for it waiting.
        flow_.offer_targets(std::move(tables.flow));
        wait_.offer_targets(std::move(tables.wait));
        source_.set(tables.transposition, now_ns);
        pump(now_ns);
        watch_run_setup();
    } else {
        tables_job_->discard(std::move(tables.flow), std::move(tables.wait));
    }
}

} // namespace etude::app
