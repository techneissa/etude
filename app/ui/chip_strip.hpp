// SPDX-License-Identifier: GPL-3.0-or-later
// A strip of chips that scrolls sideways (the Learn cards' scale types and chord qualities): the
// chips sit side by side in their own widths, a drag or a swipe scrolls them (and coasts after a
// finger's fast swipe), a wheel scrolls them, and the chip last picked is scrolled into view. A
// press that turned into a scroll is no tap on the chip under it: the strip takes the touch from it
// (`took_touch`), as the library's shelves do. No ImGui here, so tests/chip_strip_test.cpp holds
// the scroll limits, the drag-against-tap threshold and the selected chip in view; the screen feeds
// it the pointer (learn_screen.cpp).
//
// Positions are in design points: `x` along the strip from the first chip's left edge, the offset
// how far the strip is scrolled (0 at its start).
#pragma once

#include "etude/scroll_drag.hpp"

#include <algorithm>
#include <span>

namespace etude::app::ui {

// The width of chips `widths` side by side, `gap` apart.
inline float strip_content_width(std::span<const float> widths, float gap) {
    float w = 0.0f;
    for (const float chip : widths) {
        w += chip;
    }
    return widths.empty() ? 0.0f : w + gap * static_cast<float>(widths.size() - 1);
}

// The farthest the strip scrolls: its content less its view, never below 0.
inline float strip_max_offset(float content_w, float view_w) {
    return std::max(0.0f, content_w - view_w);
}

inline float strip_clamp(float offset, float content_w, float view_w) {
    return std::clamp(offset, 0.0f, strip_max_offset(content_w, view_w));
}

// The offset that brings the chip from `x0` to `x1` into the view, with `margin` of what is next to
// it showing, moving the strip as little as it can from `offset`; the chip as it is when it is in
// view already. A chip wider than the view shows its left edge.
inline float strip_offset_to_show(float offset, float x0, float x1, float content_w, float view_w,
                                  float margin) {
    float shown = offset;
    if (x1 + margin > shown + view_w) {
        shown = x1 + margin - view_w;
    }
    if (x0 - margin < shown) {
        shown = x0 - margin;
    }
    return strip_clamp(shown, content_w, view_w);
}

class ChipStrip {
  public:
    ChipStrip() { drag_.set_axis(ScrollDrag::Axis::Horizontal, true); }

    float offset() const { return offset_; }
    // The strip's content and view widths, for this frame; the offset stays within them.
    void set_extent(float content_w, float view_w) {
        content_ = content_w;
        view_ = view_w;
        offset_ = strip_clamp(offset_, content_, view_);
    }
    float max_offset() const { return strip_max_offset(content_, view_); }
    float content() const { return content_; }
    float view() const { return view_; }
    // Scrolls to bring the chip from `x0` to `x1` into view.
    void show(float x0, float x1, float margin) {
        offset_ = strip_offset_to_show(offset_, x0, x1, content_, view_, margin);
    }
    // A wheel or trackpad's sideways scroll by `points` (positive: toward the end).
    void scroll_by(float points) { offset_ = strip_clamp(offset_ + points, content_, view_); }

    // The pointer, in design points: it landed on the strip, it is at (x, y), it lifted.
    void press(float x, float y, double t, bool finger) {
        touching_ = true;
        drag_.press(x, y, t, finger);
    }
    void move(float x, float y, double t) {
        if (touching_) {
            drag_.move(x, y, t);
        }
    }
    void release(float x, float y, double t) {
        if (touching_) {
            drag_.release(x, y, t);
        }
    }
    // Once a frame after the pointer calls: follows the drag and its coasting. True on the frame
    // the touch became a scroll, when the chip under it must let go.
    bool update(double t) {
        if (!touching_ && !drag_.busy()) {
            return false;
        }
        const ScrollDrag::Step step = drag_.update(t, false, offset_, max_offset());
        offset_ = strip_clamp(offset_ + step.scroll_by, content_, view_);
        if (!drag_.busy()) {
            touching_ = false;
        }
        return step.took_touch;
    }
    // A touch is down on the strip or it is coasting.
    bool busy() const { return drag_.busy(); }
    // The touch has become a scroll.
    bool scrolling() const { return drag_.scrolling(); }
    // Drops the touch and any coasting (the strip was not drawn, or the screen left).
    void cancel() {
        drag_.cancel();
        touching_ = false;
    }

  private:
    ScrollDrag drag_;
    float offset_ = 0.0f;
    float content_ = 0.0f;
    float view_ = 0.0f;
    bool touching_ = false;
};

} // namespace etude::app::ui
