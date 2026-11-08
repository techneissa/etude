// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/evaluator.hpp"
#include "etude/hand.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace etude {

// What the player played in one run, note by note, so a run can be compared with another (the ghost
// of the best run, a replay) and not only summed up in a RunRecord.
//
// One event per
// - press that graded a note (Press): the key, how hard, the song time it was played at, the note
// it
//   matched with its hand, its grade and the signed timing error. A press that struck a unison
//   (two notes on one key within kUnisonSeconds) graded both, so it appears once per note;
// - press that matched no note (Wrong): a wrong note. More than kMaxKeptWrongPresses decided in
//   one frame are not kept by the evaluator: they are counted in `dropped`. Presses the evaluator excused (a key pressed
//   early for a note still to come) or ignored (before play, while paused) are neither wrong nor
//   graded, and are not recorded;
// - note nobody played (Missed): the key it wanted, no velocity, the note's own start as its time.
// Notes the evaluator could not judge (the microphone heard something but not what) and notes no
// key can play have no result and no event.
//
// Times are milliseconds. `song_ms` is where the song clock was, not wall time, so two runs at
// different speeds line up on the same song. `error_ms` is wall time, as the grade is: negative is
// early.
enum class RunEventKind : std::uint8_t { Press, Wrong, Missed };

struct RunEvent {
    std::int32_t song_ms = 0;
    std::int32_t error_ms = 0;    // Press only; 0 otherwise
    std::int32_t note_index = -1; // into Song::notes; -1 for a Wrong press
    RunEventKind kind = RunEventKind::Press;
    std::uint8_t key = 0;      // 0..127; a Missed note's is the key it wanted (transposed)
    std::uint8_t velocity = 0; // 1..127; 0 for a Missed note
    Hand hand = Hand::Unknown; // of the note matched; Unknown for a Wrong press
    NoteGrade grade = NoteGrade::Missed; // Press: its grade; Missed: Missed; Wrong: unused

    friend bool operator==(const RunEvent&, const RunEvent&) = default;
};

// A run's events, in song-time order (ties by note, then key).
struct RunEvents {
    std::vector<RunEvent> events;
    // How many more events the run made than the recorder could hold. They are lost, not
    // summarised; the run's RunRecord still counts them.
    std::int32_t dropped = 0;
    // The notes the run was expected to grade (the song's practised notes, once each).
    std::int32_t expected_notes = 0;
    // True when every practised note has exactly one Press or Missed event and nothing was
    // dropped: one pass over the whole song from the start. A looped section, a run started
    // mid-song or one that jumped back is not.
    bool complete = false;
    // Set by RunEventRecorder::hand_over(): the events are as they settled, not yet in song-time
    // order, and `complete` is not decided. finish_ordering() does both, so the sort of a run's
    // thousands of events is not paid by the frame that ends the run: the history writer calls it.
    bool unordered = false;
    // The run's speed, key or look-ahead changed during it: finish_ordering() never calls it
    // complete.
    bool setup_changed = false;

    friend bool operator==(const RunEvents&, const RunEvents&) = default;
};

// Puts a hand_over() run in order and decides `complete`; does nothing for a run that is ordered
// already (finish(), a stored run). Any thread, on the run alone.
void finish_ordering(RunEvents& run);

// The most events one run keeps. Past it the recorder stops: later events are counted in
// `dropped` and the run is never complete (so never a ghost). 16,384 is five times the notes of the
// longest pieces in the song library and a loop of a few bars played a thousand times; a run past
// it is a practice session, not a performance.
inline constexpr std::size_t kRunEventCapacity = 16'384;

// How many of a song's newest runs keep their events by default, besides the pinned runs; older
// runs keep their summary only (the setting `run_events_kept`, 0 to kMaxRunEventsKept). A run is
// a few thousand events at most, so twenty of them are a megabyte or two a song.
inline constexpr int kDefaultRunEventsKept = 20;
inline constexpr int kMaxRunEventsKept = 200;

// Fills a fixed buffer from the results of a run, one call per result as the evaluator settles it.
// Nothing allocates after the constructor, so it is safe to feed from any frame. finish() copies
// the run out (one allocation, at the end of the run) and empties the buffer for the next.
class RunEventRecorder {
  public:
    explicit RunEventRecorder(std::size_t capacity = kRunEventCapacity);

    // A graded note: its press, or Missed when nobody pressed.
    void add(const NoteResult& result);
    // A press that matched nothing.
    void add_wrong(const WrongPress& press);
    // `count` events the recorder was never handed (wrong presses past the evaluator's per-frame
    // keep): counted as dropped, so the run is not complete and the record never silently
    // disagrees with the summary.
    void add_dropped(int count);

    std::size_t size() const { return events_.size(); }
    std::size_t capacity() const { return capacity_; }
    std::int32_t dropped() const { return dropped_; }

    // Ends the run: sorts the events into song-time order, decides `complete` against the number of
    // notes the run should have graded, hands everything over and starts empty.
    RunEvents finish(std::size_t practised_notes);
    // Ends the run without the sort: the events as they settled, `unordered`, for finish_ordering()
    // on another thread (the frame thread pays one copy, not two sorts). Starts empty.
    RunEvents hand_over(std::size_t practised_notes);
    // Forgets the run (it was not an attempt, or the song changed under it).
    void clear();

  private:
    void put(const RunEvent& event);

    std::vector<RunEvent> events_; // reserved to capacity_ once, never grown
    std::size_t capacity_ = 0;
    std::int32_t dropped_ = 0;
};

// The event for one result or press, with every field in range. Exposed for tests and the store.
RunEvent make_run_event(const NoteResult& result);
RunEvent make_run_event(const WrongPress& press);

// The event with every field forced into its range (a database row cannot make a bad one).
RunEvent sanitized(const RunEvent& event);

} // namespace etude
