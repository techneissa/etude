// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "audio_out.hpp"

#include <cstdint>
#include <string>

namespace etude::app {

struct SongTimingOptions {
    bool midi = false;        // MIDI out to a virtual port instead of the built-in piano
    bool direct = false;      // midi only: send from the frame thread, as before scheduling
    std::string song;         // empty: the Bach prelude in the test songs
    std::int64_t lead_ns = 0; // see SongOptions::sound_lead_ns
    int frame_jitter_ms = 0;  // random sleep of 0..N ms after every frame
    double seconds = 20.0;    // how long to play
    AudioOptions audio;
};

// --song-timing: plays a song at 60 FPS with no window and prints how late each note started
// against the time it was due (min, median, p95, max). For the built-in piano the start is the
// sample the audio callback began the note on; for MIDI it is the time the message was sent and the
// time a receiver on the virtual port saw it. Returns the process exit code.
int run_song_timing(const SongTimingOptions& options);

} // namespace etude::app
