// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/touch_gesture.hpp"

#include <cstddef>

namespace etude {

// A section's boundary handle taken by a long press, as a pure state machine fed the recogniser's
// events (the recogniser runs with `drag_after_long_press`). The hold takes the handle; the finger
// then moves it (HeldMove), and the lift commits it (HeldEnd) or, when the finger never left the
// slop, lets it go unchanged (HeldRelease). A hold never edits anything by itself: the only edit
// is the commit of a move, so a slip of a hold is not a destructive one.
struct HandleGrabStep {
    enum class Kind : unsigned char {
        None,     // nothing for the caller to do
        Moved,    // the handle is now at `time_s` (show it there)
        Commit,   // lifted after a move: put the handle at `time_s`
        Released, // lifted without moving: the handle stays where it was
    };
    Kind kind = Kind::None;
    std::size_t handle = 0;
    double time_s = 0.0;
};

class HandleGrab {
  public:
    // The hold took handle `handle`, standing at `marker_s`, with the finger on song time `finger_s`.
    void grab(std::size_t handle, double marker_s, double finger_s) {
        active_ = true;
        moved_ = false;
        handle_ = handle;
        orig_s_ = at_s_ = marker_s;
        grab_s_ = finger_s;
    }
    bool active() const { return active_; }
    bool moved() const { return moved_; }
    std::size_t handle() const { return handle_; }
    // Where the handle is now (its own place until it was moved).
    double time_s() const { return at_s_; }
    void cancel() { active_ = false; }

    // Feeds one event. `finger_s` is the song time under the event's point; `place` turns the time
    // the handle would sit at, following the finger, into the time it may sit at (snapped to the
    // grid and kept between its neighbours). Events of a touch that did not take a handle, and a
    // Cancel or a new Press, end the grab with nothing done.
    template <class Place>
    HandleGrabStep on_event(const GestureRecognizer::Event& e, double finger_s, Place&& place) {
        using Kind = GestureRecognizer::Event::Kind;
        HandleGrabStep out;
        out.handle = handle_;
        switch (e.kind) {
        case Kind::HeldMove:
            if (active_) {
                moved_ = true;
                at_s_ = place(orig_s_ + (finger_s - grab_s_));
                out.kind = HandleGrabStep::Kind::Moved;
            }
            break;
        case Kind::HeldEnd:
            if (active_) {
                out.kind = moved_ ? HandleGrabStep::Kind::Commit : HandleGrabStep::Kind::Released;
                active_ = false;
            }
            break;
        case Kind::HeldRelease:
            if (active_) {
                out.kind = HandleGrabStep::Kind::Released;
                active_ = false;
            }
            break;
        case Kind::Press:
        case Kind::Cancel:
            active_ = false;
            break;
        default:
            break;
        }
        out.time_s = at_s_;
        return out;
    }

  private:
    bool active_ = false;
    bool moved_ = false;
    std::size_t handle_ = 0;
    double orig_s_ = 0.0;
    double at_s_ = 0.0;
    double grab_s_ = 0.0;
};

} // namespace etude
