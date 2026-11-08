// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/ghost_run.hpp"
#include "etude/grade_palette.hpp"
#include "etude/run_events.hpp"
#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace etude {

// The replay of a run: what the player played, note by note, laid over the song's own notes.
// Pure values built once, from a run's stored events and the song's expected notes (never per
// frame), and a small player that walks them at any speed. The screen draws the song's falling
// notes as usual and the timeline's notes over them; the player's own sound is the app's, fed from
// the notes the player passes.
//
// The stored events hold one press time and no release, so a played note has a start and no end of
// its own: `replay_length_ms` gives the length it is drawn with (the expected note's, else a short
// default for a wrong press).

// What happened to one note, in the grade colours' categories. Played is Wait Mode's grade (the
// note was played; Wait Mode has no timing grade) and takes Perfect's green, as its "OK" does.
enum class ReplayMark : std::uint8_t { Perfect, Good, Early, Late, Missed, Wrong, Played };

// The grade colour a mark is drawn in; the word `grade_name` gives goes beside it.
Grade replay_grade(ReplayMark mark);

// One played note (or a missed one, or a wrong press).
struct ReplayNote {
    // Where the song clock was: at the press, or at the note's own start for a missed note.
    std::int32_t time_ms = 0;
    // The press less the matched note's start, in song time (negative is early). 0 for a missed
    // note, a wrong press, and a press whose note the song does not have. It is the song-time
    // distance the screen draws between the played block and the falling note; `error_ms` is the
    // wall-time error the grade was decided on, so at 50% speed the offset is half of it.
    std::int32_t offset_ms = 0;
    std::int32_t error_ms = 0;    // as stored: wall time, Flow Mode presses only
    std::int32_t note_index = -1; // into Song::notes; -1 for a wrong press
    // The matched note's start and end in song time; -1 when there is none (a wrong press, or a
    // note the song does not have: a stored run older than the song's parse).
    std::int32_t expected_start_ms = -1;
    std::int32_t expected_end_ms = -1;
    std::int32_t measure = 1; // the bar it belongs to (the note's, else the bar at `time_ms`)
    std::uint8_t key = 0;     // the key pressed (a missed note: the key it wanted), transposed
    std::uint8_t velocity = 0; // 1..127; 0 for a missed note
    Hand hand = Hand::Unknown; // of the note matched; Unknown for a wrong press
    ReplayMark mark = ReplayMark::Missed;

    bool has_note() const { return expected_start_ms >= 0; }
    friend bool operator==(const ReplayNote&, const ReplayNote&) = default;
};

// How long a played block is drawn when no note says: a wrong press.
inline constexpr std::int32_t kReplayWrongLengthMs = 200;
// The length a played note is drawn with: the matched note's, at least `kMinReplayLengthMs`, else
// a wrong press's default. A missed note draws as the note it was.
inline constexpr std::int32_t kMinReplayLengthMs = 40;
std::int32_t replay_length_ms(const ReplayNote& note);

struct ReplayTimeline {
    std::vector<ReplayNote> notes; // by time_ms, ties by note then key
    // Where the replay ends: the end of the last note drawn, else 0.
    std::int32_t duration_ms = 0;
    // The latest song time any event happened at. A run stopped half way stops here.
    std::int32_t reached_ms = 0;
    // The longest bar in `notes` (replay_length_ms), so a view can look back for bars that began
    // before it and still reach into it.
    std::int32_t max_length_ms = 0;
    // The song time of every mistake (a note nobody played, a wrong key), ascending: built with the
    // timeline so the frame finds the next or previous one by binary search, not by a scan.
    std::vector<std::int32_t> mistakes;

    // The run's setup, kept so the screen can say what it replays and open it at the run's speed.
    GhostSetup setup;
    std::int32_t expected_notes = 0; // the notes the run was to grade, as stored (0: not stored)
    std::int32_t graded_notes = 0;   // distinct notes with a press or a miss
    std::int32_t dropped = 0;        // events the recorder could not hold
    std::int32_t unmatched = 0;      // events naming a note the song does not have
    bool complete = false;           // one pass over every practised note, as stored
    // A note was judged more than once: a loop, or a run that jumped back. Its passes are
    // merged on the song clock; the events keep no pass number.
    bool looped = false;
    // The run ended before it covered the notes it was to grade (stopped, or the song changed
    // under it): not complete, and either fewer notes judged than expected, or, in a loop, a last
    // lap that stopped before reaching every note the earlier laps did.
    bool stopped_early = false;

