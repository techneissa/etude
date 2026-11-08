// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/sheet_map.hpp"
#include "etude/song.hpp"

#include <cstdint>

namespace etude {

// What a point on the sheet means: the bar under it and the song positions there. A written bar
// that plays more than once (a repeat) means the pass nearest the song's position: the pass in the
// reading run being played, else the next pass, else the last one before.

// The written bar (SheetBar::bar) under `x` on `system`, in the system's picture; between two bars
// the nearer one; -1 when the system has none.
int sheet_written_bar_at(const SheetLayout& layout, int system, float x);

// The played bar (index into Song::measures) that reads written bar `written`, for a song at
// `tick`, by the rule above; -1 when none does.
int sheet_played_bar_for(const SheetMap& map, int written, double tick);

// The played bar reading `written` in the same pass (reading run) as played bar `played`; -1 when
// that pass does not read it.
int sheet_played_bar_near(const SheetMap& map, int written, int played);

// A loop being dragged from played bar `anchor` to the written bar `written`: both ends in one
// pass, so the loop never spans a repeat's jump or bars never dragged over. The anchor's own pass
// when it reads `written`; else the pass nearest the anchor that reads both bars (the anchor moves
// to its bar there); else none (-1, -1: the drag keeps its last end).
struct SheetLoopEnds {
    int anchor = -1;
    int last = -1;
};
SheetLoopEnds sheet_loop_ends(const SheetMap& map, int anchor, int written);

struct SheetTarget {
    int played_bar = -1;          // -1: nothing there
    std::int64_t beat_tick = 0;   // the bar's beat nearest the point (never the next bar's first)
    std::int64_t onset_tick = 0;  // the chord nearest the point in that bar (never a rest; the
                                  // bar's start when it has none)
};

// The point `x` on `system` for a song at `tick`: its played bar, the tick under the point along
// the cursor's path (the inverse of sheet_cursor_at), and from there the nearest beat (by the bar's
// time signature: a beat is a 1/denominator note) and the nearest chord (a rest is skipped).
SheetTarget sheet_target_at(const SheetLayout& layout, const SheetMap& map, const Song& song,
                            int system, float x, double tick);

struct SheetLoopRange {
    std::int64_t start_tick = 0;
    std::int64_t end_tick = 0; // exclusive: the end of the last bar
    int first_bar = -1;        // played bars, first <= last; -1: none
    int last_bar = -1;
};

// The loop over whole bars from played bar `a` to played bar `b` (either order).
SheetLoopRange sheet_loop_range(const SheetMap& map, const Song& song, int a, int b);

} // namespace etude
