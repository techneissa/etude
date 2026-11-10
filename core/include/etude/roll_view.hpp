// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/hand.hpp"
#include "etude/song.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace etude {

// The pure half of the section editor's piano-roll strip: which part of the song is in view, how
// zoom and pan move it, the bar and beat grid, where the split cursor snaps, and the rectangles of
// the note thumbnail and the loupe. No drawing, no clock, no files.

// ---- The visible window

// The part of the song on screen, in seconds. Always inside [0, duration_s] with start < end.
struct RollView {
    double start_s = 0.0;
    double end_s = 1.0;

    double span_s() const { return end_s - start_s; }
    friend bool operator==(const RollView&, const RollView&) = default;
};

// The most the roll zooms in: this many bars across.
inline constexpr int kMinZoomBars = 2;
// Without bars to count, the narrowest view in seconds.
inline constexpr double kMinZoomSeconds = 4.0;
// Up to this many bars across, the cursor snaps to beats instead of bars.
inline constexpr int kBeatSnapBars = 8;

// The whole song.
RollView whole_song_view(const Song& song);
// The narrowest view at `around_s`: kMinZoomBars bars from the bar holding it (kMinZoomSeconds
// for a song with no bars), never more than the whole song.
double min_view_span(const Song& song, double around_s);
// Zooms by `factor` (more than 1 zooms in) keeping `focus_s` at the same place on screen. The span
// stays between min_view_span and the whole song, and the view inside the song.
RollView zoom_view(const Song& song, const RollView& view, double factor, double focus_s);
// Moves the view by `delta_s`, stopping at the song's ends.
RollView pan_view(const Song& song, const RollView& view, double delta_s);
// A view of [start_s, end_s] with `margin` (a fraction of its length) on each side, widened to the
// narrowest zoom and kept inside the song.
RollView view_around(const Song& song, double start_s, double end_s, double margin = 0.06);
// How many bars the view shows (counting partial ones), 0 for a song with no bars.
int bars_in_view(const Song& song, const RollView& view);
// The cursor snaps to beats at this zoom.
bool beat_snap_zoom(const Song& song, const RollView& view);

// ---- Grid and cursor

struct GridLine {
    double time_s = 0.0;
    bool bar = false;      // a bar line; else a beat line inside a bar
    int bar_number = 0;    // Measure::number of the bar holding it
    int beat = 1;          // 1-based beat in that bar (1 for a bar line)
    int written_number = 0; // Measure::written_number() of that bar: the printed page's number
};

// The bar lines, and with `beats` the beat lines too, inside [start_s, end_s], in time order.
// A beat is one note of the time signature's denominator.
void grid_lines(const Song& song, double start_s, double end_s, bool beats,
                std::vector<GridLine>& out);

// The nearest bar start (or beat) to `seconds`, clamped to the song. A song with no bars returns
// `seconds` clamped.
double snap_to_grid(const Song& song, double seconds, bool beats);

// "Bar 12 · beat 3": the bar and beat holding `seconds`. When the score repeats and the bar's
// written number differs, the sheet's number follows: "Bar 9 · sheet bar 5 · beat 3". A song with
// no bars gives "m:ss".
std::string position_label(const Song& song, double seconds);

// ---- Note thumbnail

struct PitchRange {
    int lowest = 36;
    int highest = 96;
    int rows() const { return highest - lowest + 1; }
};
// The pitches of the song's notes (not drums), widened to at least two octaves so a short piece
// does not stretch into fat blocks. An empty song gives C2..C7.
PitchRange song_pitch_range(const Song& song);

// The pitches of the notes sounding in [t0_s, t1_s], padded to at least `min_rows` rows around
// their middle, for a magnified look at a short stretch. An empty stretch gives the song's range.
PitchRange window_pitch_range(const Song& song, double t0_s, double t1_s, int min_rows = 14);

// A note block in tile pixels (x grows right, y down with the highest pitch at the top).
struct RollRect {
    float x0 = 0.0f;
    float x1 = 0.0f;
    float y0 = 0.0f;
    float y1 = 0.0f;
    Hand hand = Hand::Unknown;
    // For the loupe: -1 the note ends before `split_s`, +1 it starts at or after, 0 it crosses.
    std::int8_t side = 1;
    // Whole octaves the note was moved to fit the pitch range (+1 raised, -2 lowered), 0 when it
    // sits where it is written. The block carries the fold marker the falling notes show.
    std::int8_t fold = 0;
};

// The notes that sound inside [t0_s, t1_s] as blocks in a w x h pixel tile covering exactly that
// time. Every block is at least one pixel wide; blocks are clipped to the tile. `split_s` fills
// RollRect::side. A note whose pitch is outside `pitches` folds by whole octaves into it and
// carries RollRect::fold; none is hidden except where the range has no row for its pitch class.
// Appends to `out` after clearing it.
void roll_note_rects(const Song& song, double t0_s, double t1_s, int w, int h,
                     const PitchRange& pitches, double split_s, std::vector<RollRect>& out);

// A cached image of the roll: [t0_s, t1_s] drawn w x h pixels. Drawn once per zoom level and
// reused while the view stays inside it and about as sharp as it was drawn.
struct RollTile {
    double t0_s = 0.0;
    double t1_s = 0.0;
    int w = 0;
    int h = 0;

    bool valid() const { return w > 0 && h > 0 && t1_s > t0_s; }
    double px_per_s() const { return valid() ? static_cast<double>(w) / (t1_s - t0_s) : 0.0; }
    friend bool operator==(const RollTile&, const RollTile&) = default;
};
inline constexpr int kMaxTileWidth = 4096;

// The tile that serves `view` shown `view_w_px` wide: the view's own time plus as much again on
// each side (so a pan stays in it), cut to the song, at the pixel density the view needs, at most
// kMaxTileWidth wide.
RollTile tile_for_view(const Song& song, const RollView& view, int view_w_px, int h_px);
// True when `tile` can no longer serve the view: it does not cover it, or it was drawn at under
// 0.7 or over 1.6 times the density the view needs now.
bool tile_stale(const RollTile& tile, const RollView& view, int view_w_px);
// The part of the tile to show for `view`, as texture coordinates u0..u1 (0..1). Only for a tile
// that covers the view.
void tile_uv(const RollTile& tile, const RollView& view, float& u0, float& u1);

} // namespace etude
