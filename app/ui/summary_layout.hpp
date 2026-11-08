// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the summary page ("How it went") go: pure geometry from the note area, the UI
// unit and what the page holds, like the results card's, so the layout tests can check every size
// without ImGui. The page floats over the player: its size is core's `summary_size` in
// `summary_area` (it may cover the chord strip and part of the keyboard, never the top row), and it
// is centred in that area.
//
// From the top: a header (Back, the title and what the run was, the pin, Done), the timing and the two
// hands on the left and the worst bars on the right (a bar a slim row: its words, then two icon
// chips, watch and practise; where the card is narrow the bars are one row of cells, a bar's words
// over its chips, and the timing and hands stack above them), and a row of two chips, Watch replay
// and Practise these bars. A note area too short for all of it gives up, in order: the spacing, the
// hands, the timing's ruler, the bars; the header and the two chips always stay. The header's right
// end also holds a Timing | Board track; the Board tab's body is summary_board_layout.hpp's. Where the
// Board tab has no room for the runs chart beside its table (the 7 inch panel's 800x480) the track
// has a third cell, Runs, whose body is the chart alone.
#pragma once

#include "results_card_layout.hpp"
#include "summary_board_layout.hpp"

#include "etude/results_card.hpp"
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace summary_tok {
// Design points (times the UI unit `u`).
inline constexpr float kLabelH = 20.0f;      // "Timing", "Trouble bars" over their parts
inline constexpr float kRulerH = 56.0f;      // the timing ruler and the words under it
inline constexpr float kRulerLeanH = 40.0f;  // the same where the page is stacked
inline constexpr float kHandRowH = 30.0f;    // a hand's line: its mark, accuracy and timing
inline constexpr float kBarFace = 36.0f;     // a bar's row, drawn (its chips keep a hit each)
inline constexpr float kWideAt = 640.0f;     // the inner width from which the page has two columns
inline constexpr int kBars = 3;              // the worst bars it names
inline constexpr float kBadge = 24.0f;       // the L or R mark of a hand
} // namespace summary_tok

struct SummaryBarCell {
    RectF row;      // the band the bar lives in: a hit tall
    RectF face;     // the row's slim face, inside it
    RectF text;     // the words, between the face's edge and the chips
    RectF practise; // two icon chips, a hit square each
    RectF watch;
};

struct SummaryLayout {
    bool fits = false;
    bool compact = false; // the spacing was tightened
    bool wide = false;    // two columns
    float scale = 1.0f;
    RectF card;
    RectF back;  // the header's chips: a hit square each
    RectF done;
    RectF pin;   // keep this run's replay: left of Done, a hit square
    std::array<RectF, 3> tabs{}; // Timing, Board, Runs: left of the pin, a hit tall each
    int tab_count = 2;           // 3 where the chart has no room beside the Board's table
    bool runs_tab = false;       // the Runs tab exists (tab_count is 3)
    RectF title; // the title over what the run was, between Back and the tabs
    RectF timing_label; // "Timing" and, at its right, the median and the spread
    RectF ruler;        // empty when dropped
    std::array<RectF, 2> hand_rows{}; // Left, Right; empty when dropped
    bool hands = false;
    RectF bars_label; // empty with no bar
    std::array<SummaryBarCell, summary_tok::kBars> bars{};
    int bar_count = 0;
    bool stacked_bars = false; // the cells are a row, their words two lines
    RectF actions;             // the row for Watch replay and Practise these bars
    SummaryBoard board;        // the Board tab's parts, when it is the one showing
    SummaryRuns runs;          // the Runs tab's parts, when it is the one showing
};

// The page never moves for the band hanging from the revealed bar: the band draws over it and takes
// the touches in its own area. The window order says who is on top, and a page above a live band
// (it was made after the band, or touched last) is lowered behind it; one already behind it stays.
// `page_index` and `band_index` are the windows' display positions, a higher one drawn later.
inline bool lower_behind_band(int page_index, int band_index) { return page_index > band_index; }

// What the page holds, as far as its layout goes.
struct SummaryContent {
    int bars = 0;           // worst bars to name (at most `summary_tok::kBars` are placed)
    bool timing = true;     // there is timing to show (Flow Mode, notes kept)
    bool hands = true;      // two hands were practised
};

