// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of a drill's "How it went" card go (#373): pure geometry from the note area and the
// UI unit, so the layout tests check every size without ImGui. The card is centred in the note area,
// never reaches the keyboard, and has two forms: with its head (the words "How it went", the drill's
// title and the Done check) over the figure row and the buttons, or, where the area is too short for
// that, the figure row and the buttons alone. Everything a finger may touch is at least a hit.
#pragma once

#include "tokens.hpp"

#include "etude/screen_layout.hpp"

#include <algorithm>

namespace etude::app::ui {

namespace drill_card_tok {
inline constexpr float kWidth = 560.0f;      // the card across at most, design points
inline constexpr float kFigureH = 64.0f;     // the figure row: the accuracy, the notes, the timing
inline constexpr float kCompactFigureH = 44.0f;
inline constexpr float kFigureW = 120.0f;    // the accuracy figure's room (the "100%" at kFigureSize)
inline constexpr float kTimingW = 190.0f;    // the timing's room at the row's right, at most
inline constexpr float kTimingMinW = 120.0f; // under this the row has no timing
inline constexpr float kAgainShare = 1.4f;   // Again's width over each other button's
inline constexpr float kFullH = 200.0f;      // the card with its head, about, for how much room it grows into
inline constexpr float kGrowRoom = 1.4f;     // it grows only where the area holds this many times its size
inline constexpr float kMaxGrow = 1.5f;
} // namespace drill_card_tok

struct DrillCardLayout {
    bool fits = false;
    bool compact = false; // no head
    RectF card;
    RectF head;           // empty in the compact form
    RectF title;          // "How it went" over the drill's title and tempo
    RectF done;           // the check at the head's right
    RectF figure;         // the accuracy
    RectF notes;          // "14 of 15 notes" over how it compares with last time
    RectF timing;         // the timing track with its Early and Late counts (empty when no room)
    RectF again;
    RectF faster;
    RectF to_card;
};

// The unit the card is drawn in: the window's, grown (up to `kMaxGrow`) where the note area holds more
// than the card at the plain unit, so a big window shows a card as readable as a small one.
inline float drill_card_unit(const RectF& area, float u) {
    using namespace drill_card_tok;
    const float across = area.w / (kWidth * u * kGrowRoom);
    const float down = area.h / (kFullH * u * kGrowRoom);
    return u * std::clamp(std::min(across, down), 1.0f, kMaxGrow);
}

inline DrillCardLayout drill_card_layout(const RectF& area, float u) {
    using namespace drill_card_tok;
    DrillCardLayout l;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    const float pad = tok::kS4 * u;
    const float margin = tok::kS2 * u;
    const float width = std::min(area.w - 2.0f * margin, kWidth * u);
    if (width < 3.0f * hit + 2.0f * gap + 2.0f * pad) {
        return l;
    }
    const float full_h = hit + gap + kFigureH * u + gap + hit + 2.0f * pad;
    const float compact_h = kCompactFigureH * u + gap + hit + 2.0f * pad;
    l.compact = area.h - 2.0f * margin < full_h;
    const float fig_h = (l.compact ? kCompactFigureH : kFigureH) * u;
    const float height = l.compact ? compact_h : full_h;
    if (area.h - 2.0f * margin < height) {
        return l;
    }
    l.card = {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
    const float x0 = l.card.x + pad;
    const float x1 = l.card.right() - pad;
    float y = l.card.y + pad;
    if (!l.compact) {
        l.head = {x0, y, x1 - x0, hit};
        l.done = {x1 - hit, y, hit, hit};
        l.title = {x0, y, x1 - hit - gap - x0, hit};
        y += hit + gap;
    }
    // The figure row: the accuracy, then the notes' words, then (where there is room) the timing.
    const float timing_w = std::min(kTimingW * u, (x1 - x0) * 0.4f);
    const bool timing = timing_w >= kTimingMinW * u;
    l.figure = {x0, y, kFigureW * u, fig_h};
    const float notes_x1 = timing ? x1 - timing_w - gap : x1;
    l.notes = {l.figure.right() + gap, y, std::max(0.0f, notes_x1 - l.figure.right() - gap), fig_h};
    if (timing) {
        l.timing = {x1 - timing_w, y, timing_w, fig_h};
    }
    y += fig_h + gap;
    // The buttons: Again (the larger), Faster and the Learn card, a hit tall.
    const float total = x1 - x0 - 2.0f * gap;
    const float unit_w = total / (kAgainShare + 2.0f);
    l.again = {x0, y, unit_w * kAgainShare, hit};
    l.faster = {l.again.right() + gap, y, unit_w, hit};
    l.to_card = {l.faster.right() + gap, y, unit_w, hit};
    l.fits = true;
    return l;
}

} // namespace etude::app::ui
