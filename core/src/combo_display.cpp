// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/combo_display.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

constexpr float kPi = 3.14159265358979f;

float clamp01(float v) { return std::clamp(v, 0.0f, 1.0f); }
// Fast at first, settling at the end: a digit that arrives.
float ease_out(float p) {
    const float q = 1.0f - clamp01(p);
    return 1.0f - q * q * q;
}
float smooth(float p) {
    const float q = clamp01(p);
    return q * q * (3.0f - 2.0f * q);
}

int digit_count(int n) {
    int count = 0;
    for (; n > 0; n /= 10) {
        ++count;
    }
    return count;
}

// The digit of `n` `from_right` places from the ones (0 is the ones), or -1 past its length.
int digit_of(int n, int from_right) {
    for (int i = 0; i < from_right; ++i) {
        n /= 10;
    }
    return n > 0 ? n % 10 : -1;
}

} // namespace

void ComboDisplay::reset() { *this = ComboDisplay{}; }

void ComboDisplay::update(double dt_s, int combo, bool milestone, bool reduced,
                          std::uint32_t epoch) {
    if (epoch != epoch_) {
        *this = ComboDisplay{};
        epoch_ = epoch;
    }
    // A long frame (a hitch, a resize) must not skip an animation: it plays for the time it was
    // seen.
    const float dt = static_cast<float>(std::clamp(dt_s, 0.0, 0.1));
    const int shown = std::clamp(combo, 0, kComboMaxShown);
    if (reduced) {
        value_ = shown >= kComboShowAt ? shown : 0;
        prev_ = 0;
        enter_ = 1.0f;
        pulse_ = 1.0f;
        alpha_ = value_ > 0 ? 1.0f : 0.0f;
        lost_ = false;
        build_view(true);
        return;
    }

    if (shown >= kComboShowAt) {
        if (value_ != shown) {
            // A number replacing another eases the digits that differ; one that comes back after
            // a loss, or appears at the threshold, brings all of its digits in.
            prev_ = (value_ == 0 || lost_) ? 0 : value_;
            value_ = shown;
            enter_ = 0.0f;
        }
        lost_ = false;
        alpha_ = std::min(1.0f, alpha_ + dt / kComboFadeInS);
        if (milestone) {
            pulse_ = 0.0f;
        }
    } else if (value_ != 0) {
        // Lost: the last number stays where it is and fades out, with no motion.
        lost_ = true;
        alpha_ = std::max(0.0f, alpha_ - dt / kComboFadeOutS);
        if (alpha_ <= 0.0f) {
            value_ = 0;
            prev_ = 0;
            lost_ = false;
        }
    }
    enter_ = std::min(1.0f, enter_ + dt / kComboEnterS);
    pulse_ = std::min(1.0f, pulse_ + dt / kComboPulseS);

    build_view(false);
}

void ComboDisplay::build_view(bool reduced) {
    ComboView v;
    v.reduced = reduced;
    if (value_ > 0) {
        v.alpha = reduced ? 1.0f : smooth(alpha_);
        if (pulse_ < 1.0f) {
            const float s = std::sin(kPi * pulse_);
            v.scale = 1.0f + kComboPulseGrow * s;
            v.glow = s;
        }
        const int n = std::min(digit_count(value_), kComboMaxDigits);
        v.columns = n;
        for (int c = 0; c < n; ++c) {
            const int from_right = n - 1 - c;
            const int now = digit_of(value_, from_right);
            const int before = digit_of(prev_, from_right);
            ComboColumn& col = v.column[static_cast<std::size_t>(c)];
            if (enter_ >= 1.0f || before == now) {
                col.now = {now, 1.0f, 0.0f};
                continue;
            }
            const float e = ease_out(enter_);
            col.now = {now, e, (1.0f - e) * kComboSlide};
            if (before >= 0) {
                col.out = {before, 1.0f - e, -e * kComboSlide};
            }
        }
    }
    view_ = v;
}

RectF place_combo(const RectF& note_area, float top_clear, float u, float width, float height,
                  const RectF& keep_out, float right_clear) {
    if (note_area.empty() || !(u > 0.0f) || !(width > 0.0f) || !(height > 0.0f)) {
        return {};
    }
    const float inset = std::max(kComboMarginPt * u, right_clear + kComboGapPt * u);
    RectF box{note_area.right() - inset - width,
              note_area.y + std::max(top_clear, 0.0f) + kComboTopPt * u, width, height};
    if (!keep_out.empty()) {
        const float gap = kComboGapPt * u;
        const bool touches = box.x < keep_out.right() + gap && box.right() > keep_out.x - gap &&
                             box.y < keep_out.bottom() + gap && box.bottom() > keep_out.y - gap;
        if (touches) {
            box.y = keep_out.bottom() + gap;
        }
    }
    if (box.x < note_area.x || box.bottom() > note_area.bottom()) {
        return {};
    }
    return box;
}

} // namespace etude
