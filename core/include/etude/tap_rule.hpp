// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "etude/touch_gesture.hpp"

namespace etude {

// The one rule for a tap on a control, shared by every screen. A control that changes anything acts
// when the finger (or the mouse button) is released, never when it lands, so a swipe or a brush
// that starts on it does nothing.
struct TapRuleConfig {
    float slop = kTouchSlopPt;         // in the caller's units (kTouchSlopPt times its unit)
    double long_press_s = kLongPressS; // a press this long is a hold, not a tap
    double repeat_s = 0.0; // a hold repeats every this long after the long-press time; 0: never

    // The rule for a screen drawn at `unit` (ImGui points per design point): the slop grows with it.
    static TapRuleConfig scaled(float unit, double repeat = 0.0) {
        TapRuleConfig c;
        c.slop = kTouchSlopPt * unit;
        c.repeat_s = repeat;
        return c;
    }
};

// Whether a move of (`dx`, `dy`) has left the slop. Exactly the slop is still within it.
inline bool exceeds_slop(float dx, float dy, float slop) { return std::hypot(dx, dy) > slop; }

// How many repeats a hold completes between `before_s` and `now_s` of holding: the first when the
// hold reaches the long-press time, then one each `repeat_s`.
inline int hold_repeats(double before_s, double now_s, const TapRuleConfig& c) {
    if (!(c.repeat_s > 0.0)) {
        return 0;
    }
    const auto index = [&](double t) {
        return t < c.long_press_s - 1e-9
                   ? -1
                   : static_cast<int>(std::floor((t - c.long_press_s) / c.repeat_s + 1e-9));
    };
    return std::max(0, index(now_s) - index(before_s));
}

// One press of the pointer, from landing to lifting, as a pure state machine fed the pointer's
// position and the time. A tap is a press that was lifted, stayed within the slop for its whole
// length, was not cancelled, and was shorter than the long-press time. `cancel` is for whatever
// takes the touch away: a drag claim (the bar's swipe, a scroll) or a second finger.
class TapTracker {
  public:
    // A pointer landed at (`x`, `y`) at time `t`: a new press, whatever came before.
    void down(float x, float y, double t) {
        const std::uint64_t next = serial_ + 1;
        *this = TapTracker{};
        serial_ = next;
        down_ = true;
        x0_ = x;
        y0_ = y;
        t0_ = t_ = prev_t_ = t;
    }
    // The pointer is at (`x`, `y`) at time `t`, still down. Once it has left the slop it stays a
    // drag.
    void move(float x, float y, double t) {
        if (!down_) {
            return;
        }
        prev_t_ = t_;
        t_ = std::max(t_, t);
        travel_ = std::max(travel_, std::hypot(x - x0_, y - y0_));
    }
    // The pointer lifted at (`x`, `y`) at time `t`.
    void up(float x, float y, double t) {
        if (!down_) {
            return;
        }
        move(x, y, t);
        down_ = false;
        lifted_ = true;
    }
    // The touch is no longer a tap or a hold: a drag claim took it, or a second finger landed.
    void cancel() { cancelled_ = true; }

    // Counts presses: the number of the press in force, or the last one.
    std::uint64_t serial() const { return serial_; }
    bool pressed() const { return down_; }
    bool lifted() const { return lifted_; }
    bool cancelled() const { return cancelled_; }
    float start_x() const { return x0_; }
    float start_y() const { return y0_; }
    double held_s() const { return t_ - t0_; }

    // The press just lifted counts as a tap.
    bool tap(const TapRuleConfig& c) const {
        return lifted_ && !cancelled_ && travel_ <= c.slop && held_s() < c.long_press_s;
    }
    // The press is down, still, and has been held to the long-press time.
    bool holding(const TapRuleConfig& c) const {
        return down_ && !cancelled_ && travel_ <= c.slop && held_s() >= c.long_press_s - 1e-9;
    }
    // The repeats a hold completed since the last `move`; none once the press has left the slop,
    // been cancelled or lifted.
    int repeats(const TapRuleConfig& c) const {
        if (!down_ || cancelled_ || travel_ > c.slop) {
            return 0;
        }
        return hold_repeats(prev_t_ - t0_, t_ - t0_, c);
    }

  private:
    std::uint64_t serial_ = 0;
    bool down_ = false;
    bool lifted_ = false;
    bool cancelled_ = false;
    float x0_ = 0.0f;
    float y0_ = 0.0f;
    float travel_ = 0.0f;
    double t0_ = 0.0;
    double t_ = 0.0;
    double prev_t_ = 0.0;
};

// When an overlay (a popover, a card) may be closed by a tap outside it. The release that opened
// it is the end of a press that began outside it (on its opener), so only a press that began
// after it opened may close it.
class OutsideClose {
  public:
    // Once a frame, with whether the overlay is shown and the number of the press in force.
    void update(bool shown, std::uint64_t press) {
        if (shown && !shown_) {
            armed_ = press;
        }
        shown_ = shown;
    }
    bool may_close(std::uint64_t press) const { return shown_ && press > armed_; }

  private:
    bool shown_ = false;
    std::uint64_t armed_ = 0;
};

} // namespace etude
