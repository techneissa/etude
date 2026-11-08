// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/top_indicator.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

namespace {

bool good(float v) { return std::isfinite(v) && v > 0.0f; }

} // namespace

float top_indicator_width(float font, int dots) {
    if (!good(font)) {
        return 0.0f;
    }
    if (dots <= 0 || dots > kMaxIndicatorDots) {
        return dots <= 0 ? std::round(font * kIndicatorRingEm)
                         : std::round(font * kIndicatorNumberEm);
    }
    const float d = std::round(font * kIndicatorDotEm);
    const float g = std::round(font * kIndicatorDotGapEm);
    const float gap = std::round(font * kIndicatorNumberGapEm);
    return static_cast<float>(dots) * d + static_cast<float>(dots - 1) * g + gap +
           std::round(font * kIndicatorNumberEm);
}

TopIndicator place_top_indicator(const RectF& status_area, float font, int dots) {
    TopIndicator out;
    if (status_area.empty() || !good(font) || !std::isfinite(status_area.x) ||
        !std::isfinite(status_area.y)) {
        return out;
    }
    const float w = top_indicator_width(font, dots);
    const bool ring = dots <= 0;
    const float h = ring ? std::round(font * kIndicatorRingEm) : std::round(font * 1.2f);
    if (!(w > 0.0f) || w > status_area.w || h > status_area.h) {
        return out;
    }
    out.rect = RectF{status_area.right() - w, status_area.y + std::round((status_area.h - h) * 0.5f),
                     w, h};
    if (ring) {
        out.ring = h;
        out.number = out.rect;
        return out;
    }
    if (dots <= kMaxIndicatorDots) {
        out.dot = std::round(font * kIndicatorDotEm);
        out.dot_gap = std::round(font * kIndicatorDotGapEm);
    }
    const float slot = std::round(font * kIndicatorNumberEm);
    out.number = RectF{out.rect.right() - slot, out.rect.y, slot, h};
    return out;
}

} // namespace etude
