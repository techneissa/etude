// SPDX-License-Identifier: GPL-3.0-or-later
// Where the tempo popover's card goes and in what shape, with no ImGui in it (tempo.cpp draws from it). The card is one
// column of rows: the readout, the speed slider, the presets, the typed tempo (while "set =" is on),
// the metronome (where the bar has no toggle of its own) and the count-in. Where that column does
// not fit between the bar and the keyboard, the card takes two columns (the speed controls, then
// the metronome and the count-in) with a tighter padding. The rows are always a dead gap apart
// (`tok::kHitGap`: their hit areas never touch): a card that fits neither of these scrolls with a
// drag. All lengths are in the units the caller draws in (ImGui points), `u` of them to a design
// point.
#pragma once

#include "tokens.hpp"

#include <algorithm>

namespace etude::app::ui {

// The single column's width, and a column's width in the two-column card (design points). The two
// columns share what the window leaves, never narrower than `kTempoColMin`: four presets of at
// least `tok::kHit` fit across a column from there.
inline constexpr float kTempoCardW = 360.0f;
inline constexpr float kTempoColW = 240.0f;
inline constexpr float kTempoColMin = 208.0f;

// The rows of the card's second column: the metronome (where the bar has no toggle), the Flow/Wait
// choice (where the bar has no chip) and the count-in, which is always there.
inline constexpr int tempo_right_rows(bool metronome_row, bool mode_row) {
    return (metronome_row ? 1 : 0) + (mode_row ? 1 : 0) + 1;
}

struct TempoCardInput {
    float display_w = 0.0f;
    float keyboard_top = 0.0f; // the card ends a gap above this
    float below_y = 0.0f;      // under the bar, its band and the lane: where the card starts
    float u = 1.0f;
    int left_rows = 3;  // the readout, the slider and the presets, and the typed tempo when on
    int right_rows = 2; // the metronome (when the bar has no toggle) and the count-in
    // Only the left rows are shown (the typed tempo's entry, with the number pad up): always one
    // column, its padding thinned where the room is short.
    bool single = false;
};

struct TempoCardPlan {
    bool two_columns = false;
    float w = 0.0f;     // the card's width
    float col_w = 0.0f; // a column's width inside its padding
    float pad = 0.0f;   // the padding left and right of the columns
    float pad_y = 0.0f; // above and below them
    float row_gap = 0.0f; // between two rows of a column
    float y = 0.0f;     // the card's top, before the slide-in
    float h = 0.0f;     // the card's height, from its rows
    float max_h = 0.0f; // what the window leaves from `y`
    bool fits() const { return h <= max_h + 0.01f; }
};

// A card of `rows` rows: each `tok::kHit` tall, `row_gap` between, `pad_y` above and below, and a
// pixel the columns keep for the rounding of their own height (tempo.cpp).
inline float tempo_card_height(int rows, float row_gap, float pad_y, float u) {
    return static_cast<float>(rows) * tok::kHit * u +
           static_cast<float>(std::max(rows - 1, 0)) * row_gap + 2.0f * pad_y + 1.0f;
}

inline TempoCardPlan plan_tempo_card(const TempoCardInput& in) {
    const float u = in.u;
    const float room_w = std::max(in.display_w - tok::kS5 * u, 0.0f);
    const float max_h = in.keyboard_top - in.below_y - tok::kS2 * u;

    // One column, as wide as it always was, with its usual padding.
    TempoCardPlan one;
    one.pad = one.pad_y = tok::kS3 * u;
    one.row_gap = tok::kHitGap * u;
    one.w = std::min(room_w, kTempoCardW * u);
    one.col_w = one.w - 2.0f * one.pad;
    one.y = in.below_y;
    one.h = tempo_card_height(in.left_rows + in.right_rows, one.row_gap, one.pad_y, u);
    one.max_h = max_h;
    if (one.fits()) {
        return one;
    }
    if (in.single) {
        // The padding is thinned where the room is short; the rows keep their gap, and what still
        // does not fit scrolls.
        const int rows = in.left_rows + in.right_rows;
        const float bare = tempo_card_height(rows, one.row_gap, 0.0f, u);
        one.pad_y = std::clamp((max_h - bare) * 0.5f, 0.0f, one.pad_y);
        one.h = tempo_card_height(rows, one.row_gap, one.pad_y, u);
        return one;
    }

    // Two columns share what the window leaves, each at most `kTempoColW`, never under
    // `kTempoColMin`; the padding is tighter so the card is no taller than its three rows.
    TempoCardPlan two;
    two.two_columns = true;
    two.pad = two.pad_y = tok::kS2 * u;
    two.row_gap = tok::kHitGap * u;
    const float col_gap = tok::kS4 * u;
    two.col_w = std::min(kTempoColW * u, (room_w - 2.0f * two.pad - col_gap) * 0.5f);
    two.w = 2.0f * two.col_w + col_gap + 2.0f * two.pad;
    two.y = in.below_y;
    const int rows = std::max(in.left_rows, in.right_rows);
    two.h = tempo_card_height(rows, two.row_gap, two.pad_y, u);
    two.max_h = max_h;
    if (two.col_w < kTempoColMin * u) {
        return one;
    }
    if (!two.fits()) {
        // The padding above and below takes what is left, to at most the usual; the rows keep
        // their gap, and what still does not fit scrolls.
        const float bare = tempo_card_height(rows, two.row_gap, 0.0f, u);
        two.pad_y = std::clamp((max_h - bare) * 0.5f, 0.0f, two.pad_y);
        two.h = tempo_card_height(rows, two.row_gap, two.pad_y, u);
    }
    return two;
}

} // namespace etude::app::ui
