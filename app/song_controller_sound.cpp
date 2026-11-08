// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_controller.hpp"

#include "hand_split_job.hpp"
#include "lent_song.hpp"
#include "practice_tables_job.hpp"

#include "etude/look_ahead.hpp"
#include "etude/section_editing.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

namespace etude::app {

void SongController::enqueue_timeline(std::vector<PlaybackMessage>& msgs) {
    for (PlaybackMessage m : msgs) {
        // Never due before a message already queued: the events are in Playback's order, so a
        // note-off cannot overtake its note-on. Its time otherwise stays the note's own.
        m.at_ns = std::max(m.at_ns, last_due_ns_);
        last_due_ns_ = m.at_ns;
        outbox_.push_back({SongSoundEvent::Kind::Message, m});
    }
    msgs.clear();
}

void SongController::enqueue_reactive(std::vector<PlaybackMessage>& msgs, std::int64_t now_ns,
                                      bool cancel) {
    if (!cancel && msgs.empty()) {
        return;
    }
    PlaybackMessage at;
    at.at_ns = now_ns;
    outbox_.push_back({SongSoundEvent::Kind::CancelUnstarted, at});
    // What is left queued is due at or before now_ns, so these cannot overtake it.
    last_due_ns_ = now_ns;
    for (PlaybackMessage m : msgs) {
        m.at_ns = std::max(m.at_ns, last_due_ns_);
        last_due_ns_ = m.at_ns;
        outbox_.push_back({SongSoundEvent::Kind::Message, m});
    }
    msgs.clear();
}

void SongController::enqueue_plain(std::vector<PlaybackMessage>& msgs) {
    for (const PlaybackMessage& m : msgs) {
        outbox_.push_back({SongSoundEvent::Kind::Message, m});
    }
    msgs.clear();
}

void SongController::pump(std::int64_t now_ns) {
    // The events stamped before what the clock and the ops are about to change go in first.
    if (event_pull_ && !pulling_) {
        pulling_ = true;
        event_pull_();
        pulling_ = false;
    }
    transport_.update(now_ns);
    transport_.take_ops(ops_);
    for (const TransportOp& op : ops_) {
        flow_.apply(op);
        wait_.apply(op);
        if (op.kind == TransportOp::Kind::Play) {
            start_run_if_needed(op.at_ns);
        }
        // Nothing is graded while watching, so a wrap is the only sign of a pass.
        if (op.kind == TransportOp::Kind::Wrap && mode_ == PracticeMode::FreePlay) {
            loop_passes_.push_back({false, op.at_ns});
        }
        sound_op(op);
    }
    ops_.clear();
    schedule_sound(now_ns);
}

// The sound follows the transport's commands, not its Moves: the notes were handed over ahead of
// the clock, from the cursor (schedule_sound).
void SongController::sound_op(const TransportOp& op) {
    using Kind = TransportOp::Kind;
    switch (op.kind) {
    case Kind::Play:
        playback_.apply(op, scratch_);
        enqueue_reactive(scratch_, op.at_ns, false);
        sound_cursor_ = op.to_s;
        count_in_next_ = 0;
        note_plan();
        break;
    case Kind::Pause:
    case Kind::Seek:
        playback_.apply(op, scratch_);
        enqueue_reactive(scratch_, op.at_ns, true);
        if (playback_.running()) {
            sound_cursor_ = op.to_s;
            note_plan();
        } else {
            sound_cursor_.reset();
        }
        break;
    case Kind::End:
        // The notes before the end that were not handed over yet go late; then everything stops.
        catch_up(op.at_ns, op.to_s);
        playback_.apply(op, scratch_);
        enqueue_reactive(scratch_, op.at_ns, true);
        sound_cursor_.reset();
        break;
    case Kind::Hold:
        // Wait Mode stops the clock. Notes handed over past the stop are cancelled; a stop at an
        // armed point has nothing past it.
        if (sound_cursor_ && *sound_cursor_ > op.to_s + 1e-9) {
            restart_sound(op.at_ns, op.to_s);
        } else if (sound_cursor_) {
            catch_up(op.at_ns, op.to_s);
        }
        break;
    case Kind::Click: // the clicks are planned ahead with the notes, not made by the clock
    case Kind::Release:
    case Kind::Wrap: // planned with the notes (plan_look_ahead)
    case Kind::Move:
        break;
    }
}

void SongController::note_plan() {
    plan_.speed_percent = transport_.speed_percent();
    plan_.loop_on = transport_.loop_enabled();
    plan_.loop_start_s = transport_.loop_start_s();
    plan_.loop_end_s = transport_.loop_end_s();
    plan_.transposition = source_.version();
    plan_.clicks = click_version_;
    planned_wraps_ = transport_.loop_count();
}

void SongController::schedule_sound(std::int64_t now_ns) {
    if (!loaded_ || !sound_cursor_ || !playback_.running()) {
        return;
    }
    const TransportState state = transport_.state();
    if (state != TransportState::Playing && state != TransportState::Held &&
        state != TransportState::CountIn) {
        return;
    }
    const double position = transport_.position_s();
    const std::optional<double> hold = transport_.hold_point();
    const bool changed = plan_.speed_percent != transport_.speed_percent() ||
                         plan_.loop_on != transport_.loop_enabled() ||
                         plan_.loop_start_s != transport_.loop_start_s() ||
                         plan_.loop_end_s != transport_.loop_end_s() ||
                         plan_.transposition != source_.version() ||
                         plan_.clicks != click_version_ ||
                         (hold && *hold > position && *sound_cursor_ > *hold + 1e-9);
    if (changed) {
        restart_sound(now_ns, position);
    }
    if (state == TransportState::CountIn) {
        click_ops_.clear();
        plan_count_in(count_in_input(), now_ns, options_.sound_lead_ns, options_.metronome_grid,
                      count_in_next_, click_ops_);
        for (const TransportOp& op : click_ops_) {
            playback_.apply(op, scratch_);
            enqueue_plain(scratch_);
        }
    }
    LookAheadInput in;
    in.position_s = position;
    in.speed_percent = transport_.speed_percent();
    // The notes are handed over ahead of the count-in too, due when it ends; held at the first
    // chord (Wait Mode) nothing moves.
    in.moving = state == TransportState::Playing ||
                (state == TransportState::CountIn && !transport_.held());
    if (state == TransportState::CountIn) {
        in.delay_ns = std::llround(transport_.count_in_wall_left_s() * 1e9);
    }
    in.duration_s = transport_.duration_s();
    in.loop_on = transport_.loop_enabled();
    in.loop_start_s = transport_.loop_start_s();
    in.loop_end_s = transport_.loop_end_s();
    in.wraps_ahead = transport_.loop_count() - planned_wraps_;
    in.hold_at_s = hold;
    plan_ops_.clear();
    const LookAheadPlan plan =
        plan_look_ahead(in, now_ns, options_.sound_lead_ns, *sound_cursor_, plan_ops_);
    planned_wraps_ += plan.wraps_planned;
    hand_over_ops();
}

void SongController::hand_over_ops() {
    for (const TransportOp& op : plan_ops_) {
        playback_.apply(op, scratch_);
        enqueue_timeline(scratch_);
        if (options_.metronome && op.kind == TransportOp::Kind::Move) {
            // Clicks are one-shot percussion with times of their own: they go in as they are due,
            // never held back behind a note's note-off.
            click_ops_.clear();
            grid_.clicks(op, options_.metronome_grid, click_ops_);
            for (const TransportOp& click : click_ops_) {
                playback_.apply(click, scratch_);
                enqueue_plain(scratch_);
            }
        }
    }
}

void SongController::catch_up(std::int64_t at_ns, double position_s) {
    if (!sound_cursor_) {
        return;
    }
    LookAheadInput in;
    in.position_s = position_s;
    in.speed_percent = transport_.speed_percent();
    in.moving = false;
    in.duration_s = transport_.duration_s();
    plan_ops_.clear();
    plan_look_ahead(in, at_ns, 0, *sound_cursor_, plan_ops_);
    hand_over_ops();
}

void SongController::restart_sound(std::int64_t at_ns, double position_s) {
    PlaybackMessage at;
    at.at_ns = at_ns;
    outbox_.push_back({SongSoundEvent::Kind::CancelUnstarted, at});
    last_due_ns_ = at_ns;
    playback_.rewind(position_s);
    // A new transposition stops the notes at their old keys now, which are the ones that started.
    playback_.set_transposition(source_.value(), at_ns, scratch_);
    enqueue_reactive(scratch_, at_ns, false);
    sound_cursor_ = position_s;
    if (transport_.counting_in()) {
        count_in_next_ = first_unsounded_count_in_beat(count_in_input());
    }
    note_plan();
}

CountInInput SongController::count_in_input() const {
    CountInInput in;
    in.total_beats = transport_.count_in_total();
    in.beats_left = transport_.count_in_beats_left();
    in.beat_s = transport_.count_in_beat_seconds();
    in.numerator = transport_.count_in_numerator();
    in.speed_percent = transport_.speed_percent();
    in.start_s = transport_.count_in_start_s();
    return in;
}

void SongController::pump_audition(std::int64_t now_ns) {
    if (!auditioning_) {
        return;
    }
    audition_tp_.update(now_ns);
    audition_tp_.take_ops(audition_ops_);
    for (const TransportOp& op : audition_ops_) {
        audition_pb_.apply(op, scratch_);
    }
    audition_ops_.clear();
    enqueue_plain(scratch_);
    const bool ended = audition_tp_.ended() || audition_tp_.position_s() >= audition_end_s_ - 1e-9;
    if (!audition_repeating_) {
        if (ended) {
            stop_audition(now_ns);
        }
        return;
    }
    const bool loop_on =
        transport_.loop_enabled() && transport_.loop_end_s() > transport_.loop_start_s();
    switch (audition_repeat(true, ended, loop_on)) {
    case RepeatStep::Continue:
        break;
    case RepeatStep::Again:
        start_audition(audition_repeat_start_s_, audition_repeat_end_s_, now_ns);
        audition_repeating_ = auditioning_;
        break;
    case RepeatStep::Done:
        stop_audition(now_ns);
        break;
    }
}

void SongController::audition(double start_s, double end_s, std::int64_t now_ns) {
    start_audition(start_s, end_s, now_ns);
}

void SongController::audition_repeating(double start_s, double end_s, std::int64_t now_ns) {
    start_audition(start_s, end_s, now_ns);
    audition_repeating_ = auditioning_;
    audition_repeat_start_s_ = start_s;
    audition_repeat_end_s_ = end_s;
}

void SongController::start_audition(double start_s, double end_s, std::int64_t now_ns) {
    if (!loaded_ || !(end_s > start_s)) {
        return;
    }
    stop_audition(now_ns);
    if (audition_stale_) {
        // The audition's playback takes the song's tables (shared with the main one) now, not at
        // every install: a pointer copy, nothing to build.
        audition_pb_.set_song(song_.song, now_ns, scratch_, playback_tables_, playback_fingerprint_);
        audition_stale_ = false;
        enqueue_plain(scratch_);
    }
    pump(now_ns);
    if (transport_.playing()) {
        transport_.pause(now_ns); // one voice at a time
        pump(now_ns);
    }
    audition_tp_.update(now_ns);
    audition_tp_.seek(now_ns, start_s);
    audition_tp_.play(now_ns);
    audition_end_s_ = std::min(end_s, song_.song.duration_s);
    auditioning_ = true;
    audition_tp_.take_ops(audition_ops_);
    for (const TransportOp& op : audition_ops_) {
        audition_pb_.apply(op, scratch_);
    }
    audition_ops_.clear();
    enqueue_plain(scratch_);
}

void SongController::stop_audition(std::int64_t now_ns) {
    if (!auditioning_) {
        return;
    }
    auditioning_ = false;
    audition_repeating_ = false;
    audition_tp_.update(now_ns);
    audition_tp_.pause(now_ns);
    audition_tp_.take_ops(audition_ops_);
    for (const TransportOp& op : audition_ops_) {
        audition_pb_.apply(op, scratch_);
    }
    audition_ops_.clear();
    audition_pb_.silence(now_ns, scratch_);
    enqueue_plain(scratch_);
}

void SongController::apply_mutes(std::int64_t now_ns) {
    bool left = true;
    bool right = true;
    bool unknown = true;
    bool drums = true;
    const HandFilter hands = applied_hands_; // changes with the notes and the grading, on the swap
    switch (song_sound()) {
    case SongSound::Mute:
        break;
    case SongSound::BothHands:
        left = right = unknown = drums = false;
        break;
    case SongSound::OtherHand:
        // The hand not practised sounds. The evaluator grades notes with no hand only when both
        // hands are practised (they are the player's then), so they stay muted exactly then and
        // sound otherwise. Drums are never the player's (a piano has none): they always sound.
        left = hands != HandFilter::Right;
        right = hands != HandFilter::Left;
        unknown = hands == HandFilter::Both;
        drums = false;
        break;
    }
    const bool same =
        playback_.hand_muted(Hand::Left) == left && playback_.hand_muted(Hand::Right) == right &&
        playback_.hand_muted(Hand::Unknown) == unknown && playback_.drums_muted() == drums;
    if (same) {
        return;
    }
    // Notes handed over were chosen under the old mutes: cancel them and choose again.
    if (sound_cursor_) {
        restart_sound(now_ns, transport_.position_s());
    }
    playback_.set_hand_muted(Hand::Left, left, now_ns, scratch_);
    playback_.set_hand_muted(Hand::Right, right, now_ns, scratch_);
    playback_.set_hand_muted(Hand::Unknown, unknown, now_ns, scratch_);
    playback_.set_drums_muted(drums, now_ns, scratch_);
    enqueue_reactive(scratch_, now_ns, false);
}

} // namespace etude::app
