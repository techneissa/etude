// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/slider_drag.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

float slider_value_at(SliderTrack track, float x) {
    const float w = track.x1 - track.x0;
    if (!(w > 0.0f) || !std::isfinite(x)) {
        return 0.0f;
    }
    return std::clamp((x - track.x0) / w, 0.0f, 1.0f);
}

std::optional<float> SliderDrag::press(SliderPointer pointer, float x, float y, SliderTrack track,
                                       float value, float thumb_reach) {
    pointer_ = pointer;
    origin_x_ = x;
    origin_y_ = y;
    origin_value_ = std::clamp(value, 0.0f, 1.0f);
    const float thumb_x = track.x0 + (track.x1 - track.x0) * origin_value_;
    on_thumb_ = std::fabs(x - thumb_x) <= thumb_reach;
    if (pointer == SliderPointer::Mouse) {
        mode_ = SliderMode::Drag;
        origin_value_ = slider_value_at(track, x);
        return origin_value_;
    }
    mode_ = SliderMode::Undecided;
    return std::nullopt;
}

std::optional<float> SliderDrag::move(float x, float y, SliderTrack track, float slop) {
    if (mode_ == SliderMode::Undecided) {
        const float dx = x - origin_x_;
        const float dy = y - origin_y_;
        if (std::hypot(dx, dy) <= slop) {
            return std::nullopt;
        }
        mode_ = std::fabs(dy) > std::fabs(dx) ? SliderMode::Scroll : SliderMode::Drag;
    }
    if (mode_ != SliderMode::Drag) {
        return std::nullopt;
    }
    if (pointer_ == SliderPointer::Mouse) {
        return slider_value_at(track, x);
    }
    const float w = track.x1 - track.x0;
    if (!(w > 0.0f)) {
        return std::nullopt;
    }
    return std::clamp(origin_value_ + (x - origin_x_) / w, 0.0f, 1.0f);
}

std::optional<float> SliderDrag::release(float x, SliderTrack track, bool cancelled) {
    const SliderMode was = mode_;
    mode_ = SliderMode::Idle;
    if (cancelled || was != SliderMode::Undecided || on_thumb_) {
        return std::nullopt;
    }
    return slider_value_at(track, x);
}

} // namespace etude
