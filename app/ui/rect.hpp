// SPDX-License-Identifier: GPL-3.0-or-later
// A rectangle in points: its top left and its size.
#pragma once

namespace etude::app::ui {

struct Rect {
    float x = 0.0f;
    float y = 0.0f;
    float w = 0.0f;
    float h = 0.0f;
    float right() const { return x + w; }
    float bottom() const { return y + h; }
};

} // namespace etude::app::ui
