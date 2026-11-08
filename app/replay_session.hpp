// SPDX-License-Identifier: GPL-3.0-or-later
// A run's review and its replay, on the frame thread and without SDL: the summary the history
// thread builds for the run the results card is about (asked for when the card appears, taken when
// it is in), and the replay view the player opens from it (a `ReplayPlayer` and the player's own
// sound). The answer of a read comes back some frames after it was asked for and is taken only if
// it is the latest request's and about the song that is open now (core `ReplayRequestGate`).
// Nothing here touches the song's transport: a replay has a clock of its own, so leaving it
// returns to where the song was.
#pragma once

#include "history_store.hpp"

#include "etude/evaluator.hpp"
#include "etude/replay_keys.hpp"
#include "etude/replay_marks.hpp"
#include "etude/replay_request.hpp"
#include "etude/replay_sound.hpp"
#include "etude/run_replay.hpp"
#include "etude/run_summary.hpp"

#include <cstdint>
#include <memory>
#include <utility>
#include <span>
#include <vector>

namespace etude::app {

class ReplaySession {
  public:
    enum class Review : std::uint8_t {
        None,    // no run to review
        Loading, // asked for, not in
        Ready,   // in: `data()` holds the summary, and the timeline when the notes were kept
    };

    // ---- The run's review

    // Asks the history thread for the replay of the run `record` describes, laid over the song's
    // tables. Any earlier request is forgotten (its answer will be dropped).
    void ask(HistoryStore& history, const RunRecord& record, const FlowEvaluator::NoteTable& notes,
             const FlowEvaluator::MeasureTable& measures);
    // A review made by hand (the developer's demo runs): ready at once.
    // `run_id` is the run's database id for the pin (0: it cannot be pinned).
    void adopt(const RunRecord& record, std::shared_ptr<const RunReplay> replay,
               std::int64_t run_id = 0);
    // Takes the answers `history` has: the one that applies is the review. A run whose notes were
    // not kept gets the summary of its record. `open_song_hash` is the song open now. True when the
    // review changed.
    bool poll(HistoryStore& history, std::uint64_t open_song_hash);
    // The run is not wanted any more (its card went, another song opened). The view, if up, stays.
    void forget_review();

    Review review() const { return review_; }
    const RunReplay* data() const { return data_.get(); }
    const RunRecord& record() const { return record_; }
    // There are played notes to watch.
    bool watchable() const { return data_ != nullptr && !data_->timeline.empty(); }

    // ---- Keeping the run's notes

    // The run's replay can be pinned: its notes are kept (the history thread found the run and it
    // has notes to watch). A pinned run is never pruned to make room.
    bool pin_available() const { return review_ == Review::Ready && run_id_ != 0 && watchable(); }
    // The pin as the history thread last confirmed it, and a change waiting for its answer.
    bool pinned() const { return pinned_; }
    bool pin_pending() const { return pin_serial_ != 0; }
    // Asks the history thread to pin the run, or to let go of the pin; nothing when it cannot be
    // pinned or an answer is awaited. The state changes only when the answer says it took.
    void toggle_pin(HistoryStore& history);
    // A pin or an unpin the history thread refused since the last call (the notes were gone):
    // the app says so.
    bool take_pin_refused() { return std::exchange(pin_refused_, false); }

    // ---- The replay view

    // Opens the view at `at_ms` of the replay, paused; false when there is nothing to watch.
    bool open_view(std::int32_t at_ms = 0);
    void close_view();
    bool viewing() const { return viewing_; }

    void play_pause();
    void restart();
    void skip(double seconds);
    void seek_ms(std::int32_t ms);
    void seek_fraction(double fraction);
    void next_speed();
    void set_sound(bool on);
    void toggle_sound() { set_sound(!sound_.on()); }
    // Jumps to the next (`direction` > 0) or previous mistake; false when there is none that way.
    bool jump_mistake(int direction);

    // Moves the replay on by `dt_s` of wall time and passes the notes that came due to the sound.
    void update(double dt_s);
    // The note-ons and note-offs of the player's own sound since the last call.
    std::span<const ReplaySoundEvent> take_sound() { return sound_.take(); }

    const ReplayTimeline& timeline() const { return data_->timeline; }
    std::int32_t position_ms() const { return player_.position_ms(); }
    bool playing() const { return player_.playing(); }
    int speed_percent() const { return player_.speed_percent(); }
    bool sound_on() const { return sound_.on(); }
    // The keys the replay has lit now: pressed notes in their hand, wrong keys marked, none after a
    // seek, the end, the view closing; where a paused replay stands, the keys mid-note.
    const ReplayKeys::Lit& lit_keys() const { return keys_.lit(); }
    int lit_count() const { return keys_.lit_count(); }
    // The bars to draw over the falling notes now (cleared first).
    void marks(double ahead_s, double behind_s, std::vector<ReplayBarMark>& out) const;

  private:
    ReplayRequestGate gate_;
    Review review_ = Review::None;
    std::shared_ptr<const RunReplay> data_;
    RunRecord record_;
    std::int64_t run_id_ = 0; // the database's id of the run, from the replay's answer; 0: unknown
    bool pinned_ = false;
    bool pin_target_ = false;
    std::uint64_t pin_serial_ = 0; // the request awaiting its answer, 0 for none
    bool pin_refused_ = false;

    bool viewing_ = false;
    ReplayPlayer player_;
    ReplaySound sound_;
    ReplayKeys keys_;
    bool was_playing_ = false;
};

} // namespace etude::app
