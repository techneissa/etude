// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>

#include "etude/screen_layout.hpp"

namespace etude {

// A point in window pixels.
struct SpotF {
    float x = 0.0f;
    float y = 0.0f;
};

struct TapSpot {
    bool found = false;
    SpotF at;
};

// A point to tap that lies outside `card` and below `top` (the bottom of the top row, where the bar
// and the gear take their own taps), inside a `w` x `h` window: the middle of the widest free strip
// left of the card, right of it, or under it, kept `margin` clear of the card and the window's
// edge. Used to tap "outside a popover" at any window size and UI scale.
inline TapSpot tap_spot_outside(const RectF& card, float w, float h, float top, float margin) {
    struct Strip {
        RectF r;
    };
    const float cy0 = std::max(top, 0.0f);
    const Strip strips[] = {
        {{0.0f, cy0, card.x, h - cy0}},                        // left of the card
        {{card.right(), cy0, w - card.right(), h - cy0}},      // right of it
        {{0.0f, card.bottom(), w, h - card.bottom()}},         // under it
    };
    TapSpot best;
    float best_area = 0.0f;
    for (const Strip& s : strips) {
        const float inner_w = s.r.w - 2.0f * margin;
        const float inner_h = s.r.h - 2.0f * margin;
        if (!(inner_w > 0.0f) || !(inner_h > 0.0f)) {
            continue;
        }
        if (inner_w * inner_h > best_area) {
            best_area = inner_w * inner_h;
            best.found = true;
            best.at = {s.r.x + s.r.w * 0.5f, s.r.y + s.r.h * 0.5f};
        }
    }
    return best;
}

} // namespace etude
