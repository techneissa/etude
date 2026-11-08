// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <span>

#include "etude/look.hpp"
#include "etude/song.hpp"

namespace etude {

// Beat lines: where the song's beat grid crosses the note area, so a falling block's length can
// be read by counting lines. The lines sit at song times (every block shares them), from the
// bars of `Song::measures` with their meters and the tempo map: a beat is the meter's beat (a
// quarter in 4/4, a dotted quarter in 6/8, a half in 2/2), a half beat half of it.

// The closest two lines may be on screen, in design points: a finer level that would crowd
// closer anywhere in the window gives way to the next coarser one (half beats, beats, bars), and
// bars closer than this show no lines.
inline constexpr float kBeatLineMinGapPt = 8.0f;

struct BeatLineWindow {
    BeatLines mode = BeatLines::Off;
    double now_s = 0.0;      // song time at the hit line
    double ahead_s = 4.0;    // song seconds over the note area (the look's visible seconds)
    float area_px = 0.0f;    // the note area's height
    float min_gap_px = 8.0f; // kBeatLineMinGapPt in pixels
};

struct BeatLineResult {
    std::size_t count = 0;            // lines written to `out`
    BeatLines level = BeatLines::Off; // the level drawn after thinning; Off when none fit
};

// Writes the heights of the lines in (now, now + ahead] to `out`, lowest first, each as a share
// of the note area above the hit line (0 the hit line, 1 the top). At most `out.size()` are
// written; the coarsest level is tried before giving up. Nothing for Off, an empty song, an empty
// area or a window with no time.
BeatLineResult beat_line_heights(const Song& song, const BeatLineWindow& window,
                                 std::span<float> out);

} // namespace etude
