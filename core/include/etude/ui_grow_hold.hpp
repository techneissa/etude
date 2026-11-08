// SPDX-License-Identifier: GPL-3.0-or-later
// The UI's growth (`ui_window_grow`) held while the window is being resized. Every new growth is
// a new UI scale, and every new scale bakes the UI's text at new sizes into the font atlas and
// lays every page out again; a maximise or a drag of the window's edge passes through dozens of
// sizes. So the UI keeps the growth it had until the size has been still for `kUiGrowSettleNs`,
// and takes the new one in a single step. Everything else (the keyboard, the notes, the layout's
// rects) follows the new size at once; only the scale of the controls waits.
#pragma once

#include "etude/screen_layout.hpp"

#include <cmath>
#include <cstdint>

namespace etude {

// How long the window's size must stay the same before the UI takes the growth it gives.
inline constexpr std::int64_t kUiGrowSettleNs = 120'000'000;

class UiGrowHold {
  public:
    // The growth to draw with for a window `width_px` x `height_px` at `px_per_mm` at `now_ns`:
    // the window's own on the first call, after a change of `px_per_mm` (a scale chosen, not a
    // resize), and once the size has been still for `kUiGrowSettleNs`; the one held before while
    // it keeps changing. A size that is no size (a minimised window's 0x0, or not a number)
    // changes nothing: the growth held before stays, and a restore to the same size keeps it.
    float grow(float width_px, float height_px, float px_per_mm, std::int64_t now_ns) {
        const float want = ui_window_grow(width_px, height_px, px_per_mm);
        if (!(std::isfinite(width_px) && width_px > 0.0f && std::isfinite(height_px) &&
              height_px > 0.0f)) {
            return held_ > 0.0f ? held_ : want;
        }
        if (held_ <= 0.0f || px_per_mm != px_per_mm_) {
            held_ = want;
            width_ = width_px;
            height_ = height_px;
            px_per_mm_ = px_per_mm;
            changed_ns_ = now_ns;
            return held_;
        }
        if (width_px != width_ || height_px != height_) {
            width_ = width_px;
            height_ = height_px;
            changed_ns_ = now_ns;
        }
        if (now_ns - changed_ns_ >= kUiGrowSettleNs) {
            held_ = want;
        }
        return held_;
    }

  private:
    float held_ = 0.0f;
    float width_ = 0.0f;
    float height_ = 0.0f;
    float px_per_mm_ = 0.0f;
    std::int64_t changed_ns_ = 0;
};

} // namespace etude
