// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>
#include <vector>

#include "etude/screen_layout.hpp"

namespace etude {

// A control's hit area, from its drawn face and its neighbours. Every control's area is its face
// grown (centred) to `least` on both axes. Where two areas would overlap or come closer than `gap`,
// each is cut at the midpoint between the two faces, less half the gap, so neither reaches over the
// other's drawn part and a dead strip of `gap` is left between them. All in one unit (pixels or
// millimetres).
struct HitArea {
    RectF rect;
    // False when the cut left the area under `least` in either direction, or no longer holding its
    // whole face (two faces closer than the gap, or on top of each other): the layout has to space
    // those controls apart, the area cannot be both clipped and big enough.
    bool full = true;
};
std::vector<HitArea> clip_hit_areas(std::span<const RectF> faces, float least, float gap);

// The distance between two hit areas, negative when they overlap: the gap along the one axis they
// are apart on, corner to corner when they are apart on both, and the smaller overlap (negative)
// when they overlap on both. The audit's measure of rules 2 and 3.
float hit_area_gap(const RectF& a, const RectF& b);

} // namespace etude
