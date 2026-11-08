// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

namespace etude {

inline constexpr int kMinSpeedPercent = 25;
inline constexpr int kMaxSpeedPercent = 200;
inline constexpr int kSpeedStepPercent = 5;
// The shortest A-B loop: a shorter one would wrap many times per frame.
inline constexpr double kMinLoopSeconds = 0.1;

// The speed is a whole percent (1% resolution, so a typed tempo lands within a percent of it):
// clamped to 25..200.
int clamp_speed_percent(int percent);

// Clamps to 25..200 and rounds to the nearest multiple of 5 (halves round up): the speeds the
// presets, the practice plans and the auto speed-up use.
int snap_speed_percent(int percent);

enum class SnapMode : std::uint8_t { Nearest, Floor, Ceil };

// Moves a time to the start of a bar. Floor is the bar containing it (or starting at it), Ceil the
// next bar start at or after it, Nearest the closer of the two. Past the last bar start, Ceil gives
// the song's end. A song with no bars returns `seconds` clamped to the song.
double snap_to_measure(const Song& song, double seconds, SnapMode mode = SnapMode::Nearest);

struct LoopSpan {
    bool ok = false; // false when a bar number doesn't exist or the range is backwards
    double start_s = 0.0;
    double end_s = 0.0;
};

// From the start of the bar numbered `first` to the end of the bar numbered `last` (the start of
// the next bar, or the song's end after the final bar). Numbers are Measure::number, so a pickup
// bar is 0.
LoopSpan measures_span(const Song& song, int first, int last);

// How the speed rises while a loop is practised. Speed starts at `start_percent`, and each clean
// pass, as reported by the caller, adds `step_percent` up to `max_percent`.
struct AutoSpeedUp {
    bool enabled = false;
    int start_percent = 60;
    int step_percent = 5;
    int max_percent = 100;

    // The speed for the next pass. An unclean pass, or a speed already at or above the maximum,
    // changes nothing.
    int next_percent(int current_percent, bool clean_pass) const;
};

enum class TransportState : std::uint8_t {
    Paused,  // the user paused, or never played
    CountIn, // playing, waiting out the count-in beats
    Playing, // the position moves
    Held,    // playing, but stopped by hold() or a hold point (Wait Mode)
    Ended,   // reached the end of the song
};

// What happened to the clock, in order. The playback scheduler turns these into MIDI; the metronome
// reads Click. `at_ns` is on the caller's clock.
struct TransportOp {
    enum class Kind : std::uint8_t {
        Play,    // the user pressed play at `to_s`; the count-in, if any, starts now
        Pause,   // the user paused at `to_s`
        Hold,    // the clock stopped at `to_s` without a pause (Wait Mode)
        Release, // the clock runs again from `to_s`
        Seek,    // jumped from `from_s` to `to_s`
        Wrap,    // the loop jumped from its end `from_s` back to its start `to_s`
        Move,    // the clock ran through [from_s, to_s) of the song over [at_ns, end_ns)
        End,     // reached the end of the song at `to_s`
        Click,   // a metronome or count-in beat (metronome.hpp makes them): `to_s` is its song time,
                 // before the song's start during a count-in
    };
    Kind kind = Kind::Move;
    std::int64_t at_ns = 0;
    std::int64_t end_ns = 0; // Move only
    double from_s = 0.0;
    double to_s = 0.0;
    int speed_percent = 100; // Move only; constant inside one Move
    int beat = 0;            // Click only: 1-based beat in the bar
    bool accent = false;     // Click only: beat one
    bool count_in = false;   // Click only: a count-in beat, before the song moves
    int sub = 0;             // Click only: 0 on the beat, 1.. a subdivision inside it
};

// The song clock. It has no clock of its own: every call takes `now_ns`, a monotonic time from the
// caller, and the position only moves inside update() and the commands, which settle the clock to
// `now_ns` first, so a command never changes what already happened.
//
// Everything that changes the clock is also queued as a TransportOp; drain them with take_ops()
// once per frame and feed them to Playback. The clock only waits out a count-in; the clicks of the
// count-in and the metronome are not ops of the clock: they are planned ahead of it
// (plan_count_in, BeatGrid) so each sounds on its exact time.
class Transport {
  public:
    // Copies what the clock needs (duration, tempo map, time signatures, bars). Resets the position
    // to 0, pauses, and clears the loop and hold point. Speed and count-in stay.
    //
    // Drain take_ops() and feed the ops to Playback and the evaluators before calling this: ops
    // still queued belong to the old song and are dropped; the return value is how many (0 when
    // the caller drained first). Then the queue holds what the reset did, at the last settled
    // time: a Pause if the clock was playing, and a Seek to 0 if it was elsewhere.
    std::size_t set_song(const Song& song);

