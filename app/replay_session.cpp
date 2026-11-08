// SPDX-License-Identifier: GPL-3.0-or-later
#include "replay_session.hpp"

#include <utility>

namespace etude::app {

void ReplaySession::ask(HistoryStore& history, const RunRecord& record,
                        const FlowEvaluator::NoteTable& notes,
                        const FlowEvaluator::MeasureTable& measures) {
    forget_review();
    const std::uint64_t serial = history.request_replay(0, ReplayRequest{record, notes, measures});
    if (serial == 0) {
        return; // the store takes no more work, or there are no tables: no review
    }
    gate_.begin(serial, record.song_hash);
    record_ = record;
    review_ = Review::Loading;
}

void ReplaySession::adopt(const RunRecord& record, std::shared_ptr<const RunReplay> replay,
                          std::int64_t run_id) {
    forget_review();
    run_id_ = run_id;
    record_ = record;
    data_ = std::move(replay);
    review_ = data_ ? Review::Ready : Review::None;
}

bool ReplaySession::poll(HistoryStore& history, std::uint64_t open_song_hash) {
    bool changed = false;
    for (EventsRead& answer : history.take_event_reads()) {
        if (answer.kind == EventsReadKind::Pin && pin_serial_ != 0 && answer.serial == pin_serial_) {
            // The answer to the pin or the unpin asked for: the pin is as asked only if it took.
            pin_serial_ = 0;
            if (answer.pin_took) {
                pinned_ = pin_target_;
            } else {
                pin_refused_ = true;
            }
            changed = true;
            continue;
        }
        if (answer.kind != EventsReadKind::Replay ||
            !gate_.accepts(answer.serial, answer.song_hash, open_song_hash)) {
            continue; // an answer to another request, or about another run or song
        }
        gate_.cancel();
        // The history thread built the replay, or the summary of the record when no notes are kept;
        // no answer at all (the store closing) leaves an empty one: nothing to show, nothing to do.
        data_ = answer.replay != nullptr ? std::move(answer.replay)
                                         : std::make_shared<const RunReplay>();
        run_id_ = answer.run_id;
        pinned_ = answer.pinned;
        review_ = Review::Ready;
        changed = true;
    }
    return changed;
}

void ReplaySession::toggle_pin(HistoryStore& history) {
    if (!pin_available() || pin_pending()) {
        return;
    }
    const bool target = !pinned_;
    const std::uint64_t serial = history.set_run_pinned(run_id_, target);
    if (serial != 0) {
        pin_serial_ = serial;
        pin_target_ = target;
    }
}

void ReplaySession::forget_review() {
    gate_.cancel();
    review_ = Review::None;
    run_id_ = 0;
    pinned_ = false;
    pin_serial_ = 0; // an answer still to come is dropped: it names no run of ours
    pin_refused_ = false;
    // The view keeps what it plays: its timeline is kept alive here until it is closed.
    if (!viewing_) {
        data_.reset();
    }
}

bool ReplaySession::open_view(std::int32_t at_ms) {
    if (!watchable()) {
        return false;
    }
    player_.open(data_->timeline);
    player_.seek_ms(data_->timeline, at_ms);
    player_.take_seeked();
    // What the last view left sounding is let go (its note-offs are taken by the app with the next
    // frame's): none are thrown away here, so a close and a reopen in one frame leave no stuck note.
    sound_.silence();
    keys_.seat(data_->timeline, player_.position_ms());
    was_playing_ = false;
    viewing_ = true;
    return true;
}

void ReplaySession::close_view() {
    if (!viewing_) {
        return;
    }
    player_.pause();
    sound_.silence(); // the note-offs are still taken by the app once
    keys_.clear();
    viewing_ = false;
    if (review_ == Review::None) {
        data_.reset();
    }
}

void ReplaySession::play_pause() {
    if (!viewing_) {
        return;
    }
    if (player_.playing()) {
        player_.pause();
    } else {
        player_.play(data_->timeline);
    }
}

void ReplaySession::restart() {
    if (viewing_) {
        player_.seek_ms(data_->timeline, 0);
    }
}

void ReplaySession::skip(double seconds) {
    if (viewing_) {
        seek_ms(player_.position_ms() + static_cast<std::int32_t>(seconds * 1000.0));
    }
}

void ReplaySession::seek_ms(std::int32_t ms) {
    if (viewing_) {
        player_.seek_ms(data_->timeline, ms);
    }
}

void ReplaySession::seek_fraction(double fraction) {
    if (viewing_) {
        player_.seek_fraction(data_->timeline, fraction);
    }
}

void ReplaySession::next_speed() {
    if (viewing_) {
        player_.set_speed_percent(
            next_replay_speed(player_.speed_percent(), data_->timeline.setup.speed_percent));
    }
}

void ReplaySession::set_sound(bool on) { sound_.set_on(on); }

bool ReplaySession::jump_mistake(int direction) {
    if (!viewing_) {
        return false;
    }
    const std::optional<std::int32_t> to =
        mistake_position(data_->timeline, player_.position_ms(), direction);
    if (!to) {
        return false;
    }
    player_.seek_ms(data_->timeline, *to);
    return true;
}

void ReplaySession::update(double dt_s) {
    if (!viewing_) {
        return;
    }
    const ReplayTimeline& timeline = data_->timeline;
    const std::span<const ReplayNote> passed = player_.advance(timeline, dt_s);
    const std::int32_t position = player_.position_ms();
    if (player_.take_seeked()) {
        // A jump: what sounds lets go and the keys are those mid-note where the replay now stands.
        sound_.silence();
        keys_.seat(timeline, position);
    } else if (was_playing_ && !player_.playing()) {
        // A pause or the end: what sounds lets go; the keys stay lit where a pause stands, none
        // after the end.
        sound_.silence();
        if (position >= timeline.duration_ms) {
            keys_.clear();
        }
    }
    sound_.step(passed, position);
    keys_.step(passed, position);
    was_playing_ = player_.playing();
}

void ReplaySession::marks(double ahead_s, double behind_s, std::vector<ReplayBarMark>& out) const {
    out.clear();
    if (viewing_) {
        replay_bar_marks(data_->timeline, static_cast<double>(player_.position_ms()), ahead_s,
                         behind_s, out);
    }
}

} // namespace etude::app
