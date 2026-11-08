// SPDX-License-Identifier: GPL-3.0-or-later
// Where a count-in or the resume countdown shows over the falling notes: a band across the top of
// the note area, where the blocks come in out of the fade. The band is the background colour,
// solid, so no block shows under the count, with the usual fade below it; the notes hold still
// while it counts. The count is one large number at the band's centre and, for a count-in, one
// small dot per beat of the bar under it.
#pragma once

#include "etude/screen_layout.hpp"

namespace etude {

// The number's size in design points: at least this, more on a tall note area, never so much that
// the number and the dots take more than `kCountBandMaxShare` of the note area.
inline constexpr float kCountMinFontPt = 40.0f;
inline constexpr float kCountMaxFontPt = 72.0f;
// The number's size as a share of the note area's height, between those two.
inline constexpr float kCountFontShare = 0.11f;
inline constexpr float kCountBandMaxShare = 0.3f;
// The beat dots: their size and the gap between them, in design points.
inline constexpr float kCountDotPt = 9.0f;
inline constexpr float kCountDotGapPt = 8.0f;
// Room above the number, between it and the dots, and below the dots, in design points.
inline constexpr float kCountPadPt = 6.0f;

struct CountBand {
    RectF band;            // the solid part, across the note area's top; empty: nothing shows
    float number_cx = 0.0f; // the number's centre
    float number_cy = 0.0f;
    float font_px = 0.0f;
    float dots_cy = 0.0f; // the dots' centre line, inside the band
    float dot_px = 0.0f;
    float dot_gap_px = 0.0f;

    bool visible() const { return !band.empty(); }
};

// The band for a note area `area` (window pixels) at `unit` pixels per design point. The number and
// the dots start at `clear_to` or below (the bottom of the toasts' own slot, which lies over the
// top of the note area), and the band runs from the area's top down past them, so the toast and
// the count never meet and no block shows under either. Nothing (an empty band) when the area is
// too small to hold the smallest count within its share below that line.
CountBand place_count_band(const RectF& area, float unit, float clear_to = 0.0f);

} // namespace etude
