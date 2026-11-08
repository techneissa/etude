// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>

namespace etude {

// The most bars finish_song() builds, the same cap the MIDI loader uses.
inline constexpr std::size_t kMaxSongMeasures = 100'000;

// Microseconds per quarter note for a tempo, rounded to the nearest and clamped to what a MIDI
// tempo event holds (1..0xFFFFFF). A tempo that is zero, negative or NaN gives the slowest value.
std::uint32_t us_per_quarter_from_bpm(double bpm);

// A tempo map with one tempo from tick 0.
TempoMap make_tempo_map(int ticks_per_quarter, double bpm);

// What decides whether the first bar of a song is a pickup, for the MIDI loader, finish_song and
// the score loaders alike.
struct FirstBar {
    std::int64_t length = 0;  // ticks the bar lasts: to the next signature event (MIDI), or what
                              // its music fills (scores)
    std::int64_t nominal = 0; // ticks of a full bar of its own signature
    int numerator = 4;
    int denominator = 4;
    bool next_signature_differs = false; // the bar after it has another signature
    bool marked_pickup = false;          // the file says so (MusicXML implicit="yes" or bar 0)
};
// The first bar is a pickup (numbered 0) when the file marks it, or when it is shorter than its own
// signature, whatever the signature that cuts it short is; and a lone 1/4 first bar followed by
// another signature is one as well (the form exporters write). A full 3/4 bar before 4/4 is bar 1.
bool first_bar_is_pickup(const FirstBar& bar);

// Builds `song.measures` from `song.time_signatures` (sorted, the first at tick 0) up to
// `song.end_tick`: bars of each signature in turn, a signature that falls in the middle of a bar
// starting a new bar there. Numbers run 1, 2, 3 from the first bar; the first bar is numbered 0
// when first_bar_is_pickup says so, the bar being as long as the next signature event leaves it. A
// pickup written with no signature event at its end can't be detected and stays bar 1. Returns
// false if it needs more than kMaxSongMeasures bars; the list then stops at the cap. This is the
// one place the MIDI loader and finish_song take their bar numbers from.
bool build_song_measures(Song& song);

// Finishes a Song a generator or recorder filled in tick terms, so it looks like one load_midi()
// returns and NoteWindow, Playback and Transport work on it. The caller sets, in ticks: `tempo`,
// `notes` (start_tick, end_tick, pitch, velocity, hand, finger, track, channel), `events`
// (tick and the event fields), `time_signatures` (empty means 4/4) and the `parts` it wants named.
//
// This sorts notes the way the loader does (start tick, pitch, track, channel), then fills
// seconds from ticks through every tempo change (tempo map, notes, events), the bar list with
// bar numbers (build_song_measures: a pickup is numbered 0), each note's measure,
// end_tick, duration_s, Note::drum (channel 10), and a Part for every track and channel with notes
// (the caller's named parts are kept, and every part gets its note count and, if it has none, the
// program of its first program change). Notes with end_tick <= start_tick are dropped. Returns
// false if the song needs more than kMaxSongMeasures bars; the bar list then stops at the cap and
// the rest of the song is still sorted and timed.
bool finish_song(Song& song);

} // namespace etude
