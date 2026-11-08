// SPDX-License-Identifier: GPL-3.0-or-later
#include "panel_scale.hpp"

#include "etude/session_state.hpp"

#include <algorithm>

namespace etude::app {
namespace {

float output_px_per_mm(const PanelMm& panel, const DisplayPixels& display, bool headless,
                       float out_w, float out_h, float win_density) {
    if (!panel.known()) {
        return 0.0f;
    }
    if (headless || !(display.points_w > 0.0f)) {
        return panel_px_per_mm(out_w, out_h, panel); // the window stands for the whole panel
    }
    // The display's points per millimetre, times the window's output pixels per point.
    const float density = win_density > 0.0f ? win_density : 1.0f;
    return display.points_w / panel.w * density;
}

} // namespace

float PanelScale::px_per_mm(float out_w, float out_h, float win_density, int setting_width_mm,
                            float taste) const {
    PanelMm panel = mm;
    // A headless window stands for the panel the flag names: the setting is for a real display.
    if (!headless && setting_width_mm >= kMinScreenWidthMm) {
        const float px_w = !headless && display.w > 0.0f ? display.w : out_w;
        const float px_h = !headless && display.h > 0.0f ? display.h : out_h;
        panel = panel_mm_from_width(static_cast<float>(setting_width_mm), px_w, px_h);
    }
    float base = output_px_per_mm(panel, display, headless, out_w, out_h, win_density);
    if (!(base > 0.0f)) {
        base = (win_density > 0.0f ? win_density : 1.0f) * kFallbackPxPerMm;
    }
    return base * std::max(1.0f, taste);
}

} // namespace etude::app
