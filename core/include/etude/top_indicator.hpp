// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/screen_layout.hpp"

namespace etude {

// Where the count-in and the resume countdown show: a small indicator at the right end of the top
// row's status area (the area the bar leaves the status text when it is open), never in the note
// area. The count-in is a row of beat dots, one per beat of the bar, and the beat number after
// it; the countdown is a ring with its number inside, no dots. Everything is measured from the
// status font's size, so it stays as small as the status text. Units are the caller's (pixels or
// points): `status_area` and `font` share them.
inline constexpr int kMaxIndicatorDots = 12;       // more beats than this show the number alone
inline constexpr float kIndicatorDotEm = 0.52f;    // a dot's diameter
inline constexpr float kIndicatorDotGapEm = 0.42f; // between dots
inline constexpr float kIndicatorNumberGapEm = 0.7f; // between the last dot and the number
inline constexpr float kIndicatorNumberEm = 1.15f; // the number's slot: two digits of the status font
inline constexpr float kIndicatorRingEm = 1.7f;    // the countdown ring's diameter

struct TopIndicator {
    RectF rect; // the whole indicator; empty when there is no room
    float dot = 0.0f;       // a dot's diameter (0 for the countdown)
    float dot_gap = 0.0f;   // between dots
    float ring = 0.0f;      // the countdown ring's diameter (0 for the count-in)
    RectF number;           // the slot of the beat number, or the ring's inside
    bool visible() const { return !rect.empty(); }
};

// The indicator's width for `dots` beat dots (0: the countdown's ring), 0 for a bad font.
float top_indicator_width(float font, int dots);

// Places it against the right end of `status_area`, centred vertically in it. Empty when the area
// cannot hold it (a tiny window) or a value is bad: the count-in's click still sounds. The rect is
// always inside `status_area`, which is inside the top row, so it never meets the note area. The
// status text should end `top_indicator_width + gap` before the area's right end while it shows.
TopIndicator place_top_indicator(const RectF& status_area, float font, int dots);

} // namespace etude
