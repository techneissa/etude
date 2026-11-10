// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/touch_gesture.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

using Kind = GestureRecognizer::Event::Kind;

namespace {

float dist(float ax, float ay, float bx, float by) { return std::hypot(ax - bx, ay - by); }

} // namespace

int GestureRecognizer::find(std::int64_t id) const {
    for (int i = 0; i < count_; ++i) {
        if (fingers_[static_cast<std::size_t>(i)].id == id) {
            return i;
        }
    }
    return -1;
}

float GestureRecognizer::distance() const {
    return dist(fingers_[0].x, fingers_[0].y, fingers_[1].x, fingers_[1].y);
}

void GestureRecognizer::stop_momentum() {
    if (state_ == State::Fling || state_ == State::ZoomFling) {
        push({Kind::FlingEnd});
        state_ = State::Idle;
    }
}

void GestureRecognizer::add_sample(double t, float x, float y) {
    if (sample_count_ == samples_.size()) {
        std::copy(samples_.begin() + 1, samples_.end(), samples_.begin());
        --sample_count_;
    }
    samples_[sample_count_++] = {t, x, y};
}

// The speed over the last velocity window; zero when nothing moved in it.
void GestureRecognizer::velocity(double t, float& vx, float& vy) const {
    vx = vy = 0.0f;
    if (sample_count_ < 2) {
        return;
    }
    const Sample& last = samples_[sample_count_ - 1];
    // The finger had stopped before it lifted.
    if (t - last.t > config_.velocity_window_s) {
        return;
    }
    std::size_t first = sample_count_ - 1;
    while (first > 0 && last.t - samples_[first - 1].t <= config_.velocity_window_s) {
        --first;
    }
    const Sample& a = samples_[first];
    const double dt = last.t - a.t;
    if (dt <= 1e-6) {
        return;
    }
    vx = static_cast<float>((last.x - a.x) / dt);
    vy = static_cast<float>((last.y - a.y) / dt);
}

void GestureRecognizer::down(std::int64_t id, float x, float y, double t, bool other_fingers_down) {
    stop_momentum();
    // A finger cannot land twice, and a lone finger cannot land beside one that is still held: the
    // lift of the old touch never came. Drop it, so this one is a clean first finger.
    if (count_ > 0 && (find(id) >= 0 || !other_fingers_down)) {
        cancel();
    }
    if (count_ >= 2) {
        return; // a third finger is ignored
    }
    fingers_[static_cast<std::size_t>(count_++)] = {id, x, y};
    if (count_ == 1) {
        state_ = State::Pressed;
        held_engaged_ = false;
        origin_x_ = last_x_ = x;
        origin_y_ = last_y_ = y;
        down_t_ = t;
        sample_count_ = 0;
        add_sample(t, x, y);
        Event e{Kind::Press};
        e.x = x;
        e.y = y;
        push(e);
        return;
    }
    // A second finger: the first one's gesture ends, a pinch begins.
    if (state_ == State::Dragging) {
        Event e{Kind::DragEnd};
        e.x = last_x_;
        e.y = last_y_;
        push(e);
    } else if (state_ == State::Pressed || state_ == State::Held) {
        push({Kind::Cancel});
    }
    if (state_ == State::Spent) {
        return;
    }
    state_ = State::Pinching;
    last_distance_ = distance();
    last_cx_ = (fingers_[0].x + fingers_[1].x) * 0.5f;
    last_cy_ = (fingers_[0].y + fingers_[1].y) * 0.5f;
    pinch_rate_ = 0.0;
    pinch_t_ = t;
    Event e{Kind::PinchStart};
    e.x = last_cx_;
    e.y = last_cy_;
    push(e);
}

void GestureRecognizer::move(std::int64_t id, float x, float y, double t) {
    const int i = find(id);
    if (i < 0) {
        return;
    }
    fingers_[static_cast<std::size_t>(i)].x = x;
    fingers_[static_cast<std::size_t>(i)].y = y;
    switch (state_) {
    case State::Pressed:
        if (dist(x, y, origin_x_, origin_y_) > config_.drag_threshold) {
            state_ = State::Dragging;
            Event s{Kind::DragStart};
            s.x = origin_x_;
            s.y = origin_y_;
            push(s);
            last_x_ = origin_x_;
            last_y_ = origin_y_;
        } else {
            add_sample(t, x, y);
            break;
        }
        [[fallthrough]];
    case State::Dragging: {
        Event e{Kind::DragMove};
        e.x = x;
        e.y = y;
        e.dx = x - last_x_;
        e.dy = y - last_y_;
        e.total_dx = x - origin_x_;
        e.total_dy = y - origin_y_;
        push(e);
        last_x_ = x;
        last_y_ = y;
        add_sample(t, x, y);
        break;
    }
    case State::Pinching: {
        const float d = distance();
        const float cx = (fingers_[0].x + fingers_[1].x) * 0.5f;
        const float cy = (fingers_[0].y + fingers_[1].y) * 0.5f;
        Event e{Kind::PinchMove};
        e.x = cx;
        e.y = cy;
        e.dx = cx - last_cx_;
        e.dy = cy - last_cy_;
        if (last_distance_ > 1.0f && d > 1.0f) {
            e.factor = static_cast<double>(d / last_distance_);
            const double dt = t - pinch_t_;
            if (dt > 1e-6) {
                // Smoothed over a few moves so one jittery sample does not decide the momentum.
                pinch_rate_ = 0.6 * pinch_rate_ + 0.4 * std::log(e.factor) / dt;
            }
        }
        push(e);
        last_distance_ = d;
        last_cx_ = cx;
        last_cy_ = cy;
        pinch_t_ = t;
        break;
    }
    case State::Held:
        // Engaged once the held finger leaves the slop: a tremor moves nothing, and HeldMoves
        // start from the move that crosses it.
        if (config_.drag_after_long_press && count_ == 1 && !held_engaged_ &&
            dist(x, y, origin_x_, origin_y_) > config_.drag_threshold) {
            held_engaged_ = true;
        }
        if (config_.drag_after_long_press && count_ == 1 && held_engaged_) {
            Event e{Kind::HeldMove};
            e.x = x;
            e.y = y;
            e.total_dx = x - origin_x_;
            e.total_dy = y - origin_y_;
            push(e);
        }
        break;
    case State::Idle:
    case State::Spent:
    case State::Fling:
    case State::ZoomFling:
        break;
    }
}

