// SPDX-License-Identifier: GPL-3.0-or-later
// Where the accompaniment menu's card goes and in what shape, with no ImGui in it (accompaniment.cpp draws from it).
// The card is one column of rows: "All", one row per part, the volume. Where that column does not
// fit between the bar and the keyboard, the rows go in two columns (the first takes the larger
// half, so the volume ends the second) with a tighter padding; where even that is too tall, the
// padding above and below takes what is left, down to nothing. The rows are always a dead gap apart
// (`tok::kHitGap`: hit areas never touch). A card with so many parts that none of these fits
// scrolls with a drag, as the tempo card does (tempo_card.hpp). All lengths are in the units the caller draws in
// (ImGui points), `u` of them to a design point.
#pragma once

#include "tokens.hpp"

#include <algorithm>

namespace etude::app::ui {

// A column's width in the two-column card (design points): the two share what the window leaves,
// never narrower than `kAccompColMin`, where the volume slider keeps a track a finger can hold.
inline constexpr float kAccompColW = 272.0f;
inline constexpr float kAccompColMin = 208.0f;

struct AccompCardInput {
    float display_w = 0.0f;
    float keyboard_top = 0.0f; // the card ends a gap above this
    float below_y = 0.0f;      // under the bar and its band: where the card starts
    float u = 1.0f;
    int rows = 0; // All, the parts and the volume
};

struct AccompCardPlan {
    int columns = 1;
    int first_rows = 0; // rows in the first column; the rest are in the second
    float w = 0.0f;     // the card's width
    float col_w = 0.0f; // a column's width inside its padding
    float pad = 0.0f;   // the padding left and right of the columns
    float pad_y = 0.0f; // above and below them
    float row_gap = 0.0f; // between two rows of a column
    float col_gap = 0.0f; // between the two columns
    float h = 0.0f;     // the card's height, from its rows
    float max_h = 0.0f; // what the window leaves from the card's top
    bool fits() const { return h <= max_h + 0.01f; }
};

// A card whose tallest column has `rows` rows: each `tok::kHit` tall, `row_gap` between, `pad_y`
// above and below, and a pixel the columns keep for the rounding of their own height.
inline float accomp_card_height(int rows, float row_gap, float pad_y, float u) {
    return static_cast<float>(rows) * tok::kHit * u +
           static_cast<float>(std::max(rows - 1, 0)) * row_gap + 2.0f * pad_y + 1.0f;
}

inline AccompCardPlan plan_accomp_card(const AccompCardInput& in) {
    const float u = in.u;
    const float room_w = std::max(in.display_w - tok::kS5 * u, 0.0f);
    const float max_h = in.keyboard_top - in.below_y - tok::kS2 * u;

    // One column, as wide as it always was, with its usual padding.
    AccompCardPlan one;
    one.first_rows = in.rows;
    one.pad = one.pad_y = tok::kS2 * u;
    one.row_gap = tok::kHitGap * u;
    one.w = std::min(room_w, tok::kMenuW * u);
    one.col_w = one.w - 2.0f * one.pad;
    one.h = accomp_card_height(in.rows, one.row_gap, one.pad_y, u);
    one.max_h = max_h;
    if (one.fits() || in.rows < 2) {
        return one;
    }

    // Two columns share what the window leaves, each at most `kAccompColW`, never under
    // `kAccompColMin`.
    AccompCardPlan two;
    two.columns = 2;
    two.first_rows = (in.rows + 1) / 2;
    two.pad = two.pad_y = tok::kS2 * u;
    two.row_gap = tok::kHitGap * u;
    two.col_gap = tok::kS4 * u;
    two.col_w = std::min(kAccompColW * u, (room_w - 2.0f * two.pad - two.col_gap) * 0.5f);
    two.w = 2.0f * two.col_w + two.col_gap + 2.0f * two.pad;
    two.max_h = max_h;
    if (two.col_w < kAccompColMin * u) {
        return one;
    }
    two.h = accomp_card_height(two.first_rows, two.row_gap, two.pad_y, u);
    if (!two.fits()) {
        // The padding above and below takes the rest, to at most the usual.
        const float bare = accomp_card_height(two.first_rows, two.row_gap, 0.0f, u);
        two.pad_y = std::clamp((max_h - bare) * 0.5f, 0.0f, two.pad_y);
        two.h = accomp_card_height(two.first_rows, two.row_gap, two.pad_y, u);
    }
    return two;
}

} // namespace etude::app::ui
