// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the results card go: pure geometry from the note area, the UI unit and what
// the card holds, so the layout tests can check every size without ImGui. The card is centred in
// the note area (the run has ended, so the notes are not playing) and never reaches the keyboard.
// Its size is core's `card_size`: a share of the note area, drawn at a scale of its own that is the
// UI scale or a little more on a big screen. When the area is too short for every row the spacing
// tightens first (the compact form), then the least important rows go: the counts, then the trouble
// bars with their actions. The headline and the buttons are never clipped.
#pragma once

#include "tokens.hpp"

#include "etude/results_card.hpp"
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

namespace card_tok {
// Design points (times the UI unit `u`).
inline constexpr float kHeadlineH = 40.0f;  // the accuracy figure and the two lines beside it
inline constexpr float kCountsH = 38.0f;    // a number over its caption
inline constexpr float kFigureSize = 28.0f; // the accuracy figure's type size
inline constexpr float kMargin = tok::kS2;  // clear of the area's edge
// The surface starts this share of its size and grows to the whole as the card comes in; the
// content, inset by more than the difference, only fades.
inline constexpr float kEnterScale = 0.96f;
inline constexpr float kCompactHeadlineH = 38.0f; // the headline when the area is very short
// With the weighted score on, the headline shows two figures, each over a caption ("Score",
// "Weighted"): the pair takes this much of the card's inner width at most (design points), and each
// figure is drawn at kPairFigureSize; the figures take this share of the headline's height, the
// captions the rest.
inline constexpr float kPairW = 150.0f;
inline constexpr float kPairShare = 0.62f;
inline constexpr float kPairFigureSize = 22.0f;
// The order list's drag grip (two columns of three dots) and where a row's content starts.
inline constexpr float kGripDot = 1.5f;      // a dot's radius
inline constexpr float kGripGapX = 3.5f;     // between the two columns
inline constexpr float kGripGapY = 5.0f;     // between the rows of dots
inline constexpr float kGripInset = 4.0f;    // the grip's centre, in from the row's left padding
inline constexpr float kNumberInset = 14.0f; // the place number, in from the row's left padding
inline constexpr float kNumberWidth = 16.0f; // the room kept for it, so every row's words line up
// The "How it went" chip at the headline's right: with its words it takes `kReviewW`; where the
// lines of text beside it would be left under `kTextMinW`, it is its icon alone (a hit square); and
// where even that leaves the text under `kTextMinIconW`, it is left off.
inline constexpr float kReviewW = 128.0f;
inline constexpr float kTextMinW = 150.0f;
inline constexpr float kTextMinIconW = 96.0f;
} // namespace card_tok

// The buttons' row: Replay, then (when the hands are chosen here) Left, Right and Both, then Done.
inline constexpr int kMaxCardButtons = 5;

struct CardLayout {
    bool fits = false;    // every part that is shown lies inside the area
    bool compact = false; // the area was too short for the normal spacing
    float scale = 1.0f;   // pixels per design point the card draws at (core `card_size`)
    RectF card;
    RectF headline; // the figure, the two lines and the hands' figures
    RectF figure;   // the accuracy figure, at the left of the headline (with the weighted score on:
                    // the plain and the weighted figure side by side, each over its caption)
    RectF hands;    // the hands' figures, right-aligned in the headline (empty without them)
    RectF text;     // between the figure and the hands: the mode line and the comparison line
    RectF review;   // the "How it went" chip, right of the text (empty when there is no room)
    bool review_words = false; // it shows its words, not the icon alone
    RectF counts;   // empty when dropped or when the run has none
    int count_cells = 0;
    RectF bars_head;    // the caption and the two actions; empty when dropped or no bar is named
    RectF bars_caption; // the words at the left of the head
    std::array<RectF, 2> action_cells{}; // One by one, Together
    RectF bars;                          // the chips' row; empty when dropped or no bar is named
    std::array<RectF, kMaxCardBars> bar_cells{};
    int bars_shown = 0; // how many of the card's bars have a chip
    RectF buttons;
    std::array<RectF, kMaxCardButtons> button_cells{};
    int button_count = 0;
    bool hand_cells = false; // cells 1 to 3 are Left, Right and Both
};