    bool empty() const { return notes.empty(); }
};

// What the timeline needs besides the events: the song's notes and bars (read only; the app lends
// the evaluators' shared tables) and the run's setup.
struct ReplaySong {
    std::span<const Note> notes;
    std::span<const Measure> measures;
};

// The bar number holding the song time `seconds`: the last bar that has started, the first bar
// before any starts, and 1 when the song lists none.
int measure_number_at(std::span<const Measure> measures, double seconds);

// Lays a run's events over the song. Events are taken in any order (a hand_over run is sorted
// here). An event naming a note beyond the song's is kept, without a note, and counted in
// `unmatched`. Allocates the timeline (the store builds it on its history thread, where an
// allocation failure ends the run as it does anywhere in the store); an empty run gives an empty
// timeline.
ReplayTimeline build_replay_timeline(const RunEvents& run, const GhostSetup& setup,
                                     const ReplaySong& song);

// How far before a mistake a jump to it lands, so the notes are seen coming.
inline constexpr std::int32_t kMistakeLeadMs = 2000;

// Where to jump to for the next (`direction` > 0) or the previous (< 0) mistake, a note nobody
// played or a wrong key, from `position_ms`: a lead before the mistake (`kMistakeLeadMs`), so the
// notes are seen coming. Going on, a mistake whose lead point is ahead of the playhead lands on it;
// one the playhead is inside the lead of (or whose lead would fall before the start) lands on the
// mistake itself; one the playhead stands exactly at the lead point of is the one just reached, so
// repeated jumps walk on. Going back lands on the lead point of the last mistake whose lead point is
// behind the playhead (the start for an early one). Nullopt when there is none that way. Binary
// search over `timeline.mistakes`.
std::optional<std::int32_t> mistake_position(const ReplayTimeline& timeline,
                                             std::int32_t position_ms, int direction);

// The notes of `timeline` with `from_ms <= time_ms < to_ms`, in order: what the screen draws for a
// window of the song, and what the player passes in a step. Two binary searches.
std::span<const ReplayNote> replay_window(const ReplayTimeline& timeline, std::int32_t from_ms,
                                          std::int32_t to_ms);

// ---- The player -----------------------------------------------------------------------------

// The speeds a replay runs at: the song's own range (clamp_speed_percent).
inline constexpr int kMinReplaySpeedPercent = 25;
inline constexpr int kMaxReplaySpeedPercent = 200;
int clamp_replay_speed(int percent);

// The speed a tap on the speed control moves to: the next of 25, 50, 75, 100, 150 and 200% above
// `current`, with the run's own speed among them, and from the top back to the lowest.
int next_replay_speed(int current, int run_speed_percent);

// Walks a timeline: play, pause, speed and seek. It keeps a position and no timeline of its own, so
// the same player serves any run; every call takes the timeline it walks. Nothing allocates.
class ReplayPlayer {
  public:
    // Back to the start, paused, at the run's own speed.
    void open(const ReplayTimeline& timeline);

    void play(const ReplayTimeline& timeline);
    void pause() { playing_ = false; }
    bool playing() const { return playing_; }
    // The position reached the end (play from there starts over).
    bool finished() const { return finished_; }

    int speed_percent() const { return speed_; }
    void set_speed_percent(int percent) { speed_ = clamp_replay_speed(percent); }

    std::int32_t position_ms() const;
    // 0..1 along the replay; 0 for an empty one.
    double fraction(const ReplayTimeline& timeline) const;

    // Moves to a time, clamped to the replay. The notes it jumps over are not handed over by the
    // next advance(); `take_seeked()` tells the app to cancel the sounds it had started.
    void seek_ms(const ReplayTimeline& timeline, std::int32_t ms);
    void seek_fraction(const ReplayTimeline& timeline, double share);
    bool take_seeked();

    // Moves on by `dt_s` of wall time at the replay's speed and returns the notes the position
    // passed: those with time_ms in [old position, new position). While paused, or for a bad
    // step (negative, NaN), it returns nothing and moves nothing. Reaching the end stops playing
    // and sets finished().
    std::span<const ReplayNote> advance(const ReplayTimeline& timeline, double dt_s);

  private:
    double position_ = 0.0; // song milliseconds
    int speed_ = 100;
    bool playing_ = false;
    bool finished_ = false;
    bool seeked_ = false;
};

} // namespace etude
