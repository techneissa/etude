// SPDX-License-Identifier: GPL-3.0-or-later
// Two small motions for icons that are not path morphs, both driven by the springs of
// icon_morph.hpp and free of allocation:
// - TurnIcon: one glyph that turns to an angle (a chevron opening and closing is the same glyph
//   rotated, never a deformed one).
// - RollIcon: a number that rolls like a counter. The old value slides out and fades while the new
//   one slides in from the direction of change and fades in, so every frame shows whole numbers
//   or a crossfade of two.
#pragma once

#include "etude/icon_morph.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude {

// The most a retargeted spring's progress speed is rescaled to. A retarget that lands close to the
// new target has a tiny span, so the same angular or positional speed is a large progress speed;
// this only stops a span of almost nothing from running away.
inline constexpr float kRetargetMaxSpeed = 8.0f * kSpringMaxSpeed;

// An angle that eases to its target on a spring.
class TurnIcon {
  public:
    // At `angle` (radians) at once: the first state, or reduced effects.
    void set(float angle) {
        from_ = to_ = angle;
        spring_ = {};
        moving_ = false;
    }
    // Turns to `angle` from wherever it is, keeping the angular speed: the spring's velocity is in
    // progress units, so it is rescaled from the old span to the new one (a turn reversed in
    // flight carries on the way it was going before the spring pulls it back). The same target
    // changes nothing.
    void turn_to(float angle, Spring spring = kSpringSmooth) {
        if (angle == to_) {
            return;
        }
        const float here = this->angle();
        const float span = angle - here;
        float v = 0.0f;
        if (moving_ && span != 0.0f) {
            v = std::clamp(spring_.v * (to_ - from_) / span, -kRetargetMaxSpeed, kRetargetMaxSpeed);
        }
        from_ = here;
        to_ = angle;
        params_ = spring;
        spring_ = {0.0f, v};
        moving_ = true;
    }
    // `animate` false is set(), true is turn_to().
    void go(float angle, bool animate, Spring spring = kSpringSmooth) {
        if (animate) {
            turn_to(angle, spring);
        } else if (angle != to_ || moving_) {
            set(angle);
        }
    }
    // Advances the spring. Returns true while it moves.
    bool step(float dt) {
        if (!moving_) {
            return false;
        }
        if (step_spring(spring_, params_, dt)) {
            from_ = to_;
            moving_ = false;
            return false;
        }
        return true;
    }

    bool moving() const { return moving_; }
    // At rest: exactly the target.
    float angle() const { return moving_ ? from_ + (to_ - from_) * spring_.x : to_; }
    // Radians per second.
    float velocity() const { return moving_ ? (to_ - from_) * spring_.v : 0.0f; }

  private:
    float from_ = 0.0f;
    float to_ = 0.0f;
    SpringState spring_;
    Spring params_ = kSpringSmooth;
    bool moving_ = false;
};

inline constexpr int kRollLayers = 3; // a roll cut short by the next leaves at most this many

// A whole number that rolls to the next one. A layer's offset is in units of the travel: 0 is
// where a number rests, +1 one travel below (y grows downward), −1 one above. An increasing value
// moves the numbers up: the new one rises from below while the old one leaves through the top;
// a decreasing value does the reverse.
class RollIcon {
  public:
    struct Layer {
        int value = 0;
        float from = 0.0f; // offset when it started
        float to = 0.0f;   // 0 for the shown number, ±1 for one leaving
        SpringState spring;
    };

    // Shows `value` at once: the first state, or reduced effects.
    void set(int value) {
        layers_[0] = {value, 0.0f, 0.0f, {}};
        count_ = 1;
        moving_ = false;
    }
    // Rolls to `value`. A roll in flight sends what is on its way in out again from where it is.
    // The same value changes nothing.
    void roll_to(int value, Spring spring = kSpringSmooth) {
        if (count_ == 0) {
            set(value);
            return;
        }
        if (value == this->value()) {
            return;
        }
        const float dir = value > this->value() ? 1.0f : -1.0f;
        for (int i = 0; i < count_; ++i) {
            Layer& l = layers_[static_cast<std::size_t>(i)];
            if (l.to == 0.0f) { // the shown number leaves, the way the new one pushes it
                // Keep its speed in offsets per second: rescale the velocity (in progress units)
                // from the span it had to the span it has now.
                const float here = offset(i);
                const float span = -dir - here;
                const float v = span != 0.0f ? l.spring.v * (l.to - l.from) / span : 0.0f;
                l.from = here;
                l.to = -dir;
                l.spring.x = 0.0f;
                l.spring.v = std::clamp(v, -kRetargetMaxSpeed, kRetargetMaxSpeed);
            }
        }
        if (count_ == kRollLayers) { // drop the one furthest gone
            int worst = 0;
            for (int i = 1; i < count_; ++i) {
                if (std::fabs(offset(i)) > std::fabs(offset(worst))) {
                    worst = i;
                }
            }
            layers_[static_cast<std::size_t>(worst)] = layers_[static_cast<std::size_t>(--count_)];
        }
        layers_[static_cast<std::size_t>(count_++)] = {value, dir, 0.0f, SpringState{0.0f, 0.0f}};
        params_ = spring;
        moving_ = true;
    }
    // `animate` false is set(), true is roll_to().
    void go(int value, bool animate, Spring spring = kSpringSmooth) {
        if (animate) {
            roll_to(value, spring);
        } else if (count_ == 0 || this->value() != value || moving_) {
            set(value);
        }
    }
    // Advances the springs. Returns true while anything moves.
    bool step(float dt) {
        if (!moving_) {
            return false;
        }
        bool any = false;
        for (int i = 0; i < count_;) {
            Layer& l = layers_[static_cast<std::size_t>(i)];
            // At rest is exactly x = 1 and v = 0, which step_spring sets on settling; a spring that
            // overshot (x past 1) is still moving.
            bool rest = l.spring.x == 1.0f && l.spring.v == 0.0f;
            if (!rest && step_spring(l.spring, params_, dt)) {
                rest = true;
                if (l.to != 0.0f) { // one that has left is gone
                    l = layers_[static_cast<std::size_t>(--count_)];
                    continue;
                }
            }
            any = any || !rest;
            ++i;
        }
        moving_ = any;
        return any;
    }

    bool moving() const { return moving_; }
    // The number that shows at rest: the last one rolled to.
    int value() const {
        for (int i = count_ - 1; i >= 0; --i) {
            if (layers_[static_cast<std::size_t>(i)].to == 0.0f) {
                return layers_[static_cast<std::size_t>(i)].value;
            }
        }
        return 0;
    }
    int count() const { return count_; }
    int layer_value(int i) const { return layers_[static_cast<std::size_t>(i)].value; }
    float offset(int i) const {
        const Layer& l = layers_[static_cast<std::size_t>(i)];
        return l.from + (l.to - l.from) * l.spring.x;
    }
    // Offsets per second.
    float velocity(int i) const {
        const Layer& l = layers_[static_cast<std::size_t>(i)];
        return (l.to - l.from) * l.spring.v;
    }
    // 1 at rest, falling to 0 as the number moves a whole travel away, faster than the distance so
    // that the two numbers rarely show together: one is clearly the stronger at every moment, and
    // halfway both are an eighth.
    float alpha(int i) const {
        const float a = std::clamp(1.0f - std::fabs(offset(i)), 0.0f, 1.0f);
        return a * a * a;
    }

  private:
    std::array<Layer, kRollLayers> layers_{};
    int count_ = 0;
    Spring params_ = kSpringSmooth;
    bool moving_ = false;
};

} // namespace etude