// How many cells the counts row has: Flow Mode shows five grades, the wrong presses and the
// longest combo; Wait Mode the notes played, the wrong presses and the longest combo; a run that
// graded nothing none.
inline int card_count_cells(const ResultsCard& card) {
    if (card.ungraded) {
        return 0;
    }
    return card.mode == RunMode::Wait ? 3 : 7;
}

namespace card_detail {

// The "How it went" chip comes out of the text's room, at its right end, while the text keeps
// enough.
inline void place_review(CardLayout& l, float y, float hit, float gap, float sc) {
    const float words_w = card_tok::kReviewW * sc;
    float want = 0.0f;
    if (l.text.w - words_w - gap >= card_tok::kTextMinW * sc) {
        want = words_w;
        l.review_words = true;
    } else if (l.text.w - hit - gap >= card_tok::kTextMinIconW * sc) {
        want = hit;
    }
    if (want > 0.0f) {
        // A hit area tall: its foot on the headline's, so the gap below stays whole, reaching up
        // into the card's padding when the headline is shorter.
        l.review = {l.text.x + l.text.w - want, y + l.headline.h - hit, want, hit};
        l.text.w -= want + gap;
    }
}

// The buttons' row: Replay a little wider (it is the primary), then the three hands, then Done.
inline void place_buttons(CardLayout& l, const ResultsCard& card, float x, float y, float inner,
                          float hit, float small_gap) {
    l.buttons = {x, y, inner, hit};
    l.hand_cells = card.hand_choice;
    l.button_count = l.hand_cells ? 5 : 2;
    float weights[kMaxCardButtons] = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f};
    float sum = 0.0f;
    if (l.hand_cells) {
        weights[0] = 1.4f;
    }
    for (int i = 0; i < l.button_count; ++i) {
        sum += weights[i];
    }
    const float unit = (inner - static_cast<float>(l.button_count - 1) * small_gap) / sum;
    float bx = x;
    for (int i = 0; i < l.button_count; ++i) {
        l.button_cells[static_cast<std::size_t>(i)] = {bx, y, unit * weights[i], hit};
        bx += unit * weights[i] + small_gap;
    }
}

} // namespace card_detail

