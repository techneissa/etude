// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/touch_gesture.hpp"

#include <vector>

namespace etude {

// A touch that scrolls a page: drag to scroll, and a finger's fast drag coasts on after it lifts.
// It sits on `GestureRecognizer`, so the slop, the fling speed and its ease-out are the shared
// ones, and it knows nothing of the screen: positions are in the caller's units (points), the
// offset is how far the page is scrolled (0 at the top, `max_offset` at the end).
//
// Rules:
// - The touch scrolls after it moves past the slop, and the page follows it. Holding still first
//   makes no difference (no long press).
// - A widget that takes the touch (a slider being dragged) keeps it for good: from the moment the
//   caller says `held_by_widget`, that touch never scrolls and never coasts, even if the finger
//   drifts. A touch the page never scrolled with never coasts either.
// - A finger that lifts while moving fast coasts on, its speed halving on the recogniser's
//   schedule. A mouse drag stops at the lift.
// - A new touch stops the coasting at once, and so does either end of the page (a hard stop).
// - A page scrolls up and down, or (Axis::Horizontal) sideways. With `lock`, the touch's first
//   move past the slop decides (decide_drag_axis): a sideways page takes only a sideways drag, a
//   page that scrolls down every other one, so a row that scrolls sideways can sit inside a page
//   that scrolls down and the two never both take a touch.
class ScrollDrag {
  public:
    enum class Axis : unsigned char { Vertical, Horizontal };
    ScrollDrag();
    void set_axis(Axis axis, bool lock);

    // A touch began at (x, y). `finger` is false for a mouse.
    void press(float x, float y, double t, bool finger);
    void move(float x, float y, double t);
    void release(float x, float y, double t);
    // Drops the touch and any coasting (the page went away, or the touch was taken).
    void cancel();

    struct Step {
        float scroll_by = 0.0f;  // move the offset by this much (positive: toward the end)
        bool took_touch = false; // the touch just became a scroll: controls under it let go
    };
    // Call once per frame after the touch calls. `offset` and `max_offset` describe the page now.
    Step update(double t, bool held_by_widget, float offset, float max_offset);

    // A touch is down or the page is coasting.
    bool busy() const { return rec_.touching() || rec_.flinging(); }
    // The touch is scrolling the page (not yet lifted).
    bool scrolling() const { return scrolling_; }
    bool coasting() const { return rec_.flinging(); }

  private:
    GestureRecognizer rec_;
    std::vector<GestureRecognizer::Event> events_; // update()'s copy of rec_'s events, kept
    bool finger_ = false;
    bool scrolling_ = false; // the touch is scrolling the page now
    bool scrolled_ = false;  // the touch has scrolled the page, so it may coast
    bool denied_ = false;    // a widget took the touch
    bool starting_ = false;  // past the slop: its first move decides
    Axis axis_ = Axis::Vertical;
    bool lock_ = false;
};

} // namespace etude
