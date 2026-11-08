// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/screen_layout.hpp"

#include <array>
#include <cstddef>
#include <span>

namespace etude {

// Where the chord label's parts stand in the strip above the note area (`ScreenLayout::
// chord_label`), and where a name goes when something else shares its height. Pure.

// The most names (two hands, each with up to two labels fading out and one fading in) and the most
// things sharing the strip's height that the placing handles; more are ignored, so nothing
// allocates.
inline constexpr std::size_t kChordMaxNames = 6;
inline constexpr std::size_t kChordMaxBlockers = 8;

// A horizontal run in window pixels.
struct ChordSpan {
    float x0 = 0.0f;
    float x1 = 0.0f;
};

// The vertical geometry of the strip, from the root's font size: the name stands on its baseline,
// a thin line with a short tick at each end runs along the strip's foot under it. All of it lies
// inside the strip.
struct ChordLabelGeometry {
    float line_y = 0.0f; // top of the line
    float line_thickness = 1.0f;
    float tick_height = 2.0f; // the ticks stand this far above the line
    float baseline = 0.0f;    // of the root
    float name_top = 0.0f;    // the top of the root's line box (cap_top_em above its capitals)
    float margin = 0.0f;      // the names keep this far from the window's sides
    float pad = 0.0f;         // and this far from anything they step aside from
};
// `font_px` is the root's size (`ScreenLayout::chord_label_font_px`), `ui_unit` the pixels of one
// design point, `cap_em` and `cap_top_em` the face's capital height and the room above it, as
// shares of the size (`Text::vertical_metrics`).
ChordLabelGeometry chord_label_geometry(const RectF& strip, float font_px, float ui_unit,
                                        float cap_em, float cap_top_em);

// The runs of `rects` that share the strip's height, each grown by `pad` on both sides: what a name
// and its line keep clear of. Returns how many were written to `out` (at most out.size()).
std::size_t chord_label_blockers(const RectF& strip, std::span<const RectF> rects, float pad,
                                 std::span<ChordSpan> out);

// What one name asks for: its width, the middle of the keys it belongs over, and its group: the
// names of one group (a hand's label fading out and the one fading in) may stand over one another;
// each group steps aside from the groups placed before it.
struct ChordNameWant {
    float width = 0.0f;
    float centre = 0.0f;
    int group = 0;
};
struct ChordNamePlace {
    float left = 0.0f;
    bool placed = false; // false: nowhere is free, the name is not drawn
};
// Centres each name over its keys inside [x_min, x_max]; where that run meets one of `blocked` (a
// run already grown by the pad) or a name placed before it (grown by `pad`), the name steps to the
// nearest place that is free, left or right of the obstacle, keeping `x_min` and `x_max`. `out`
// takes one place per want.
void place_chord_names(std::span<const ChordNameWant> wants, float x_min, float x_max, float pad,
                       std::span<const ChordSpan> blocked, std::span<ChordNamePlace> out);

// The parts of [x0, x1] left after taking out `blocked`, in order, each at least one pixel wide:
// the line stays put and is cut round what shares its height. Returns how many were written.
std::size_t chord_line_runs(float x0, float x1, std::span<const ChordSpan> blocked,
                            std::span<ChordSpan> out);

} // namespace etude
