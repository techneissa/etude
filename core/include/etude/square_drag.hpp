// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/slider_drag.hpp"

#include <optional>

namespace etude {

// A point in a square control, each axis 0..1 (x left to right, y top to bottom).
struct SquarePos {
    float x = 0.0f;
    float y = 0.0f;
    friend bool operator==(const SquarePos&, const SquarePos&) = default;
};

// The square control's corners in the caller's units (points or pixels; all inputs use the same).
struct SquareArea {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 1.0f;
    float y1 = 1.0f;
};

// 0..1 on each axis for a point, clamped; 0 on an axis with no length.
SquarePos square_pos_at(SquareArea area, float x, float y);

// Decides what a touch on a square control (the colour picker's saturation and brightness square)
// means: the slider rules (slider_drag.hpp) on two axes at once, and no scroll hand-off, since
// the touch belongs to the square whichever way it goes.
// - A mouse press sets the value to the press point and the drag follows the pointer.
// - A finger press changes nothing. A finger that stays within `slop` of where it began is a tap:
//   on release, a tap off the thumb moves the thumb to the tap, a tap on the thumb changes nothing.
// - Once a finger is past the slop it is a drag, and the value moves from where it was by the
//   finger's movement since it began (so a finger on the thumb never makes it jump), clamped.
class SquareDrag {
  public:
    // `value` is the thumb's position; `thumb_reach` is how far from its centre a touch still
    // counts as on the thumb. Returns the new value when the press itself sets one.
    std::optional<SquarePos> press(SliderPointer pointer, float x, float y, SquareArea area,
                                   SquarePos value, float thumb_reach);
    // The pointer moved. Returns the new value when it changes one.
    std::optional<SquarePos> move(float x, float y, SquareArea area, float slop);
    // The pointer lifted (or the touch was taken away: `cancelled`). Returns the new value for a
    // tap off the thumb.
    std::optional<SquarePos> release(float x, float y, SquareArea area, bool cancelled = false);

    bool dragging() const { return mode_ == SliderMode::Drag; }
    SliderMode mode() const { return mode_; }

  private:
    SliderPointer pointer_ = SliderPointer::Mouse;
    SliderMode mode_ = SliderMode::Idle;
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    SquarePos origin_value_;
    bool on_thumb_ = false;
};

} // namespace etude