namespace summary_detail {

// What the page keeps and how it is spaced once it has been fitted to the area.
struct Plan {
    float sc = 1.0f;
    float width = 0.0f;
    float pad = 0.0f;
    float gap = 0.0f;
    bool compact = false;
    bool wide = false;
    bool hands = false;
    bool ruler = false;
    bool bars = false;
    int bar_count = 0;
    BoardPlan board;
    bool runs = false;       // the Runs tab: the chart fills the body
    bool runs_label = false; // ... under its label
    float runs_body = 0.0f;

    float hit() const { return tok::kHit * sc; }
    float dead() const { return tok::kHitGap * sc; } // between two hit areas
    float ruler_h() const {
        return (wide ? summary_tok::kRulerH : summary_tok::kRulerLeanH) * sc;
    }
    // The timing's label, ruler and hands.
    float left_h() const {
        float h = summary_tok::kLabelH * sc;
        h += ruler ? gap + ruler_h() : 0.0f;
        h += hands ? gap + 2.0f * summary_tok::kHandRowH * sc : 0.0f;
        return h;
    }
    float bars_h() const {
        if (!bars) {
            return 0.0f;
        }
        return wide ? summary_tok::kLabelH * sc + static_cast<float>(bar_count) * hit() +
                          static_cast<float>(bar_count - 1) * dead()
                    : hit();
    }
    // Wide: the columns side by side. Narrow: the timing and hands, then the bars' row.
    float body_h() const {
        if (runs) {
            return runs_body;
        }
        if (board.on) {
            return board.left_h(sc, gap);
        }
        return wide ? std::max(left_h(), bars_h()) : left_h() + (bars ? gap + bars_h() : 0.0f);
    }
    float total() const { return 2.0f * pad + hit() + gap + body_h() + gap + hit(); }
};

// The Board tab fitted to `room`: the spacing tightens first, then the footnote goes, then a wide
// card puts the points and the period track in one row (a narrow card always has), then the chart
// (the table takes the whole width), then table rows down to three, then the column heads, then all
// but one row; the lines left out are scrolled to (the table keeps all of them). A narrow card has
// its chart beside the table, so there the table gives up its rows and heads first and the chart is
// the last of those to go.
inline void fit_board(Plan& p, float room, const BoardContent& b) {
    p.board.on = true;
    p.board.merged = !p.wide;
    p.board.rows = std::clamp(b.rows, 1, board_tok::kMaxRows);
    p.board.total = p.board.rows;
    p.board.footnote = b.footnote;
    p.board.row_h = board_tok::kRowH * p.sc;
    p.board.row_gap = board_tok::kRowGap * p.sc;
    const bool beside = b.chart && !p.wide && p.width - 2.0f * p.pad >= board_tok::kChartNarrowAt * p.sc;
    bool chart = (p.wide && b.chart) || beside;
    const int least = std::min(p.board.rows, board_tok::kMinRows);
    for (int step = 0; step < 4 && p.total() > room; ++step) {
        if (step == 0) {
            p.compact = true;
            p.pad = tok::kS2 * p.sc;
            p.gap = tok::kHitGap * p.sc;
            p.board.row_h = board_tok::kCompactRowH * p.sc;
        } else if (step == 1) {
            p.board.footnote = false;
        } else if (step == 2) {
            p.board.merged = true;
        } else if (!beside) {
            chart = false;
        }
    }
    while (p.total() > room && p.board.rows > least) {
        --p.board.rows;
    }
    if (p.total() > room) {
        p.board.heads = false;
    }
    while (p.total() > room && p.board.rows > 1) {
        --p.board.rows;
    }
    // The chart is as tall as the table's column, so it goes last: only once every cut above has
    // left that column too short for its five rows.
    p.board.chart = chart && board_chart_fits(p.board.left_h(p.sc, p.gap), p.sc, p.gap);
}

// The Runs tab fitted to `room`, after the Board tab's plan `board_plan` (the same spacing and never
// a shorter card, so a switch between the two does not shrink it): the spacing tightens first, then
// the label goes. The chart is never under its least height; where that and the Board tab's height
// do not both fit `room` the plan is over it and the page has no Runs tab (`wants_runs_tab`).
inline void fit_runs(Plan& p, float room, const Plan& board_plan) {
    p.runs = true;
    p.runs_label = true;
    p.pad = board_plan.pad;
    p.gap = board_plan.gap;
    p.compact = board_plan.compact;
    const auto least = [&p] {
        return (p.runs_label ? summary_tok::kLabelH * p.sc + p.gap : 0.0f) +
               board_runs_min_h(p.sc);
    };
    p.runs_body = std::max(board_plan.body_h(), least());
    for (int step = 0; step < 2 && p.total() > room; ++step) {
        if (step == 0) {
            p.compact = true;
            p.pad = tok::kS2 * p.sc;
            p.gap = tok::kHitGap * p.sc;
        } else {
            p.runs_label = false;
        }
        p.runs_body = least();
    }
    // Never a shorter card than the Board tab's.
    const float chrome = p.total() - p.runs_body;
    p.runs_body = std::max(p.runs_body, board_plan.total() - chrome);
}

// Fits the page to `area`: the spacing tightens first, then the hands, the ruler and the bars go.
inline Plan fit_plan(const RectF& area, const CardSize& size, const SummaryContent& c,
                     const BoardContent& board = {}) {
    Plan p;
    p.sc = size.scale;
    p.width = size.width;
    p.pad = tok::kS4 * p.sc;
    p.gap = tok::kS2 * p.sc;
    p.bar_count = std::clamp(c.bars, 0, summary_tok::kBars);
    p.wide = p.width - 2.0f * p.pad >= summary_tok::kWideAt * p.sc;
    p.hands = c.hands;
    p.ruler = c.timing;
    p.bars = p.bar_count > 0;
    const float room = area.h - 2.0f * card_tok::kMargin * p.sc;
    if (board.runs) {
        BoardContent as_board = board;
        as_board.on = true;
        as_board.runs = false;
        Plan on_board = p;
        fit_board(on_board, room, as_board);
        fit_runs(p, room, on_board);
        return p;
    }
    if (board.on) {
        fit_board(p, room, board);
        return p;
    }
    for (int step = 0; step < 4 && p.total() > room; ++step) {
        if (step == 0) {
            p.compact = true;
            p.pad = tok::kS2 * p.sc;
            p.gap = tok::kHitGap * p.sc;
        } else if (step == 1) {
            p.hands = false;
        } else if (step == 2) {
            p.ruler = false;
        } else {
            p.bars = false;
        }
    }
    return p;
}

// One bar's cell in `row`: the slim face, the words, the two chips at the right end.
inline void place_bar_cell(SummaryBarCell& cell, const RectF& row, float sc) {
    const float chip = tok::kHit * sc;
    cell.row = row;
    const float face_h = std::min(row.h, summary_tok::kBarFace * sc);
    cell.face = {row.x, row.y + (row.h - face_h) * 0.5f, row.w, face_h};
    const float dead = tok::kHitGap * sc;
    cell.watch = {row.right() - chip, row.y, chip, row.h};
    cell.practise = {row.right() - 2.0f * chip - dead, row.y, chip, row.h};
    cell.text = {row.x + tok::kS3 * sc, row.y,
                 std::max(0.0f, row.w - 2.0f * chip - dead - tok::kS3 * sc), row.h};
}

// The worst bars: a column of rows at the right (wide), or a row of cells under the timing and
// hands, which end at `left_bottom`.
inline void place_bars(SummaryLayout& l, const Plan& p, float x, float inner, float left_w,
                       float body_top, float left_bottom) {
    const float hit = p.hit();
    if (p.wide) {
        const float rx = x + left_w + tok::kS4 * p.sc;
        const float rw = inner - left_w - tok::kS4 * p.sc;
        l.bars_label = {rx, body_top, rw, summary_tok::kLabelH * p.sc};
        for (int i = 0; i < l.bar_count; ++i) {
            place_bar_cell(l.bars[static_cast<std::size_t>(i)],
                           {rx,
                            body_top + summary_tok::kLabelH * p.sc +
                                static_cast<float>(i) * (hit + p.dead()),
                            rw, hit},
                           p.sc);
        }
        return;
    }
    const float cw =
        (inner - static_cast<float>(l.bar_count - 1) * p.gap) / static_cast<float>(l.bar_count);
    for (int i = 0; i < l.bar_count; ++i) {
        place_bar_cell(l.bars[static_cast<std::size_t>(i)],
                       {x + static_cast<float>(i) * (cw + p.gap), left_bottom + p.gap, cw, hit},
                       p.sc);
    }
}

// The Runs tab's parts from `body_top`: the label, the chart under it, down to the body's foot, and
// the view button at the end of the actions' row.
inline void place_runs(SummaryLayout& l, const Plan& p, float x, float inner, float body_top,
                       const RectF& actions) {
    const float sc = p.sc;
    SummaryRuns& r = l.runs;
    r.shown = true;
    float top = body_top;
    if (p.runs_label) {
        r.label = {x, top, inner, summary_tok::kLabelH * sc};
        top += summary_tok::kLabelH * sc + p.gap;
    }
    r.chart = {x, top, inner, std::max(0.0f, body_top + p.runs_body - top)};
    r.view = {x + inner - p.hit(), actions.y, p.hit(), p.hit()};
}

// Whether the page has a Runs tab: the player has runs to chart, the Board tab, fitted to the area,
// cannot draw the chart beside its table, and the Runs body (the chart at its least, under its
// label where there is room, and a card no shorter than the Board's) fits the area.
inline bool wants_runs_tab(const RectF& area, const CardSize& size, const SummaryContent& c,
                           const BoardContent& board) {
    if (!board.chart) {
        return false;
    }
    BoardContent as_board = board;
    as_board.on = true;
    as_board.runs = false;
    if (fit_plan(area, size, c, as_board).board.chart) {
        return false;
    }
    as_board.runs = true;
    const Plan runs = fit_plan(area, size, c, as_board);
    return runs.total() <= area.h - 2.0f * card_tok::kMargin * runs.sc;
}

// The Board tab's parts from `body_top`, `body_h` tall: the table's column, and where the card is
// wide and the plan kept the chart, a column of bars at its right.
inline void place_board(SummaryLayout& l, const Plan& p, float x, float inner, float body_top,
                        float body_h, const RectF& actions) {
    const float sc = p.sc;
    const float hit = p.hit();
    SummaryBoard& b = l.board;
    b.shown = true;
    b.row_h = p.board.row_h;
    const float col_gap = tok::kS4 * sc;
    const float left_w = p.board.chart ? (inner - col_gap) * board_tok::kLeftShare : inner;
    float y = body_top;
    // Where the chart shares the head's row the period cells are `kPeriodSlimW` wide (still more
    // than a hit once the dead gap is taken off a cell's side).
    const float cell_w =
        (p.board.merged && p.board.chart ? board_tok::kPeriodSlimW : board_tok::kPeriodCellW) * sc;
    if (p.board.merged) {
        // One row: the points at the left, the period track at the right.
        const float track_x = x + left_w - 3.0f * cell_w;
        b.head = {x, y, std::max(0.0f, track_x - p.gap - x), board_tok::kHeadH * sc};
        for (std::size_t i = 0; i < 3; ++i) {
            b.period[i] = {track_x + static_cast<float>(i) * cell_w, y, cell_w, hit};
        }
        y += board_tok::kHeadH * sc + p.gap;
    } else {
        b.head = {x, y, left_w, board_tok::kHeadH * sc};
        y += board_tok::kHeadH * sc + p.gap;
        for (std::size_t i = 0; i < 3; ++i) {
            b.period[i] = {x + static_cast<float>(i) * cell_w, y, cell_w, hit};
        }
        const float cap_x = x + 3.0f * cell_w + p.gap;
        if (x + left_w - cap_x >= board_tok::kCaptionMinW * sc) {
            b.caption = {cap_x, y, x + left_w - cap_x, hit};
        }
        y += hit + p.gap;
    }
    if (p.board.heads) {
        b.table_head = {x, y, left_w, board_tok::kThH * sc};
        y += board_tok::kThH * sc;
    }
    b.row_count = p.board.rows;
    b.total_rows = p.board.total;
    b.row_gap = p.board.row_gap;
    const float rows_top = y;
    for (int i = 0; i < p.board.rows; ++i) {
        b.rows[static_cast<std::size_t>(i)] = {x, y, left_w, p.board.row_h};
        y += p.board.row_h + p.board.row_gap;
    }
    b.rows_view = {x, rows_top, left_w, y - p.board.row_gap - rows_top};
    if (p.board.footnote) {
        b.footnote = {x, y + p.gap - p.board.row_gap, left_w, board_tok::kFootH * sc};
    }
    if (p.board.chart) {
        const float rx = x + left_w + col_gap;
        const float rw = inner - left_w - col_gap;
        b.divider = {x + left_w + col_gap * 0.5f, body_top, 0.0f, body_h};
        b.chart_label = {rx, body_top, rw, board_tok::kLabelH * sc};
        const float top = body_top + board_tok::kLabelH * sc + p.gap;
        b.chart = {rx, top, rw, body_top + body_h - top};
        b.chart_view = {x + inner - hit, actions.y, hit, hit};
    }
}

// The header's controls from `y`: Back at the left, Done at the right, the tabs and the pin left of
// Done, the title between Back and the tabs.
inline void place_header(SummaryLayout& l, const Plan& p, float x, float inner, float y,
                         bool runs_tab) {
    const float sc = p.sc;
    const float hit = p.hit();
    l.back = {x, y, hit, hit};
    l.done = {x + inner - hit, y, hit, hit};
    l.pin = {l.done.x - hit - p.dead(), y, hit, hit};
    // The tabs are one segmented pill; each one's hit area is its cell less half the dead gap at a
    // neighbour's side (`segment_cells`), and the pill keeps a gap from the pin.
    l.runs_tab = runs_tab;
    l.tab_count = runs_tab ? 3 : 2;
    const float tab_w = (runs_tab ? board_tok::kTabCell3W : board_tok::kTabCellW) * sc;
    const float tabs_gap = std::max(tok::kS2 * sc, p.dead());
    for (int i = 0; i < l.tab_count; ++i) {
        l.tabs[static_cast<std::size_t>(i)] = {
            l.pin.x - tabs_gap - static_cast<float>(l.tab_count - i) * tab_w, y, tab_w, hit};
    }
    l.title = {x + hit + tok::kS2 * sc, y,
               std::max(0.0f, l.tabs[0].x - tok::kS2 * sc - (x + hit + tok::kS2 * sc)), hit};
}

} // namespace summary_detail

