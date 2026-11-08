// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/scroll_drag.hpp"

#include <algorithm>
#include <vector>

namespace etude {

using Kind = GestureRecognizer::Event::Kind;

ScrollDrag::ScrollDrag() {
    events_.reserve(32);
    GestureRecognizer::Config config;
    config.long_press = false;
    rec_.set_config(config);
}

void ScrollDrag::set_axis(Axis axis, bool lock) {
    axis_ = axis;
    lock_ = lock;
}

void ScrollDrag::press(float x, float y, double t, bool finger) {
    rec_.cancel();
    rec_.clear_events();
    finger_ = finger;
    scrolling_ = false;
    scrolled_ = false;
    denied_ = false;
    starting_ = false;
    rec_.down(0, x, y, t);
}

void ScrollDrag::move(float x, float y, double t) {
    if (rec_.touching()) {
        rec_.move(0, x, y, t);
    }
}

void ScrollDrag::release(float x, float y, double t) {
    if (rec_.touching()) {
        rec_.up(0, x, y, t);
    }
}

void ScrollDrag::cancel() {
    rec_.cancel();
    rec_.clear_events();
    scrolling_ = false;
    scrolled_ = false;
    denied_ = false;
}

ScrollDrag::Step ScrollDrag::update(double t, bool held_by_widget, float offset, float max_offset) {
    Step step;
    if (held_by_widget) {
        denied_ = true;
        scrolling_ = false;
    }
    rec_.update(t);
    // Copied out (the loop below may cancel the recognizer) into a buffer kept between frames, so a
    // drag or a fling allocates nothing once it is warm.
    events_.assign(rec_.events().begin(), rec_.events().end());
    rec_.clear_events();
    float target = offset;
    for (const GestureRecognizer::Event& e : events_) {
        switch (e.kind) {
        case Kind::DragStart:
            starting_ = true;
            break;
        case Kind::DragMove:
            if (starting_) {
                // The first move past the slop (it comes with the start): along this page, or
                // across it, where a page that locks its axis leaves the touch alone.
                starting_ = false;
                // A sideways page takes only a clearly sideways drag; one that scrolls down takes
                // the rest, so the two never both take a touch.
                const bool sideways =
                    decide_drag_axis(e.total_dx, e.total_dy) == DragAxis::Horizontal;
                if (lock_ && sideways != (axis_ == Axis::Horizontal)) {
                    denied_ = true;
                } else if (!denied_ && max_offset > 0.0f) {
                    scrolling_ = true;
                    scrolled_ = true;
                    step.took_touch = true;
                }
            }
            if (scrolling_) {
                target -= axis_ == Axis::Vertical ? e.dy : e.dx;
            }
            break;
        case Kind::DragEnd:
            scrolling_ = false;
            if (!finger_ || !scrolled_ || denied_) {
                rec_.cancel(); // nothing to carry on: a mouse, or a touch that did not scroll
                rec_.clear_events();
            }
            break;
        case Kind::Fling:
            if (finger_ && scrolled_ && !denied_) {
                target -= axis_ == Axis::Vertical ? e.dy : e.dx;
                if (target <= 0.0f || target >= max_offset) {
                    target = std::clamp(target, 0.0f, std::max(max_offset, 0.0f));
                    rec_.cancel(); // a page end stops it at once
                    rec_.clear_events();
                }
            }
            break;
        case Kind::Cancel:
            scrolling_ = false;
            break;
        case Kind::Press:
        case Kind::Tap:
        case Kind::DoubleTap:
        case Kind::LongPress:
        case Kind::PinchStart:
        case Kind::PinchMove:
        case Kind::PinchEnd:
        case Kind::FlingEnd:
        case Kind::ZoomFling:
        case Kind::HeldMove:
        case Kind::HeldEnd:
        case Kind::HeldRelease:
            break;
        }
    }
    step.scroll_by = target - offset;
    if (!busy()) {
        scrolling_ = false;
    }
    return step;
}

} // namespace etude
