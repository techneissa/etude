// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/square_drag.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

SquarePos square_pos_at(SquareArea area, float x, float y) {
    const auto along = [](float lo, float hi, float v) {
        const float len = hi - lo;
        if (!(len > 0.0f) || !std::isfinite(v)) {
            return 0.0f;
        }
        return std::clamp((v - lo) / len, 0.0f, 1.0f);
    };
    return {along(area.x0, area.x1, x), along(area.y0, area.y1, y)};
}

std::optional<SquarePos> SquareDrag::press(SliderPointer pointer, float x, float y,
                                           SquareArea area, SquarePos value, float thumb_reach) {
    pointer_ = pointer;
    origin_x_ = x;
    origin_y_ = y;
    origin_value_ = {std::clamp(value.x, 0.0f, 1.0f), std::clamp(value.y, 0.0f, 1.0f)};
    const float thumb_x = area.x0 + (area.x1 - area.x0) * origin_value_.x;
    const float thumb_y = area.y0 + (area.y1 - area.y0) * origin_value_.y;
    on_thumb_ = std::hypot(x - thumb_x, y - thumb_y) <= thumb_reach;
    if (pointer == SliderPointer::Mouse) {
        mode_ = SliderMode::Drag;
        origin_value_ = square_pos_at(area, x, y);
        return origin_value_;
    }
    mode_ = SliderMode::Undecided;
    return std::nullopt;
}

std::optional<SquarePos> SquareDrag::move(float x, float y, SquareArea area, float slop) {
    if (mode_ == SliderMode::Undecided) {
        if (std::hypot(x - origin_x_, y - origin_y_) <= slop) {
            return std::nullopt;
        }
        mode_ = SliderMode::Drag;
    }
    if (mode_ != SliderMode::Drag) {
        return std::nullopt;
    }
    if (pointer_ == SliderPointer::Mouse) {
        return square_pos_at(area, x, y);
    }
    const float w = area.x1 - area.x0;
    const float h = area.y1 - area.y0;
    if (!(w > 0.0f) || !(h > 0.0f)) {
        return std::nullopt;
    }
    return SquarePos{std::clamp(origin_value_.x + (x - origin_x_) / w, 0.0f, 1.0f),
                     std::clamp(origin_value_.y + (y - origin_y_) / h, 0.0f, 1.0f)};
}

std::optional<SquarePos> SquareDrag::release(float x, float y, SquareArea area, bool cancelled) {
    const SliderMode was = mode_;
    mode_ = SliderMode::Idle;
    if (cancelled || was != SliderMode::Undecided || on_thumb_) {
        return std::nullopt;
    }
    return square_pos_at(area, x, y);
}

} // namespace etude