void GestureRecognizer::up(std::int64_t id, float x, float y, double t) {
    const int i = find(id);
    if (i < 0) {
        return;
    }
    fingers_[static_cast<std::size_t>(i)] = fingers_[static_cast<std::size_t>(count_ - 1)];
    --count_;
    switch (state_) {
    case State::Pressed: {
        if (have_tap_ && t - tap_t_ <= config_.double_tap_s &&
            dist(x, y, tap_x_, tap_y_) <= config_.double_tap_distance) {
            Event e{Kind::DoubleTap};
            e.x = x;
            e.y = y;
            push(e);
            have_tap_ = false;
        } else {
            Event e{Kind::Tap};
            e.x = x;
            e.y = y;
            push(e);
            have_tap_ = true;
            tap_t_ = t;
            tap_x_ = x;
            tap_y_ = y;
        }
        state_ = State::Idle;
        break;
    }
    case State::Held:
        if (config_.drag_after_long_press) {
            Event e{held_engaged_ ? Kind::HeldEnd : Kind::HeldRelease};
            e.x = x;
            e.y = y;
            push(e);
        }
        state_ = State::Idle;
        break;
    case State::Dragging: {
        float vx = 0.0f;
        float vy = 0.0f;
        add_sample(t, x, y);
        velocity(t, vx, vy);
        Event e{Kind::DragEnd};
        e.x = x;
        e.y = y;
        e.vx = vx;
        e.vy = vy;
        push(e);
        if (std::hypot(vx, vy) >= config_.fling_min_speed) {
            state_ = State::Fling;
            fling_vx_ = vx;
            fling_vy_ = vy;
            fling_t_ = t;
        } else {
            state_ = State::Idle;
        }
        break;
    }
    case State::Pinching: {
        Event e{Kind::PinchEnd};
        e.x = last_cx_;
        e.y = last_cy_;
        push(e);
        // The finger still down does nothing until it lifts too.
        const bool fast = std::fabs(pinch_rate_) >= config_.zoom_fling_min_rate &&
                          t - pinch_t_ <= config_.velocity_window_s;
        if (count_ > 0) {
            state_ = State::Spent;
        } else {
            state_ = State::Idle;
        }
        if (fast) {
            // The pinch coasts on; the finger still down (if any) does nothing more.
            zoom_rate_ = pinch_rate_;
            zoom_x_ = last_cx_;
            zoom_y_ = last_cy_;
            fling_t_ = t;
            state_ = State::ZoomFling;
        }
        break;
    }
    case State::Spent:
        if (count_ == 0) {
            state_ = State::Idle;
        }
        break;
    case State::Idle:
    case State::Fling:
    case State::ZoomFling:
        break;
    }
}

void GestureRecognizer::update(double t) {
    switch (state_) {
    case State::Pressed:
        if (config_.long_press && t - down_t_ >= config_.long_press_s) {
            Event e{Kind::LongPress};
            e.x = last_x_;
            e.y = last_y_;
            push(e);
            state_ = State::Held;
        }
        break;
    case State::Fling: {
        const double dt = std::max(0.0, t - fling_t_);
        if (dt <= 0.0) {
            break;
        }
        const double half = config_.fling_half_life_s;
        const double decay = std::pow(0.5, dt / half);
        // The distance covered while the speed falls from v to v * decay.
        const double travelled = half / std::log(2.0) * (1.0 - decay);
        Event e{Kind::Fling};
        e.dx = static_cast<float>(fling_vx_ * travelled);
        e.dy = static_cast<float>(fling_vy_ * travelled);
        push(e);
        fling_vx_ = static_cast<float>(fling_vx_ * decay);
        fling_vy_ = static_cast<float>(fling_vy_ * decay);
        fling_t_ = t;
        if (std::hypot(fling_vx_, fling_vy_) < config_.fling_stop_speed) {
            push({Kind::FlingEnd});
            state_ = State::Idle;
        }
        break;
    }
    case State::ZoomFling: {
        const double dt = std::max(0.0, t - fling_t_);
        if (dt <= 0.0) {
            break;
        }
        const double half = config_.fling_half_life_s;
        const double decay = std::pow(0.5, dt / half);
        const double e_folds = zoom_rate_ * half / std::log(2.0) * (1.0 - decay);
        Event e{Kind::ZoomFling};
        e.x = zoom_x_;
        e.y = zoom_y_;
        e.factor = std::exp(e_folds);
        push(e);
        zoom_rate_ *= decay;
        fling_t_ = t;
        if (std::fabs(zoom_rate_) < config_.zoom_fling_stop_rate) {
            push({Kind::FlingEnd});
            state_ = State::Idle;
        }
        break;
    }
    case State::Idle:
    case State::Held:
    case State::Dragging:
    case State::Pinching:
    case State::Spent:
        break;
    }
}

