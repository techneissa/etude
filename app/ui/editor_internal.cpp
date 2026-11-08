// SPDX-License-Identifier: GPL-3.0-or-later
#include "editor_internal.hpp"

namespace etude::app::ui {

FoldMarker fold_marker(float x0, float y0, float x1, float y1, int fold) {
    FoldMarker m;
    const float a = std::min({0.4f * (x1 - x0), 0.8f * (y1 - y0), 10.0f});
    if (fold == 0 || a < 3.0f) {
        return m;
    }
    const float cx = 0.5f * (x0 + x1);
    const float cy = 0.5f * (y0 + y1);
    const float dir = fold > 0 ? -1.0f : 1.0f; // raised: points up
    m.shown = true;
    m.tip = {cx, cy + dir * 0.5f * a};
    m.left = {cx - 0.5f * a, cy - dir * 0.5f * a};
    m.right = {cx + 0.5f * a, cy - dir * 0.5f * a};
    return m;
}

} // namespace etude::app::ui