    void play(std::int64_t now_ns);
    void pause(std::int64_t now_ns);
    // Clamped to the song. A NaN target is ignored. Forgets a hold_at() point.
    void seek(std::int64_t now_ns, double song_seconds);
    // A skip button or arrow key: moves the position by `delta_s` (negative goes back), clamped to
    // the song. While a loop is on and the position is inside it, the skip stays inside: one that
    // would land at or past the loop end wraps to the loop start, and one before the start stops
    // at the start. From outside the loop, a skip that lands outside it ends the loop (scrub).
    // Returns true when it ended the loop.
    bool skip(std::int64_t now_ns, double delta_s);
    // A jump of the user's (scrubber drag or tap, skip, the app's seek): seeks to `song_seconds`.
    // The loop stays on only while the position is inside it, so a time outside it (before its
    // start, or at or past its end) ends the loop, and what is drawn matches what plays; returns
    // true when it did. A time inside the loop keeps the loop on. seek() itself leaves the loop
    // alone.
    bool scrub(std::int64_t now_ns, double song_seconds);
    // Returns the speed applied (clamped to 25..200, a whole percent). The position is continuous.
    int set_speed_percent(std::int64_t now_ns, int percent);
    // Settles the clock to `now_ns`. A `now_ns` that isn't later than the last call is ignored.
    void update(std::int64_t now_ns);

    // Wait Mode. hold() stops the clock where it is without a user pause; hold_at() arms a stop for
    // the exact moment the clock reaches that song time (ignored when it is not ahead of the
    // position), so the clock never overshoots a note by a frame.
    void hold(std::int64_t now_ns);
    void release(std::int64_t now_ns);
    void hold_at(std::int64_t now_ns, double song_seconds);

    // A-B loop. The clock wraps from `end` to `start` exactly: the position is never seen at `end`
    // while looping. Reversed times are swapped and both are clamped to the song. Returns false and
    // changes nothing if the loop would be shorter than kMinLoopSeconds or a time is NaN. Resets
    // the loop count.
    bool set_loop(std::int64_t now_ns, double start_s, double end_s);
    void set_loop_enabled(std::int64_t now_ns, bool enabled);
    void clear_loop(std::int64_t now_ns);

    // The caller reports how a pass through the loop went (the evaluator knows); with auto speed-up
    // on, a clean pass raises the speed.
    void report_pass(std::int64_t now_ns, bool clean);
    // Enabling sets the speed to the policy's start. Disabling leaves the speed where it is.
    void set_auto_speed_up(std::int64_t now_ns, const AutoSpeedUp& policy);

    // Beats to wait after play() before the position moves, at the tempo and time signature where
    // the position is. 0 turns the count-in off. A beat is the meter's beat (meter.hpp: a quarter
    // in 4/4, a dotted quarter in 6/8). Does not repeat after a hold. See set_count_in_bars for the
    // musical form.
    void set_count_in_beats(int beats);
    // The same in whole bars of the meter where play() starts (0 is off; wins over
    // set_count_in_beats). Starting at a pickup bar the count-in leaves out the pickup's beats, so
    // the pickup is the last of the counted bar: 1 bar of 4/4 before a one-beat pickup is 1 2 3.
    void set_count_in_bars(int bars);

    // Moves the queued ops into `out` (appending) and empties the queue.
    void take_ops(std::vector<TransportOp>& out);

    double position_s() const { return position_s_; }
    double duration_s() const { return duration_s_; }
    int speed_percent() const { return speed_percent_; }
    TransportState state() const;
    bool playing() const { return playing_; }
    bool held() const { return held_; }
    bool ended() const { return ended_; }
    bool counting_in() const { return playing_ && count_in_left_ > 0.0; }
    double count_in_beats_left() const { return count_in_left_; }
    // What the count-in click planner needs (see plan_count_in): zeros when none runs.
    int count_in_total() const { return counting_in() ? count_in_total_ : 0; }
    double count_in_beat_seconds() const { return count_in_beat_s_; }
    int count_in_numerator() const { return count_in_numerator_; }
    // Where the song starts after the count-in: the position play() began at.
    double count_in_start_s() const { return position_s_; }
    // Wall seconds until the position starts to move, at the current speed; 0 when none runs.
    double count_in_wall_left_s() const;
    bool loop_enabled() const { return loop_on_; }
    double loop_start_s() const { return loop_a_; }
    double loop_end_s() const { return loop_b_; }
    int loop_count() const { return loop_count_; }
    // The song time Wait Mode armed the clock to stop at, while that is still ahead.
    std::optional<double> hold_point() const { return hold_at_; }
    int clean_passes() const { return clean_passes_; }
    const AutoSpeedUp& auto_speed_up() const { return auto_; }

  private:
    void settle(std::int64_t now_ns);
    void push(const TransportOp& op);
    double beat_seconds_at(double song_seconds) const; // song seconds per count-in beat there
    int beats_per_bar_at(double song_seconds) const;

    double duration_s_ = 0.0;
    TempoMap tempo_;
    std::vector<TimeSignature> signatures_{TimeSignature{}};
    std::vector<Measure> measures_;

    bool started_ = false; // the first call sets the clock's origin
    std::int64_t last_ns_ = 0;
    double position_s_ = 0.0;
    int speed_percent_ = 100;
    bool playing_ = false;
    bool held_ = false;
    bool ended_ = false;
    std::optional<double> hold_at_;

    bool loop_on_ = false;
    double loop_a_ = 0.0;
    double loop_b_ = 0.0;
    int loop_count_ = 0;
    int clean_passes_ = 0;
    AutoSpeedUp auto_;

    int count_in_beats_ = 0;
    int count_in_bars_ = 0;
    int count_in_total_ = 0;
    double count_in_left_ = 0.0;   // beats
    double count_in_beat_s_ = 0.5; // song seconds per beat, fixed at play()
    int count_in_numerator_ = 4;

    std::vector<TransportOp> ops_;
};

} // namespace etude
