// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "etude/panel_size.hpp"

namespace etude {

// The touch timings and distances every screen shares. A touch that stays within the slop is a
// tap, not a drag; held still for the long-press time it is a long press; after a finger event the
// mouse is ignored for the ignore window (a touch also arrives as a synthesised mouse).
// The slop is `kTapSlopMm` of real screen, rounded up to a whole design point (a design point is
// 1/96 inch at the panel's real size): 10 points, 2.65 mm.
inline constexpr float kTouchSlopPt = pt_from_mm_up(kTapSlopMm); // design points
inline constexpr double kLongPressS = 0.45;             // seconds
inline constexpr double kIgnoreMouseAfterFingerS = 0.6; // seconds

// Turns timestamped touch points into the gestures a touch screen needs: tap, double tap, long
// press, drag, two-finger pinch, and the momentum that carries a drag or a pinch on after the
// fingers lift. A mouse feeds it the same way (one finger, id 0). It has no clock, no screen and no
// SDL: every call carries the time in seconds and positions are in the caller's units (points),
// so the same input always gives the same events.
//
// Rules:
// - A touch that stays within `drag_threshold` of where it began is never a drag, so a tap never
//   nudges anything. Press is reported at once (for instant feedback); Tap only when the finger
//   lifts without having moved past the threshold or been held for `long_press_s`.
// - Held still for `long_press_s` (when `long_press` is on): one LongPress, and the lift is not a
//   Tap. Moving afterwards does nothing more, unless `drag_after_long_press` is on: once the held
//   finger moves past `drag_threshold` from where it went down, that move and each one after it is
//   a HeldMove at the finger, and the lift a HeldEnd (a press-and-hold that then drags, as a sheet
//   music loop is drawn, or a section handle is moved; a right click does the same through
//   long_press_now()). A held finger that never leaves the threshold lifts with a HeldRelease
//   instead, so whatever the hold took hold of can be let go of.
// - A second Tap within `double_tap_s` and `double_tap_distance` of the first is a DoubleTap in
//   its place (the first was already a Tap).
// - Past the threshold: DragStart at the point where the touch began, then DragMove with the whole
//   move so far. DragEnd carries the velocity at the lift, measured over the last
//   `velocity_window_s` (zero when the finger had stopped).
// - A finger that cannot be new means its lift was lost, and the touch is dropped at once (Cancel
//   for one in progress) and the new finger starts a clean press: a DOWN for a finger already held,
//   or one the caller knows lands with no other finger on the screen (`down`'s last argument) while
//   the recogniser still holds some. Without it every touch after a lost lift would count as a
//   second finger, or as a move of the finger that is long gone.
// - A second finger turns the gesture into a pinch: Cancel (or DragEnd) for what the first finger
//   was doing, PinchStart, PinchMove with the change of distance as a factor (more than 1 spreads)
//   and the movement of the midpoint, PinchEnd when either finger lifts. The finger left down does
//   nothing until all are up. A third finger is ignored.
// - After a fast drag lifts, update() produces Fling events whose speed halves every
//   `fling_half_life_s` (an ease-out) until it is under `fling_stop_speed`, then FlingEnd. After a
//   fast pinch it produces ZoomFling the same way. A new touch stops either at once (FlingEnd).
class GestureRecognizer {
  public:
    struct Config {
        float drag_threshold = kTouchSlopPt; // points
        bool long_press = true;              // false: holding still never becomes a long press
        bool drag_after_long_press = false;  // true: moves after a long press are HeldMoves
        double long_press_s = kLongPressS;
        double double_tap_s = 0.30;
        float double_tap_distance = 32.0f; // points between the two taps
        double velocity_window_s = 0.08;
        float fling_min_speed = 150.0f; // points per second a lifted drag needs
        float fling_stop_speed = 20.0f;
        double fling_half_life_s = 0.22;
        double zoom_fling_min_rate = 0.6; // e-folds of scale per second a lifted pinch needs
        double zoom_fling_stop_rate = 0.08;
    };

