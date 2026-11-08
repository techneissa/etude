// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/count_band.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

// The height a number of `font` pixels takes: its line, a little over the font size.
constexpr float kNumberLineShare = 1.15f;

float band_height(float font, float unit) {
    return kCountPadPt * unit + font * kNumberLineShare + kCountPadPt * unit + kCountDotPt * unit +
           kCountPadPt * unit;
}

} // namespace

CountBand place_count_band(const RectF& area, float unit, float clear_to) {
    CountBand out;
    if (area.empty() || !std::isfinite(unit) || !(unit > 0.0f) || !std::isfinite(area.h) ||
        !std::isfinite(area.w)) {
        return out;
    }
    // Where the number and the dots may start: the area's top, or the line to keep clear above.
    const float top = std::isfinite(clear_to) ? std::max(area.y, clear_to) : area.y;
    if (top - area.y >= area.h) {
        return out;
    }
    const float max_h = kCountBandMaxShare * area.h;
    const float fixed = band_height(0.0f, unit);
    // The largest font the share allows, then the wanted one within the limits.
    const float room = (max_h - fixed) / kNumberLineShare;
    const float min_font = kCountMinFontPt * unit;
    if (room < min_font) {
        return out;
    }
    const float font = std::floor(std::min(
        {std::clamp(area.h * kCountFontShare, min_font, kCountMaxFontPt * unit), room}));
    const float h = std::ceil(top - area.y + band_height(font, unit));
    if (h > area.h) {
        return out;
    }
    out.band = {area.x, area.y, area.w, h};
    out.font_px = font;
    out.number_cx = area.x + area.w * 0.5f;
    out.number_cy = top + kCountPadPt * unit + font * kNumberLineShare * 0.5f;
    out.dot_px = kCountDotPt * unit;
    out.dot_gap_px = kCountDotGapPt * unit;
    out.dots_cy = top + kCountPadPt * unit + font * kNumberLineShare + kCountPadPt * unit +
                  out.dot_px * 0.5f;
    return out;
}

} // namespace etude