inline SummaryLayout summary_layout(const RectF& area, float u, const SummaryContent& c,
                                    const BoardContent& board = {}, float card_scale = 0.0f) {
    SummaryLayout l;
    const CardSize size = summary_size(area, u, card_scale);
    l.scale = size.scale;
    if (area.empty() || !(size.width > 2.0f * tok::kS4 * size.scale)) {
        return l;
    }
    // The Runs tab is there exactly where the Board tab drops the chart; a page asked for it
    // elsewhere shows the Board tab.
    const bool runs_tab = summary_detail::wants_runs_tab(area, size, c, board);
    BoardContent shown = board;
    if (shown.runs && !runs_tab) {
        shown.runs = false;
        shown.on = true;
    }
    const summary_detail::Plan p = summary_detail::fit_plan(area, size, c, shown);
    const float sc = p.sc;
    const float hit = p.hit();
    const float height = std::min(p.total(), area.h);
    l.fits = p.total() <= area.h;
    l.compact = p.compact;
    l.wide = p.wide;
    l.hands = p.hands;
    l.card = {area.x + (area.w - p.width) * 0.5f, area.y + (area.h - height) * 0.5f, p.width,
              height};
    const float x = l.card.x + p.pad;
    const float inner = p.width - 2.0f * p.pad;
    float y = l.card.y + p.pad;
    summary_detail::place_header(l, p, x, inner, y, runs_tab);
    y += hit + p.gap;
    const float body_top = y;
    l.actions = {x, l.card.bottom() - p.pad - hit, inner, hit};
    if (p.runs) {
        summary_detail::place_runs(l, p, x, inner, body_top, l.actions);
        l.actions.w = std::max(0.0f, l.runs.view.x - p.dead() - l.actions.x);
        return l;
    }
    if (p.board.on) {
        summary_detail::place_board(l, p, x, inner, body_top, p.board.left_h(sc, p.gap), l.actions);
        if (!l.board.chart_view.empty()) {
            // The chart's view button ends the actions' row; the chips keep the room before it.
            l.actions.w = std::max(0.0f, l.board.chart_view.x - p.dead() - l.actions.x);
        }
        return l;
    }
    const float left_w = p.wide ? (inner - tok::kS4 * sc) * 0.5f : inner;
    l.timing_label = {x, y, left_w, summary_tok::kLabelH * sc};
    float ly = y + summary_tok::kLabelH * sc;
    if (p.ruler) {
        l.ruler = {x, ly + p.gap, left_w, p.ruler_h()};
        ly += p.gap + p.ruler_h();
    }
    if (p.hands) {
        for (std::size_t i = 0; i < 2; ++i) {
            l.hand_rows[i] = {x, ly + p.gap + static_cast<float>(i) * summary_tok::kHandRowH * sc,
                              left_w, summary_tok::kHandRowH * sc};
        }
        ly += p.gap + 2.0f * summary_tok::kHandRowH * sc;
    }
    l.bar_count = p.bars ? p.bar_count : 0;
    l.stacked_bars = !p.wide;
    if (l.bar_count > 0) {
        summary_detail::place_bars(l, p, x, inner, left_w, body_top, ly);
    }
    return l;
}

} // namespace etude::app::ui
