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

bool SongController::read_hands_scope(bool force) {
    const bool loop = loaded_ && transport_.loop_enabled();
    const double a = loop ? transport_.loop_start_s() : 0.0;
    const double b = loop ? transport_.loop_end_s() : 0.0;
    if (!force && loop == scope_loop_ && a == scope_start_s_ && b == scope_end_s_) {
        return false;
    }
    scope_loop_ = loop;
    scope_start_s_ = a;
    scope_end_s_ = b;
    present_ = !loaded_                ? HandsPresent{}
               : loop                  ? etude::hands_present(song_.song, a, b)
               : prepared_ != nullptr ? prepared_->present
                                      : etude::hands_present(song_.song);
    return true;
}

void SongController::refresh_hands_scope(std::int64_t now_ns) {
    if (!read_hands_scope(false)) {
        return;
    }
    if (practice_hands() != applied_hands_) {
        finish_run(now_ns);
        if (tables_inline()) {
            rebuild_evaluators(now_ns);
            restart_here(now_ns);
        } else {
            update_tables(now_ns); // the worker builds them; restart_here comes with them
        }
    }
    apply_mutes(now_ns); // "the other hand" appears or goes with the second hand
}

void SongController::set_practice_hands(HandFilter hands, std::int64_t now_ns) {
    if (hands == hands_) {
        return;
    }
    pump(now_ns);
    const HandFilter before = practice_hands();
    hands_ = hands;
    if (practice_hands() == before) {
        return; // no change in what is practised (the song has one hand's notes only)
    }
    finish_run(now_ns); // each hand's results are kept apart
    if (tables_inline()) {
        rebuild_evaluators(now_ns);
        apply_mutes(now_ns);
        restart_here(now_ns);
    } else {
        apply_mutes(now_ns);
        update_tables(now_ns); // the worker builds them; restart_here comes with them
    }
}

void SongController::set_hand_split(int split_point, std::int64_t /*now_ns*/) {
    split_point = sanitized_split_point(split_point);
    if (split_point == split_point_) {
        return;
    }
    split_point_ = split_point;
    if (!hand_split_available()) {
        return; // kept for the song's record; there is nothing here to split
    }
    HandSplitRequest request;
    request.file_hands = song_.file_hands;
    request.lent = lent_;
    request.generation = lent_->generation;
    request.hash = song_.hash;
    request.abc_chords = song_.abc_chords;
    request.split_point = split_point;
    request.plan = load_plan();
    request.serial = ++split_serial_;
    split_job_->request(std::move(request));
}

int SongController::hand_split_point() const {
    if (split_point_ != kAutoSplit) {
        return split_point_;
    }
    if (!loaded_) {
        return kMiddleC;
    }
    if (applied_split_ != kAutoSplit) {
        return applied_split_; // Auto is being made: the marker stays until its hands are in
    }
    if (suggested_split_ == 0) {
        suggested_split_ = suggested_split_point(song_.song);
    }
    return suggested_split_;
}

void SongController::wait_hand_split(std::int64_t now_ns) {
    split_job_->wait_idle();
    poll_hand_split(now_ns);
}

void SongController::poll_hand_split(std::int64_t now_ns) {
    if (!loaded_) {
        return;
    }
    // The worker may be copying the song for the next split, which nothing may write meanwhile: the
    // answer stays with the job for a frame.
    std::unique_lock lent(lent_->mutex, std::try_to_lock);
    if (!lent.owns_lock()) {
        return;
    }
    std::optional<HandSplitResult> done = split_job_->take();
    if (!done || done->serial != split_serial_ || done->split_point != split_point_ ||
        done->split_point == applied_split_ || done->song.notes.size() != song_.song.notes.size()) {
        return;
    }
    pump(now_ns);
    stop_audition(now_ns);
    finish_run(now_ns); // each split's results are kept apart, as each hand's are
    song_.song = std::move(done->song);
    song_.fingerprint = done->prepared.valid ? done->prepared.fingerprint : 0; // of the new notes
    lent.unlock();
    if (done->difficulty != nullptr) {
        song_.difficulty = std::move(done->difficulty);
    }
    applied_split_ = done->split_point;
    ++notes_serial_;
    drop_tables(); // built from the notes as they were
    suggested_split_ = done->suggested;
    split_seconds_ = std::max(split_seconds_, done->seconds);
    // The tables built from the old hands go, as a load() would replace them, but the transport,
    // the loop and the accompaniment stay where they are. The worker made the new ones from this
    // very song and nothing has touched it since, so its fingerprint is the song's: the pass over
    // the notes is not made again here.
    PreparedSong& prepared = done->prepared;
    const bool ahead = prepared.valid;
    if (!ahead) {
        song_.fingerprint = song_fingerprint(song_.song);
    }
    const std::uint64_t fingerprint = song_.fingerprint;
    playback_tables_ =
        ahead ? std::move(prepared.playback) : Playback::prepare(song_.song, fingerprint);
    playback_fingerprint_ = fingerprint;
    playback_.set_song(song_.song, now_ns, scratch_, playback_tables_, fingerprint);
    enqueue_reactive(scratch_, now_ns, true);
    sound_cursor_.reset();
    audition_pb_.drop_song(now_ns, scratch_);
    audition_stale_ = true;
    enqueue_plain(scratch_);
    if (ahead) {
        prepared_fingerprint_ = fingerprint;
        window_ = std::move(prepared.window);
        densest_window_ = prepared.densest_window;
        prepared_ = &prepared;
    } else {
        window_.build(song_.song);
        densest_window_ = etude::densest_window(song_.song, options_.lookahead_s);
    }
    read_hands_scope(true);
    rebuild_evaluators(now_ns);
    prepared_ = nullptr;
    apply_mutes(now_ns);
    restart_here(now_ns); // the evaluators start fresh at the playhead, and the sound with it
}

