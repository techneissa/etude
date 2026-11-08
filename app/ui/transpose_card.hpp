// SPDX-License-Identifier: GPL-3.0-or-later
// Where the transposition card's rows go, with no ImGui in it so tests/transpose_card_test.cpp
// checks the real numbers (transpose.cpp draws from them). Both cards are a stack of rows of
// `tok::kHit`: the transposition card (opened from the tempo popover) holds the stepper (with Keys
// and Reset beside it) and the three whole-octave moves, the offer card holds what it proposes and
// its answers; either grows by the fit strip when it is shown. All lengths are in the units the
// caller draws in (ImGui points), `u` of them to a design point.
#pragma once

#include "tokens.hpp"

#include <algorithm>

namespace etude::app::ui {

// Bars of the strip: the current shift and each whole-octave move that stays within -12..+12.
inline constexpr int kStripMaxBars = 3;

inline float strip_height(int bars, float u) {
    return tok::kStripKeys * u +
           static_cast<float>(std::clamp(bars, 0, kStripMaxBars)) * (tok::kStripRow + tok::kS1) *
               u +
           tok::kS1 * u;
}

struct TransposeCardPlan;
// Where even the usual spacing does not fit, the padding above and below takes what is left, down
// to nothing (the rows keep their dead gap, `tok::kHitGap`: hit areas never touch); a card that
// still does not fit scrolls with a drag.
inline void tighten(TransposeCardPlan& p, int rows, float u);

struct TransposeCardPlan {
    float w = 0.0f;
    float pad = 0.0f;
    float gap = 0.0f;   // between two rows
    float h = 0.0f;     // the card's height
    float max_h = 0.0f; // what the window leaves under the bar
    bool fits() const { return h <= max_h + 0.01f; }
};

inline void tighten(TransposeCardPlan& p, [[maybe_unused]] int rows, [[maybe_unused]] float u) {
    if (p.fits()) {
        return;
    }
    const float bare = p.h - 2.0f * p.pad;
    const float pad = std::clamp((p.max_h - bare) * 0.5f, 0.0f, p.pad);
    p.h = bare + 2.0f * pad;
    p.pad = pad;
}

// The transposition card: the stepper with Keys and Reset beside it, the octave moves, then the
// strip.
inline TransposeCardPlan plan_transpose_card(float display_w, float below_y, float keyboard_top,
                                             float u, bool strip, int bars) {
    TransposeCardPlan p;
    p.pad = tok::kS2 * u;
    p.gap = tok::kHitGap * u;
    p.w = std::min(tok::kTransposeW * u, std::max(display_w - tok::kS5 * u, 0.0f));
    constexpr int rows = 2;
    p.h = 2.0f * p.pad + static_cast<float>(rows) * tok::kHit * u +
          static_cast<float>(rows - 1) * p.gap;
    if (strip) {
        p.h += p.gap + strip_height(bars, u);
    }
    p.max_h = keyboard_top - below_y - tok::kS2 * u;
    tighten(p, rows, u);
    return p;
}

// The offer card: what it proposes (with the strip toggle), then its answers in `answer_rows` rows
// (one when the three chips share a line, two when they wrap), then the strip. `step` is
// `tok::wide_step` of the window (the popup rule): a wider card with taller rows.
inline TransposeCardPlan plan_offer_card(float display_w, float below_y, float keyboard_top,
                                         float u, int answer_rows, bool strip, int step = 0) {
    TransposeCardPlan p;
    p.pad = tok::kS2 * u;
    p.gap = tok::kHitGap * u;
    p.w = std::min(tok::popup_width(tok::kOfferW, step, display_w / u) * u,
                   std::max(display_w - tok::kS5 * u, 0.0f));
    const int rows = 1 + std::max(answer_rows, 1);
    p.h = 2.0f * p.pad + static_cast<float>(rows) * tok::kHit * tok::popup_grow(step) * u +
          static_cast<float>(rows - 1) * p.gap;
    if (strip) {
        p.h += p.gap + strip_height(2, u);
    }
    p.max_h = keyboard_top - below_y - tok::kS2 * u;
    tighten(p, rows, u);
    return p;
}

} // namespace etude::app::ui
