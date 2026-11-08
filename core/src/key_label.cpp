// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/key_label.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

bool finite(float v) { return std::isfinite(v); }

bool overlaps(const RectF& a, const RectF& b) {
    return a.x < b.right() && b.x < a.right() && a.y < b.bottom() && b.y < a.bottom();
}

float approach(float value, float target, float step) {
    return value < target ? std::min(value + step, target) : std::max(value - step, target);
}

} // namespace

float key_label_font_px(float unit) {
    if (!finite(unit) || !(unit > 0.0f)) {
        return kMinKeyLabelFontPx;
    }
    return std::round(std::clamp(kKeyLabelFontPx * unit, kMinKeyLabelFontPx, kMaxKeyLabelFontPx));
}

KeyLabelPlacement place_key_label(const RectF& area, float width, float height, float margin,
                                  std::span<const RectF> avoid) {
    KeyLabelPlacement out;
    if (area.empty() || !finite(area.x) || !finite(area.y) || !finite(area.w) || !finite(area.h) ||
        !finite(width) || !finite(height) || !(width > 0.0f) || !(height > 0.0f) ||
        !finite(margin) || margin < 0.0f) {
        return out;
    }
    if (width + 2.0f * margin > area.w) {
        return out;
    }
    const float x = std::round(area.x + margin);
    float y = std::round(area.bottom() - margin - height);
    // Each pass moves the label above the first rect it touches; a rect it clears stays clear
    // because the label only goes up, so a few passes settle it.
    for (std::size_t pass = 0; pass <= avoid.size(); ++pass) {
        const RectF box{x, y, width, height};
        bool moved = false;
        for (const RectF& r : avoid) {
            if (!r.empty() && finite(r.x) && finite(r.y) && finite(r.w) && finite(r.h) &&
                overlaps(box, r)) {
                y = std::floor(r.y - kKeyLabelGapPx - height);
                moved = true;
                break;
            }
        }
        if (!moved) {
            break;
        }
    }
    const RectF box{x, y, width, height};
    if (y < area.y) {
        return out;
    }
    for (const RectF& r : avoid) {
        if (!r.empty() && finite(r.x) && finite(r.y) && finite(r.w) && finite(r.h) &&
            overlaps(box, r)) {
            return out;
        }
    }
    out.visible = true;
    out.rect = box;
    return out;
}

KeyLabelFade::Update KeyLabelFade::update(std::int64_t now_ns, bool shown, std::uint64_t id,
                                          bool behind, bool instant) {
    Update result;
    const std::int64_t dt = started_ ? std::max<std::int64_t>(now_ns - last_ns_, 0) : 0;
    last_ns_ = now_ns;
    started_ = true;

    if (shown && (!has_current_ || layers_[current_].id != id)) {
        if (has_current_) {
            current_ = 1 - current_;
        }
        has_current_ = true;
        // The slot keeps the presence it has: a label that came back, or a third key within the
        // fade, rises from where that slot stood instead of snapping to nothing first.
        layers_[current_].id = id;
        result.new_label = true;
        result.slot = current_;
    }

    const float step = instant ? 1.0f
                               : static_cast<float>(dt) / static_cast<float>(kKeyLabelFadeNs);
    for (int slot = 0; slot < 2; ++slot) {
        const bool is_current = has_current_ && slot == current_;
        layers_[slot].presence =
            std::clamp(approach(layers_[slot].presence, is_current && shown ? 1.0f : 0.0f, step),
                       0.0f, 1.0f);
    }
    const float dim_step =
        instant ? 1.0f : static_cast<float>(dt) / static_cast<float>(kKeyLabelDimNs);
    dim_ = std::clamp(approach(dim_, behind ? 1.0f : 0.0f, dim_step), 0.0f, 1.0f);
    return result;
}

float KeyLabelFade::opacity() const {
    return kKeyLabelOpacity * (1.0f - (1.0f - kKeyLabelBehindShare) * dim_);
}

void KeyLabelFade::reset() { *this = KeyLabelFade{}; }

} // namespace etude