inline CardLayout results_card_layout(const RectF& area, float u, const ResultsCard& card) {
    CardLayout l;
    const CardSize size = card_size(area, u);
    const float sc = size.scale;
    l.scale = sc;
    float pad = tok::kS4 * sc;
    float gap = tok::kS3 * sc;
    float head_h = card_tok::kHeadlineH * sc;
    const float small_gap = tok::kS2 * sc;
    const float hit = tok::kHit * sc;
    const float width = size.width;
    if (area.empty() || !(width > 2.0f * pad)) {
        return l;
    }
    const int cells = card_count_cells(card);
    bool counts = cells > 0;
    bool bars = !card.ungraded && card.bar_count > 0;
    const auto total = [&] {
        float h = 2.0f * pad + head_h + gap + hit; // headline, gap, buttons
        h += counts ? gap + card_tok::kCountsH * sc : 0.0f;
        h += bars ? gap + hit + small_gap + hit : 0.0f; // the bars' head, then the chips
        return h;
    };
    const float room = area.h - 2.0f * card_tok::kMargin * sc;
    if (total() > room) {
        // Tighter spacing first: every row stays.
        l.compact = true;
        pad = tok::kS2 * sc;
        gap = tok::kS2 * sc;
        head_h = card_tok::kCompactHeadlineH * sc;
    }
    if (total() > room) {
        counts = false;
    }
    if (total() > room) {
        bars = false;
    }
    const float height = std::min(total(), area.h);
    l.fits = total() <= area.h;
    l.card = {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
    const float x = l.card.x + pad;
    const float inner = width - 2.0f * pad;
    float y = l.card.y + pad;

    l.headline = {x, y, inner, head_h};
    // The figure takes what "100%" needs; the hands' figures, when there are two, the right
    // fifth, and the lines of text what is between.
    const bool pair = card.weighted_pct >= 0 && card.accuracy_pct >= 0;
    const float figure_w = pair ? std::min(inner * 0.38f, card_tok::kPairW * sc)
                                : std::min(inner * 0.24f, 88.0f * sc);
    l.figure = {x, y, figure_w, l.headline.h};
    const float hands_w =
        card.right_pct >= 0 && card.left_pct >= 0 ? std::min(inner * 0.3f, 150.0f * sc) : 0.0f;
    if (hands_w > 0.0f) {
        l.hands = {x + inner - hands_w, y, hands_w, l.headline.h};
    }
    const float text_x = x + figure_w + gap;
    l.text = {text_x, y,
              std::max(0.0f, x + inner - hands_w - (hands_w > 0.0f ? gap : 0.0f) - text_x),
              l.headline.h};
    card_detail::place_review(l, y, hit, gap, sc);
    y += l.headline.h + gap;

    if (counts) {
        l.counts = {x, y, inner, card_tok::kCountsH * sc};
        l.count_cells = cells;
        y += l.counts.h + gap;
    }
    if (bars) {
        l.bars_head = {x, y, inner, hit};
        // The two actions at the right end, the words ("Trouble bars", "Tap to choose") before
        // them.
        const float action_w = std::clamp(inner * 0.24f, 2.0f * hit, 150.0f * sc);
        l.action_cells[1] = {x + inner - action_w, y, action_w, hit};
        l.action_cells[0] = {x + inner - 2.0f * action_w - small_gap, y, action_w, hit};
        l.bars_caption = {x, y, std::max(0.0f, l.action_cells[0].x - gap - x), hit};
        y += hit + small_gap;
        l.bars = {x, y, inner, hit};
        // As many chips as fit one row at their narrowest, up to the card's bars. A few bars keep
        // chips as wide as the fewest the row is cut in (three), so one or two sit at the left,
        // never stretched.
        const int fit_n =
            static_cast<int>(std::floor((inner + small_gap) / (kCardChipMinPt * sc + small_gap)));
        // The compact form (no wider than the base width at its scale) keeps its three chips.
        const bool compact_width = width <= kCardBaseWidthPt * sc + 0.5f;
        const int per_row = std::clamp(fit_n, 1, compact_width ? kCardCompactBars : kMaxCardBars);
        l.bars_shown = std::min(card.bar_count, per_row);
        const int across = std::min(per_row, std::max(l.bars_shown, 3));
        const float cell_w =
            (inner - static_cast<float>(across - 1) * small_gap) / static_cast<float>(across);
        for (int i = 0; i < l.bars_shown; ++i) {
            l.bar_cells[static_cast<std::size_t>(i)] = {
                x + static_cast<float>(i) * (cell_w + small_gap), y, cell_w, hit};
        }
        y += hit + gap;
    }
    card_detail::place_buttons(l, card, x, y, inner, hit, small_gap);
    return l;
}

// ---- The order of the bars
// -------------------------------------------------------------------------

// One Start and one Back on the head row; a row for each chosen bar under it.
struct OrderLayout {
    bool fits = false;    // every part lies inside the area
    bool compact = false; // the spacing was tightened to fit
    float scale = 1.0f;   // pixels per design point (core `card_size`)
    RectF card;
    RectF head;
    RectF caption; // the words at the left of the head row
    RectF back;
    RectF start;
    std::array<RectF, kMaxCardBars> rows{};  // each bar's row, the card's inner width
    std::array<RectF, kMaxCardBars> grabs{}; // the part of the row that drags it
    std::array<RectF, kMaxCardBars> ups{};   // the move buttons at its right end
    std::array<RectF, kMaxCardBars> downs{};
    int count = 0;
    int columns = 1;       // 2 when one column of rows did not fit the room
    float rows_top = 0.0f; // where the first row starts
    float pitch = 0.0f;    // from one row to the next: the row and the gap
    float row_h = 0.0f;
    // The columns of rows: where the first starts, how wide each is and the gap between (for
    // core's `ReorderDrag::Rows`).
    float column_left = 0.0f;
    float column_w = 0.0f;
    float column_gap = 0.0f;
};

// The order list for `count` chosen bars (2 to `kMaxCardBars`) centred in `area`. Every row is
// `tok::kHit` tall, the up and down buttons are `tok::kHit` square, and the rest of the row is the
// grab area, so a bar is moved by a drag from anywhere on it or by the two buttons. A note area too
// short for the rows in one column (after the spacing is tightened) gets them in two, the first
// half down the left and the rest down the right, so no row is ever dropped or shrunk.
inline OrderLayout order_layout(const RectF& area, float u, int count) {
    OrderLayout l;
    const CardSize size = card_size(area, u);
    const float sc = size.scale;
    l.scale = sc;
    count = std::clamp(count, 0, kMaxCardBars);
    float pad = tok::kS4 * sc;
    float head_gap = tok::kS3 * sc;
    float row_gap = tok::kS2 * sc;
    const float hit = tok::kHit * sc;
    const float width = size.width;
    if (area.empty() || count <= 0 || !(width > 2.0f * pad)) {
        return l;
    }
    int columns = 1;
    const auto per_column = [&] { return (count + columns - 1) / columns; };
    const auto total = [&] {
        const int n = per_column();
        return 2.0f * pad + hit + head_gap + static_cast<float>(n) * hit +
               static_cast<float>(n - 1) * row_gap;
    };
    const float room = area.h - 2.0f * card_tok::kMargin * sc;
    if (total() > room) {
        l.compact = true;
        pad = tok::kS2 * sc;
        head_gap = tok::kHitGap * sc;
        row_gap = tok::kHitGap * sc;
    }
    if (total() > room && count > 1) {
        columns = 2;
    }
    l.columns = columns;
    const float height = std::min(total(), area.h);
    l.fits = total() <= area.h;
    l.card = {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
    const float x = l.card.x + pad;
    const float inner = width - 2.0f * pad;
    float y = l.card.y + pad;
    l.head = {x, y, inner, hit};
    const float button_w = std::clamp(inner * 0.24f, 2.0f * hit, 150.0f * sc);
    l.start = {x + inner - button_w, y, button_w, hit};
    l.back = {l.start.x - tok::kS2 * sc - button_w, y, button_w, hit};
    l.caption = {x, y, std::max(0.0f, l.back.x - tok::kS3 * sc - x), hit};
    y += hit + head_gap;
    l.rows_top = y;
    l.row_h = hit;
    l.pitch = hit + row_gap;
    l.count = count;
    const float column_gap = tok::kS3 * sc;
    const float column_w =
        (inner - static_cast<float>(columns - 1) * column_gap) / static_cast<float>(columns);
    l.column_left = x;
    l.column_w = column_w;
    l.column_gap = columns > 1 ? column_gap : 0.0f;
    const int lines = per_column();
    const float dead = tok::kHitGap * sc; // between the grab area and the buttons, and between those
    for (int i = 0; i < count; ++i) {
        const auto j = static_cast<std::size_t>(i);
        const int column = i / lines;
        const int line = i % lines;
        const float rx = x + static_cast<float>(column) * (column_w + column_gap);
        const float ry = y + static_cast<float>(line) * l.pitch;
        l.rows[j] = {rx, ry, column_w, hit};
        l.downs[j] = {rx + column_w - hit, ry, hit, hit};
        l.ups[j] = {rx + column_w - 2.0f * hit - dead, ry, hit, hit};
        l.grabs[j] = {rx, ry, column_w - 2.0f * hit - 2.0f * dead, hit};
    }
    return l;
}

} // namespace etude::app::ui
