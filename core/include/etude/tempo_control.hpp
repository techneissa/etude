// SPDX-License-Identifier: GPL-3.0-or-later
// The tempo the player sees and sets. A song's tempo is a map of changes in quarter notes a minute;
// the speed is a whole percent of it. The chip on the bar shows the tempo at the playhead times the
// speed in beats a minute, the beat being the one the meter has there (meter.hpp: a quarter note,
// a dotted quarter in 6/8, 9/8 and 12/8, where "♩. 56" is one beat of a six-eight bar).
// Typing a tempo sets the speed that gives it, in the same beat, at the song's main tempo.
#pragma once

#include "etude/song.hpp"
#include "etude/transport.hpp"

namespace etude {

// Quarter notes per minute in force at `song_s` (the tempo change at or before it; the first one
// before the song starts).
double song_bpm_at(const Song& song, double song_s);

// The tempo that covers the most of the song's time; the earliest wins a tie. A song with no
// duration gives its first tempo. In quarter notes a minute.
double main_bpm(const Song& song);

// The beat the chip counts at `song_s`: its length in quarter notes (1, or 1.5 for a dotted
// quarter) and whether it is dotted. The meter of the bar containing the time; a song with no
// bars is 4/4.
struct BeatUnit {
    double quarters = 1.0;
    bool dotted = false;
};
BeatUnit beat_unit_at(const Song& song, double song_s);

// What the player hears at `song_s` at `speed_percent`, in the beat of beat_unit_at(): the song's
// tempo there x speed / 100.
double effective_bpm(const Song& song, double song_s, int speed_percent);

// The song's main tempo in the beat of beat_unit_at(song, song_s).
double main_beat_bpm(const Song& song, double song_s);

// The speed that plays `target_bpm` where the song's tempo is `song_bpm` (both in the same beat),
// the nearest whole percent kept in kMinSpeedPercent..kMaxSpeedPercent. A target or tempo that is
// not a positive number gives 100.
int speed_for_bpm(double target_bpm, double song_bpm);

// `speed_for_bpm` for a `target_bpm` in the beat at `song_s`, against the song's main tempo.
int speed_for_target(const Song& song, double target_bpm, double song_s);

// The range a typed or dragged target can take, in the beat at `song_s`: the song's main tempo at
// the lowest and highest speed.
struct BpmRange {
    double low = 0.0;
    double high = 0.0;
};
BpmRange bpm_range(const Song& song, double song_s);

// A tempo shown as a whole number of beats a minute, at least 1.
int display_bpm(double bpm);

// The -/+ buttons: `delta_steps` steps of kSpeedStepPercent from `percent`. From a speed between two
// steps (83%) the first step goes to the next multiple of 5 in that direction (85 or 80). Kept in
// range.
int step_speed(int percent, int delta_steps);

} // namespace etude