void SongController::set_song_sound(SongSound sound, std::int64_t now_ns) {
    pump(now_ns);
    sound_ = sound;
    apply_mutes(now_ns);
}

void SongController::cycle_song_sound(std::int64_t now_ns) {
    switch (song_sound()) {
    case SongSound::Mute:
        // "The other hand" needs two hands with notes.
        set_song_sound(present_.both() ? SongSound::OtherHand : SongSound::BothHands, now_ns);
        break;
    case SongSound::OtherHand:
        set_song_sound(SongSound::BothHands, now_ns);
        break;
    case SongSound::BothHands:
        set_song_sound(SongSound::Mute, now_ns);
        break;
    }
}

void SongController::set_mode(PracticeMode mode, std::int64_t now_ns) {
    if (mode == PracticeMode::FreePlay) {
        mode = PracticeMode::Flow; // with a song, the choice is Flow or Wait
    }
    if (mode == mode_) {
        return;
    }
    pump(now_ns);
    finish_run(now_ns);
    mode_ = mode;
    grades_.clear();
    wrongs_.clear();
    restart_here(now_ns); // Wait Mode finds the chord to wait for; Flow lets go of a hold
}

void SongController::toggle_mode(std::int64_t now_ns) {
    set_mode(mode_ == PracticeMode::Wait ? PracticeMode::Flow : PracticeMode::Wait, now_ns);
}

void SongController::set_transpose(int semitones, std::int64_t now_ns) {
    semitones_ = std::clamp(semitones, -kMaxTransposeSemitones, kMaxTransposeSemitones);
    pump(now_ns);
    wanted_ = Transposition(semitones_, keyboard_);
    if (tables_inline()) {
        source_.set(wanted_, now_ns);
        pump(now_ns);
    } else {
        update_tables(now_ns); // source_ takes it when the worker's tables are in
    }
    watch_run_setup();
}

void SongController::player_set_transpose(int semitones, std::int64_t now_ns) {
    offer_.player_changed();
    set_transpose(semitones, now_ns);
}

void SongController::set_keyboard(KeyRange keyboard, std::int64_t now_ns) {
    if (keyboard.lowest == keyboard_.lowest && keyboard.highest == keyboard_.highest) {
        return;
    }
    keyboard_ = keyboard;
    fits_valid_ = false; // the offer is judged again on the new keyboard
    set_transpose(semitones_, now_ns);
}

void SongController::set_options(const SongOptions& wanted, std::int64_t now_ns, bool settling) {
    SongOptions options = wanted;
    if (settling) {
        options.hit_window_ms = options_.hit_window_ms;
        options.latency_ms = options_.latency_ms;
        options.input_delay_ms = options_.input_delay_ms;
    }
    if (options == options_) {
        return;
    }
    const bool grading = options.hit_window_ms != options_.hit_window_ms ||
                         options.latency_ms != options_.latency_ms ||
                         options.input_delay_ms != options_.input_delay_ms;
    const bool lookahead = options.lookahead_s != options_.lookahead_s;
    const bool clicks = options.metronome != options_.metronome ||
                        options.click_voice != options_.click_voice ||
                        options.click_volume != options_.click_volume ||
                        options.metronome_grid != options_.metronome_grid;
    const bool volume = options.accompaniment_volume != options_.accompaniment_volume;
    options_ = options;
    if (lookahead) {
        // Only the "not wrong" reach of a press: no run ends, nothing is copied.
        flow_.set_lookahead(options_.lookahead_s);
        wait_.set_lookahead(options_.lookahead_s, now_ns);
        watch_run_setup(); // the run goes on, but is no longer one setting's worth
    }
    if (volume) {
        // Notes handed over carry the old volume: choose them again.
        if (loaded_ && sound_cursor_) {
            restart_sound(now_ns, transport_.position_s());
        }
        playback_.set_accompaniment_volume(options_.accompaniment_volume, now_ns, scratch_);
        enqueue_reactive(scratch_, now_ns, false);
    }
    if (clicks) {
        playback_.set_click_sound(click_sound_for(options_.click_voice, options_.click_volume));
        ++click_version_; // what was handed over rests on the old settings: plan it again
    }
    if (grading && loaded_) {
        pump(now_ns);
        finish_run(now_ns);
        rebuild_evaluators(now_ns, true);
        restart_here(now_ns);
    }
}

} // namespace etude::app
