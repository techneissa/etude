// SPDX-License-Identifier: GPL-3.0-or-later
// The metronome and the count-in. Both are Click ops made ahead of the clock, so each sounds on its
// exact time through the same look-ahead path as the song's notes (look_ahead.hpp): the beat grid
// turns a Move of the clock into the clicks inside it, and the count-in planner turns the
// transport's count-in into the clicks due in the next window. The seconds countdown that follows
// the settings closing is not one of them: SongController::update makes its soft tick on the frame
// the second passes, so it is within a frame of its time, not on its sample.
//
// Rules, each with a test in tests/metronome_test.cpp:
// - The grid follows the song: a beat is the meter's beat (meter.hpp: one note of the denominator,
//   three eighths in 6/8, 9/8 and 12/8), at the time the tempo map gives it, bar start accented.
//   A pickup bar's beats are numbered back from the end of the bar. Speed only stretches the wall
//   time of a Move, so clicks across a tempo change and at any speed fall on the beat grid.
// - A subdivision splits each beat into equal parts; the extra clicks are quieter and never
//   accented. Divide gives 2 parts (3 in a compound meter, the eighths of the beat), Triplets 3
//   parts in a simple meter and the same as Divide in a compound one.
// - A Move gives the clicks in [from_s, to_s): consecutive Moves share their boundary, so a beat
//   is sounded once. A loop wrap starts the grid again from the loop start.
// - The count-in clicks are due at now + (beat - elapsed) / beats-per-second, so a speed change
//   mid-count moves the rest; a click already due when asked for is handed over late, never lost.
#pragma once

#include "etude/playback.hpp"
#include "etude/song.hpp"
#include "etude/transport.hpp"

#include <cstdint>
#include <vector>

namespace etude {

enum class ClickSubdivision : std::uint8_t { Off, Divide, Triplets };
enum class ClickVoice : std::uint8_t { WoodBlock, Click, Beep };

inline constexpr int kMinClickVolume = 0;
inline constexpr int kMaxClickVolume = 100;
inline constexpr int kDefaultClickVolume = 70;

struct MetronomeSettings {
    bool accent = true; // beat one is louder and higher
    ClickSubdivision subdivision = ClickSubdivision::Off;
    friend bool operator==(const MetronomeSettings&, const MetronomeSettings&) = default;
};

// The click's voice for MIDI out and the built-in sound: General MIDI percussion on channel 10 (Hi
// and Low Wood Block; Metronome Bell and Click; Open and Mute Triangle). `volume_percent` scales
// the velocities (0 mutes). The built-in sound makes the same three voices itself from the note
// numbers.
ClickSound click_sound_for(ClickVoice voice, int volume_percent);

// The song's beats. Copies what it needs; keeps no reference to the Song.
class BeatGrid {
  public:
    void set_song(const Song& song);

    // Appends the Click ops for the beats (and subdivisions) of the song time span of `move`, a
    // Move op, in time order, each at the wall time the Move gives its song time. Nothing for a
    // song with no bars or an empty span.
    void clicks(const TransportOp& move, const MetronomeSettings& settings,
                std::vector<TransportOp>& out) const;

    // The latest bar beat at or before `song_s`: its song time and its number in the bar. False for
    // a song with no bars or a time before the first beat. For the beat pulse.
    struct Beat {
        double time_s = 0.0;
        int number = 0;
    };
    bool last_beat(double song_s, Beat* out) const;

  private:
    TempoMap tempo_;
    std::vector<Measure> measures_;
};

// What the transport's count-in is doing, as the planner needs it.
struct CountInInput {
    int total_beats = 0;      // 0: no count-in runs
    double beats_left = 0.0;  // fractional, as the transport counts
    double beat_s = 0.5;      // song seconds per beat
    int numerator = 4;        // beats per bar, for the numbering
    int speed_percent = 100;
    double start_s = 0.0;     // the song time the count-in leads to
};

// Appends the Click ops of count-in beats `next_beat`.. that are due before now_ns + window_ns, and
// moves `next_beat` past them. Beat k (0-based) is due when the beats counted reach k: the first is
// due the moment the count-in starts. `to_s` of a click is its song time (before start_s), `beat`
// its number in the bar, `accent` beat one when asked. Nothing when no count-in runs.
void plan_count_in(const CountInInput& in, std::int64_t now_ns, std::int64_t window_ns,
                   const MetronomeSettings& settings, int& next_beat, std::vector<TransportOp>& out);

// The first count-in beat that has not sounded yet at this moment of the count (a beat due exactly
// now counts as not sounded: a cancel at now drops it). For starting the plan again.
int first_unsounded_count_in_beat(const CountInInput& in);

// The number to show for the count-in at this moment: 1-based beat in the bar of the last beat
// reached, 0 when no count-in runs.
int count_in_label(const CountInInput& in);

} // namespace etude