void GestureRecognizer::long_press_now(float x, float y, double /*t*/) {
    if (state_ == State::Pressed) {
        Event e{Kind::LongPress};
        e.x = x;
        e.y = y;
        push(e);
        state_ = State::Held;
    } else if (state_ == State::Idle) {
        Event e{Kind::LongPress};
        e.x = x;
        e.y = y;
        push(e);
    }
}

void GestureRecognizer::cancel() {
    if (state_ == State::Pressed || state_ == State::Held || state_ == State::Dragging ||
        state_ == State::Pinching) {
        push({Kind::Cancel});
    }
    stop_momentum();
    state_ = State::Idle;
    count_ = 0;
}

float GestureRecognizer::long_press_progress(double t) const {
    if (state_ != State::Pressed || !config_.long_press) {
        return 0.0f;
    }
    return static_cast<float>(std::clamp((t - down_t_) / config_.long_press_s, 0.0, 1.0));
}

namespace {

// The swipe a drag that began at `start_y` and has moved `total_dy` up or down is going toward, by
// zone and direction alone: a downward one that began in the top row reveals the hidden bar, an
// upward one that began on the shown bar hides it.
BarSwipe intended_swipe(const BarSwipeArea& area, float start_y, float total_dy) {
    if (!std::isfinite(start_y) || !std::isfinite(total_dy)) {
        return BarSwipe::None;
    }
    if (!area.bar_shown) {
        return total_dy > 0.0f && start_y < area.row_bottom ? BarSwipe::Reveal : BarSwipe::None;
    }
    return total_dy < 0.0f && start_y < area.bar_bottom ? BarSwipe::Hide : BarSwipe::None;
}

float travel_of(const BarSwipeArea& area) {
    return kBarSwipeTravelPt * (area.unit > 0.0f ? area.unit : 1.0f);
}

} // namespace

DragAxis decide_drag_axis(float total_dx, float total_dy) {
    if (!std::isfinite(total_dx) || !std::isfinite(total_dy) ||
        (total_dx == 0.0f && total_dy == 0.0f)) {
        return DragAxis::Undecided;
    }
    return std::fabs(total_dy) >= kDragAxisRatio * std::fabs(total_dx) ? DragAxis::Vertical
                                                                       : DragAxis::Horizontal;
}

BarSwipe BarSwipeTracker::on_event(const GestureRecognizer::Event& event,
                                   const BarSwipeArea& area) {
    switch (event.kind) {
    case Kind::DragStart:
        dragging_ = true;
        done_ = false;
        claimed_ = false;
        axis_ = DragAxis::Undecided;
        start_y_ = event.y;
        return BarSwipe::None;
    case Kind::DragMove: {
        if (!dragging_ || done_) {
            return BarSwipe::None;
        }
        if (axis_ == DragAxis::Undecided) {
            axis_ = decide_drag_axis(event.total_dx, event.total_dy); // once, at the slop
        }
        if (axis_ != DragAxis::Vertical) {
            return BarSwipe::None;
        }
        const BarSwipe intent = intended_swipe(area, start_y_, event.total_dy);
        if (intent == BarSwipe::None) {
            return BarSwipe::None;
        }
        if (std::fabs(event.total_dy) >= travel_of(area)) {
            done_ = true;
            return intent;
        }
        if (!claimed_) {
            claimed_ = true;
            return BarSwipe::Claim;
        }
        return BarSwipe::None;
    }
    case Kind::DragEnd:
    case Kind::Cancel:
    case Kind::PinchStart:
        dragging_ = false;
        done_ = false;
        claimed_ = false;
        axis_ = DragAxis::Undecided;
        return BarSwipe::None;
    case Kind::Press:
    case Kind::Tap:
    case Kind::DoubleTap:
    case Kind::LongPress:
    case Kind::PinchMove:
    case Kind::PinchEnd:
    case Kind::Fling:
    case Kind::ZoomFling:
    case Kind::FlingEnd:
    case Kind::HeldMove:
    case Kind::HeldEnd:
    case Kind::HeldRelease:
        break;
    }
    return BarSwipe::None;
}

} // namespace etude