    struct Event {
        enum class Kind : std::uint8_t {
            Press, // a touch began at (x, y)
            Tap,
            DoubleTap,
            LongPress,
            DragStart,  // (x, y) is where the touch began
            DragMove,   // (dx, dy) since the last one, (total_dx, total_dy) since the start
            DragEnd,    // (vx, vy) at the lift, points per second
            PinchStart, // (x, y) the midpoint
            PinchMove,  // factor, and the midpoint's move (dx, dy)
            PinchEnd,
            Cancel,      // the touch is no longer a gesture (a second finger came, or cancel())
            Fling,       // (dx, dy) moved since the last one
            ZoomFling,   // factor around (x, y)
            FlingEnd,    // the momentum ran out, or a touch stopped it
            HeldMove,    // after a long press (drag_after_long_press): the finger is at (x, y)
            HeldEnd,     // and lifted at (x, y)
            HeldRelease, // a held finger lifted at (x, y) without leaving the slop
                         // (drag_after_long_press)
        };
        Kind kind = Kind::Press;
        float x = 0.0f;
        float y = 0.0f;
        float dx = 0.0f;
        float dy = 0.0f;
        float total_dx = 0.0f;
        float total_dy = 0.0f;
        float vx = 0.0f;
        float vy = 0.0f;
        double factor = 1.0;
    };

    GestureRecognizer() = default;
    explicit GestureRecognizer(const Config& config) : config_(config) {}
    void set_config(const Config& config) { config_ = config; }
    const Config& config() const { return config_; }

    // A finger landed. `other_fingers_down` is what the caller knows of the screen: false when no
    // other finger is on it (then a recogniser that still holds one lost its lift, see above).
    // A mouse, or a caller that does not know, leaves it true.
    void down(std::int64_t id, float x, float y, double t, bool other_fingers_down = true);
    void move(std::int64_t id, float x, float y, double t);
    void up(std::int64_t id, float x, float y, double t);
    // Time passes: a still touch may become a long press, momentum moves on.
    void update(double t);
    // A right click, or any "long press now": acts like the hold finishing, at (x, y).
    void long_press_now(float x, float y, double t);
    // Drops whatever is in progress (a Cancel for a touch in progress) and any momentum.
    void cancel();

    // Events since the last clear_events(), in order.
    std::span<const Event> events() const { return events_; }
    void clear_events() { events_.clear(); }

    // 0..1 while a touch is held still toward a long press, else 0.
    float long_press_progress(double t) const;
    bool touching() const {
        return state_ == State::Pressed || state_ == State::Held || state_ == State::Dragging;
    }
    bool pinching() const { return state_ == State::Pinching; }
    bool flinging() const { return state_ == State::Fling || state_ == State::ZoomFling; }
    int fingers() const { return count_; }

  private:
    enum class State : std::uint8_t {
        Idle,
        Pressed,
        Held,
        Dragging,
        Pinching,
        Spent,
        Fling,
        ZoomFling
    };
    struct Finger {
        std::int64_t id = 0;
        float x = 0.0f;
        float y = 0.0f;
    };
    struct Sample {
        double t = 0.0;
        float x = 0.0f;
        float y = 0.0f;
    };

    void push(const Event& e) { events_.push_back(e); }
    void stop_momentum();
    void add_sample(double t, float x, float y);
    void velocity(double t, float& vx, float& vy) const;
    float distance() const;
    int find(std::int64_t id) const;

    Config config_;
    std::vector<Event> events_;
    State state_ = State::Idle;
    std::array<Finger, 2> fingers_{};
    int count_ = 0;

    // The single touch.
    bool held_engaged_ = false; // a held touch moved past the slop (drag_after_long_press)
    float origin_x_ = 0.0f;
    float origin_y_ = 0.0f;
    float last_x_ = 0.0f;
    float last_y_ = 0.0f;
    double down_t_ = 0.0;
    std::array<Sample, 16> samples_{};
    std::size_t sample_count_ = 0;

