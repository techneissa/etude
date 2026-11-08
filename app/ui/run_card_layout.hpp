// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of a trouble-bar run's two cards go: the check-in after each bar (that bar's
// grade counts, then how it went, then Next bar, Again, Skip and End) and the summary at the end
// (every bar before and after, with its counts where they fit, then Done). Pure geometry from the
// note area, the UI unit and what the card holds, like the results card's, so the layout tests
// check every size without ImGui. Both are centred in the note area, at the width and scale of
// core's `card_size`, and never reach the keyboard or the top row.
#pragma once

#include "results_card_layout.hpp"

#include "etude/trouble_run.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace run_tok {
inline constexpr float kRowH = 30.0f;       // a summary row (not pressable)
inline constexpr float kRowCountsH = 46.0f; // a summary row with its grade counts on a second line
inline constexpr float kRowGap = 2.0f;      // between summary rows
} // namespace run_tok

inline constexpr int kCheckInStats = 3;   // Passes, Clean passes, Before
inline constexpr int kCheckInButtons = 4; // Next bar, Again, Skip, End

struct RunCardLayout {
    bool fits = false;
    bool compact = false;
    float scale = 1.0f;
    RectF card;
    RectF headline; // the bar's figure at the left, two lines beside it
    RectF figure;
    RectF text;
    // The check-in: the bar's grade counts at the top, the headline, then the stats row.
    RectF counts;
    int count_cells = 0;
    RectF stats;
    int stat_cells = 0;
    RectF buttons;
    std::array<RectF, kCheckInButtons> button_cells{};
    int button_count = 0;
    // The summary.
    RectF list;
    std::array<RectF, kMaxRunBars> rows{};
    int row_count = 0;
    bool row_counts = false; // each row has its grade counts on a second line
    int columns = 1;         // 2 when one column of rows did not fit the room
};

enum class RunCardKind : std::uint8_t { CheckIn, Summary };

// The card of `kind` centred in `area` (the note area, ImGui points) at the UI unit `u`; the
// summary has `bars` rows; the check-in `grade_cells` cells of grade counts (7 in Flow Mode, 3 in
// Wait Mode).
inline RunCardLayout run_card_layout(const RectF& area, float u, RunCardKind kind, int bars,
                                     int grade_cells = 7) {
    RunCardLayout l;
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
    bars = std::clamp(bars, 0, kMaxRunBars);
    const bool summary = kind == RunCardKind::Summary;
    int columns = 1;
    bool row_counts = summary;
    bool stats = !summary;
    bool counts = !summary && grade_cells > 0;
    const auto row_h = [&] { return (row_counts ? run_tok::kRowCountsH : run_tok::kRowH) * sc; };
    const float row_gap = run_tok::kRowGap * sc;
    const auto list_h = [&] {
        const int per_column = (bars + columns - 1) / columns;
        return per_column > 0 ? static_cast<float>(per_column) * row_h() +
                                    static_cast<float>(per_column - 1) * row_gap
                              : 0.0f;
    };
    const auto total = [&] {
        float h = 2.0f * pad + head_h + gap + hit; // headline, gap, buttons
        if (summary) {
            h += bars > 0 ? gap + list_h() : 0.0f;
        } else {
            h += counts ? gap + card_tok::kCountsH * sc : 0.0f;
            h += stats ? gap + card_tok::kCountsH * sc : 0.0f;
        }
        return h;
    };
    const float room = area.h - 2.0f * card_tok::kMargin * sc;
    // What goes, in order, while the card is too tall: the spacing tightens, then the check-in's
    // stats row and its counts go (the headline and the buttons always stay), or the summary's
    // counts go and its rows split into two columns.
    for (int step = 0; step < 4 && total() > room; ++step) {
        if (step == 0) {
            l.compact = true;
            pad = tok::kS2 * sc;
            gap = tok::kS2 * sc;
            head_h = card_tok::kCompactHeadlineH * sc;
        } else if (summary) {
            if (step == 1) {
                row_counts = false;
            } else if (step == 2 && bars > 1) {
                columns = 2; // the same rows in two columns
            }
        } else if (step == 1) {
            stats = false;
        } else if (step == 2) {
            counts = false;
        }
    }
    l.columns = columns;
    l.row_counts = summary && row_counts;
    const float height = std::min(total(), area.h);
    l.fits = total() <= area.h;
    l.card = {area.x + (area.w - width) * 0.5f, area.y + (area.h - height) * 0.5f, width, height};
    const float x = l.card.x + pad;
    const float inner = width - 2.0f * pad;
    float y = l.card.y + pad;
    if (!summary && counts) {
        l.counts = {x, y, inner, card_tok::kCountsH * sc};
        l.count_cells = grade_cells;
        y += l.counts.h + gap;
    }
    l.headline = {x, y, inner, head_h};
    const float figure_w = std::min(inner * 0.24f, 88.0f * sc);
    l.figure = {x, y, figure_w, head_h};
    l.text = {x + figure_w + gap, y, std::max(0.0f, inner - figure_w - gap), head_h};
    y += head_h + gap;
    if (!summary) {
        if (stats) {
            l.stats = {x, y, inner, card_tok::kCountsH * sc};
            l.stat_cells = kCheckInStats;
            y += l.stats.h + gap;
        }
        l.buttons = {x, y, inner, hit};
        l.button_count = kCheckInButtons;
        // Next bar a little wider (the primary), then Again, Skip and End.
        const float weights[kCheckInButtons] = {1.4f, 1.0f, 1.0f, 1.0f};
        const float sum = weights[0] + weights[1] + weights[2] + weights[3];
        const float unit = (inner - static_cast<float>(kCheckInButtons - 1) * small_gap) / sum;
        float bx = x;
        for (int i = 0; i < kCheckInButtons; ++i) {
            l.button_cells[static_cast<std::size_t>(i)] = {bx, y, unit * weights[i], hit};
            bx += unit * weights[i] + small_gap;
        }
        return l;
    }
    l.row_count = bars;
    if (bars > 0) {
        l.list = {x, y, inner, list_h()};
        const int per_column = (bars + columns - 1) / columns;
        const float column_gap = tok::kS3 * sc;
        const float column_w =
            (inner - static_cast<float>(columns - 1) * column_gap) / static_cast<float>(columns);
        for (int i = 0; i < bars; ++i) {
            const int column = i / per_column;
            const int line = i % per_column;
            l.rows[static_cast<std::size_t>(i)] = {
                x + static_cast<float>(column) * (column_w + column_gap),
                y + static_cast<float>(line) * (row_h() + row_gap), column_w, row_h()};
        }
        y += list_h() + gap;
    }
    // Done at the right end, no wider than a button needs.
    const float done_w = std::min(inner, std::max(2.0f * hit, inner * 0.3f));
    l.buttons = {x, y, inner, hit};
    l.button_count = 1;
    l.button_cells[0] = {x + inner - done_w, y, done_w, hit};
    return l;
}

} // namespace etude::app::ui
