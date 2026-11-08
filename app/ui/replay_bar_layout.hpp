// SPDX-License-Identifier: GPL-3.0-or-later
// Where the replay's controls go: five icon controls and the speed chip in the top row, and the run
// bar, its time and five tools in the band's row under it. Pure geometry from the two rows, in
// ImGui points, so the layout tests check every size without ImGui.
#pragma once

#include "tokens.hpp"

#include "etude/screen_layout.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace replay_tok {
inline constexpr float kSpeedW = 80.0f;  // the speed chip (a hit tall, a face inside it)
inline constexpr float kTimeW = 64.0f;   // "0:09 / 2:20"
inline constexpr float kPad = tok::kS4;  // the band's inset at its left
inline constexpr int kCells = 5;         // restart, back, play, forward, sound
inline constexpr int kTools = 5;         // previous mistake, next mistake, summary, pin, practise
} // namespace replay_tok

struct ReplayBarLayout {
    std::array<RectF, replay_tok::kCells> cells{}; // restart, back, play, forward, sound
    RectF speed;
    RectF track;
    RectF time;
    std::array<RectF, replay_tok::kTools> tools{};
    bool fits = false; // the track keeps at least a hit's width
};

// The width the top row's controls take, in points at the UI unit `u`: every control a hit area and
// a dead gap (`tok::kHitGap`) from the next.
inline float replay_bar_width(float u) {
    return u * (static_cast<float>(replay_tok::kCells) * tok::kCell + replay_tok::kSpeedW);
}

// `bar` is the part of the top row the controls may use (from `split_top_row`), `band` the row
// under it, both in points; `u` the UI unit.
inline ReplayBarLayout replay_bar_layout(const RectF& bar, const RectF& band, float u) {
    ReplayBarLayout l;
    const float hit = tok::kHit * u;
    const float gap = tok::kHitGap * u;
    // The top row's controls are a hit tall, centred in the row; the band's, a hit tall on its
    // lower edge: the two rows' hit areas keep their dead gap.
    const float top_h = std::min(bar.h, hit);
    const float top_y = bar.y + (bar.h - top_h) * 0.5f;
    float x = bar.x;
    for (std::size_t i = 0; i < l.cells.size(); ++i) {
        l.cells[i] = {x, top_y, hit, top_h};
        x += hit + gap;
    }
    l.speed = {x, top_y, replay_tok::kSpeedW * u, top_h};
    const float pad = replay_tok::kPad * u;
    const float band_h = std::min(band.h, hit);
    const float band_y = band.bottom() - band_h;
    float right = band.right() - tok::kS2 * u;
    for (std::size_t i = l.tools.size(); i-- > 0;) {
        l.tools[i] = {right - hit, band_y, hit, band_h};
        right -= hit + gap;
    }
    l.time = {right - replay_tok::kTimeW * u, band_y, replay_tok::kTimeW * u, band_h};
    const float track_right = l.time.x - gap;
    l.track = {band.x + pad, band_y, std::max(0.0f, track_right - band.x - pad), band_h};
    l.fits = l.track.w >= hit;
    return l;
}

} // namespace etude::app::ui