    // The last tap, for a double tap.
    bool have_tap_ = false;
    double tap_t_ = 0.0;
    float tap_x_ = 0.0f;
    float tap_y_ = 0.0f;

    // The pinch.
    float last_distance_ = 0.0f;
    float last_cx_ = 0.0f;
    float last_cy_ = 0.0f;
    double pinch_rate_ = 0.0; // e-folds of scale per second, smoothed
    double pinch_t_ = 0.0;

    // Momentum.
    double fling_t_ = 0.0;
    float fling_vx_ = 0.0f;
    float fling_vy_ = 0.0f;
    double zoom_rate_ = 0.0;
    float zoom_x_ = 0.0f;
    float zoom_y_ = 0.0f;
};

// Which way a drag goes, decided once, when it first passes the touch slop, and never again: a
// drag that starts mostly vertical stays a vertical one however it drifts later, and the other way
// round. Vertical when the move up or down is at least `kDragAxisRatio` times the sideways move
// (exactly at the ratio is vertical); a diagonal is horizontal. Whatever a horizontal drag does
// on a control (the song overview scrubs) a vertical one never starts it.
inline constexpr float kDragAxisRatio = 1.5f;

enum class DragAxis : std::uint8_t {
    Undecided, // no move yet, or a bad number
    Horizontal,
    Vertical,
};

DragAxis decide_drag_axis(float total_dx, float total_dy);

// The control bar's two swipes. A downward swipe that begins in the top row (the screen's top
// edge, where the faint grab handle is) brings the hidden bar in; an upward swipe that begins on
// the shown bar sends it away. A touch that begins there and is vertical at the touch slop
// (`decide_drag_axis`) is the bar's from that moment: the tracker reports `Claim` so the control
// under the finger (the song overview's scrub, its long press, a button's tap) lets go of the
// touch and nothing seeks or opens. The swipe itself is reported, once, at `kBarSwipeTravelPt` of
// travel (design points, scaled by `BarSwipeArea::unit`) in the right direction. A touch that is
// horizontal at the slop is never a bar swipe, and a second finger ends the swipe: a pinch is not
// one. A tap is not a swipe: it is under the drag threshold, so the recogniser never starts a drag
// for it.
inline constexpr float kBarSwipeTravelPt = 24.0f;

enum class BarSwipe : std::uint8_t {
    None,
    Claim,  // this touch is a bar swipe: the control under it must let go
    Reveal, // the hidden bar should come in (a claim too, if none was reported)
    Hide,   // the shown bar should go (likewise)
};

// Where the bar is, in the caller's units (the recogniser's).
struct BarSwipeArea {
    bool bar_shown = false;
    float row_bottom = 0.0f; // the bottom of the top row: a reveal swipe begins above it
    float bar_bottom = 0.0f; // the bottom of the shown bar with its overview: a hide swipe begins
                             // above it
    float unit = 1.0f;       // units per design point
};

// What a drag judged from where it began and its totals alone is (a flick, or a test): the axis
// from the totals, then the zone, direction and travel. Never `Claim`. Bad numbers (NaN) are never
// a swipe.
BarSwipe classify_bar_swipe(const BarSwipeArea& area, float start_y, float total_dx,
                            float total_dy);

// Reads a recogniser's events for the bar's swipes, one drag at a time. It decides the axis at the
// drag's first move, reports `Claim` once for a vertical drag that began where a swipe can, then
// the swipe once at its travel. A pinch, a cancel or the end of the drag forgets the drag. Feed it
// every event the recogniser produced, in order.
class BarSwipeTracker {
  public:
    BarSwipe on_event(const GestureRecognizer::Event& event, const BarSwipeArea& area);

  private:
    bool dragging_ = false;
    bool done_ = false;
    bool claimed_ = false;
    DragAxis axis_ = DragAxis::Undecided;
    float start_y_ = 0.0f;
};

} // namespace etude
