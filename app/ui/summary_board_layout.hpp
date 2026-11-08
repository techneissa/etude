// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the summary page's "Board" tab go, pure geometry like summary_layout.hpp, which
// calls it for the body under the header. The left column holds the run's points and its two chips,
// the Week | Month | All track and the table of the household's best run of the song, then a
// footnote; where the card is wide the right column holds the bars of the player's own latest runs.
// Where the card is too narrow for two columns the points and the Week | Month | All track share
// one row. A short note area gives up, in order: the spacing, the footnote, the chart (the table
// then takes the whole width), then table rows down to three and, where even those do not fit, the
// column heads and then all but one row. No player is lost to that: the rows the area has no room
// for are still in the table, which scrolls with a drag (`SummaryBoard::scrolls`; it starts at the
// player's own line, `board_first_scroll`).
#pragma once

#include "tokens.hpp"

#include "etude/profile.hpp"
#include "etude/runs_chart.hpp"
#include "etude/screen_layout.hpp"

#include <algorithm>
#include <array>
#include <cstddef>

namespace etude::app::ui {

inline constexpr int kChartRowsMax = kChartRuns; // the runs the chart draws

namespace board_tok {
// Design points (times the card's scale).
inline constexpr float kRowH = 48.0f;         // a table row's face
inline constexpr float kCompactRowH = 34.0f;  // the same where the area is short
inline constexpr float kRowGap = tok::kS1;
inline constexpr float kHeadH = tok::kHit;    // the points and their chips
inline constexpr float kThH = 20.0f;          // the table's column heads
inline constexpr float kFootH = 20.0f;        // the footnote's line
inline constexpr float kLabelH = 20.0f;       // "Your runs on this song"
inline constexpr float kPeriodCellW = 72.0f;  // a Week, Month or All cell
inline constexpr float kTabCellW = 84.0f;     // a Timing or Board cell, in the header
inline constexpr float kTabCell3W = 58.0f;    // the same where a third tab, Runs, shares the header
inline constexpr float kCaptionMinW = 140.0f; // the period row's caption needs this much
inline constexpr float kChartRowMinH = 20.0f; // a row of the bars or the ranked list, least
inline constexpr float kChartLineH = 20.0f;   // the line under the chart: this run's place
inline constexpr float kRunsRowMinH = 18.0f;  // a row of the Runs tab's chart, least (it has no table beside it)
inline constexpr float kWideAt = 640.0f;      // the inner width from which the page has two columns
inline constexpr float kChartNarrowAt = 440.0f; // a narrower card has the chart from this inner width
inline constexpr float kPeriodSlimW = 56.0f;  // a period cell where the chart shares the head's row
inline constexpr float kPlayerMinW = 110.0f;  // under this the table drops its accuracy and speed
inline constexpr float kLeftShare = 0.58f;    // the table's share of a two-column body
inline constexpr float kRankW = 30.0f;        // the table's columns, design points
inline constexpr float kPointsW = 72.0f;
inline constexpr float kSpeedW = 56.0f;
inline constexpr float kAccuracyW = 68.0f;
inline constexpr int kMaxRows = static_cast<int>(kMaxProfiles);
inline constexpr int kMinRows = 3;            // rows kept before the column heads go
} // namespace board_tok

// What the Board tab holds, as far as its layout goes.
struct BoardContent {
    bool on = false;      // the Board tab is the one showing
    bool runs = false;    // the Runs tab is the one showing (it wins over `on`; it needs a chart)
    int rows = 1;         // the table's lines: one per profile of the household
    bool chart = true;    // the player has runs of the song to chart
    bool footnote = true; // there is a footnote to say
};

// The places of a table row's columns inside its face.
struct BoardColumns {
    RectF rank;
    RectF player; // the avatar, the name and the day
    RectF accuracy;
    RectF speed;
    RectF points;
};

inline BoardColumns board_columns(const RectF& row, float sc) {
    const float pad = tok::kS3 * sc;
    const float right = row.right() - pad;
    BoardColumns c;
    c.points = {right - board_tok::kPointsW * sc, row.y, board_tok::kPointsW * sc, row.h};
    c.speed = {c.points.x - board_tok::kSpeedW * sc, row.y, board_tok::kSpeedW * sc, row.h};
    c.accuracy = {c.speed.x - board_tok::kAccuracyW * sc, row.y, board_tok::kAccuracyW * sc, row.h};
    c.rank = {row.x + pad, row.y, board_tok::kRankW * sc, row.h};
    const float px = c.rank.right();
    if (c.accuracy.x - px < board_tok::kPlayerMinW * sc) {
        // A narrow table keeps the player and the points: the figures between them are dropped.
        c.accuracy = {};
        c.speed = {};
        c.player = {px, row.y, std::max(0.0f, c.points.x - px), row.h};
        return c;
    }
    c.player = {px, row.y, std::max(0.0f, c.accuracy.x - px), row.h};
    return c;
}

// The hit area of cell `i` of `count` in a segmented track (`segment_cells`): the cell, less half
// the dead gap `dead` at the side of a neighbour, so two cells' hit areas are `dead` apart.
inline RectF segment_hit(const RectF& cell, int i, int count, float dead) {
    const float half = dead * 0.5f;
    const float x0 = cell.x + (i > 0 ? half : 0.0f);
    const float x1 = cell.right() - (i + 1 < count ? half : 0.0f);
    return {x0, cell.y, x1 - x0, cell.h};
}

// The table scrolls when it has more lines (`count`) than fit (`shown`). Its lines are `row_h` tall
// with `gap` between; the scroll that brings line `index` fully into a window `view_h` tall (never
// under 0 or past the end), and the one the table opens at, which keeps the active profile's line
// `active` (-1 for none) in view, the leader's first when it is.
inline float board_scroll_max(int count, float row_h, float gap, float view_h) {
    const float content = static_cast<float>(count) * (row_h + gap) - gap;
    return std::max(0.0f, content - view_h);
}
inline float board_scroll_to(int index, int count, float row_h, float gap, float view_h) {
    const float bottom = static_cast<float>(index + 1) * (row_h + gap) - gap;
    const float want = std::max(0.0f, bottom - view_h);
    return std::min(want, board_scroll_max(count, row_h, gap, view_h));
}
inline float board_first_scroll(int count, int active, float row_h, float gap, float view_h) {
    return active < 0 ? 0.0f : board_scroll_to(std::min(active, count - 1), count, row_h, gap, view_h);
}

// How the body is spaced once it has been fitted.
struct BoardPlan {
    bool on = false;
    bool chart = false;
    bool footnote = false;
    bool merged = false; // the points and the period track share a row
    bool heads = true;   // the column heads over the table
    int rows = 0;  // the lines placed: the ones that fit
    int total = 0; // the household's: what the table holds
    float row_h = 0.0f;
    float row_gap = 0.0f;

    // The left column's height under `gap`, at scale `sc`.
    float left_h(float sc, float gap) const {
        float h = board_tok::kHeadH * sc + gap + (heads ? board_tok::kThH * sc : 0.0f) +
                  static_cast<float>(rows) * (row_h + row_gap);
        h += merged ? 0.0f : tok::kHit * sc + gap;
        h += footnote ? gap + board_tok::kFootH * sc : 0.0f;
        return h;
    }
};

// The chart's least height under its label: the runs it holds in rows of their least height, then
// the line about this run.
inline float board_chart_min_h(float sc, float gap) {
    return static_cast<float>(kChartRowsMax) * board_tok::kChartRowMinH * sc + gap +
           board_tok::kChartLineH * sc;
}

// The Runs tab's chart least height: its rows at their least, `tok::kS2` (the chart's own gap, see
// `chart_parts`), then the line about this run.
inline float board_runs_min_h(float sc) {
    return static_cast<float>(kChartRowsMax) * board_tok::kRunsRowMinH * sc + tok::kS2 * sc +
           board_tok::kChartLineH * sc;
}

// The chart gets a column where the left column is tall enough to give it its least height under
// its label.
inline bool board_chart_fits(float left_h, float sc, float gap) {
    return left_h - board_tok::kLabelH * sc - gap >= board_chart_min_h(sc, gap);
}

// The Runs tab as placed: the player's chart over the whole body, its view button in the actions'
// row. The tab exists only where the Board tab has no room for the chart beside its table.
struct SummaryRuns {
    bool shown = false;
    RectF label;  // "Your runs on this song"; empty when the body is too short for it
    RectF chart;  // `chart_parts` splits it into the runs and the line about this run
    RectF view;   // the button that cycles the chart's views, a hit square
};

// The Board tab as placed.
struct SummaryBoard {
    bool shown = false;
    RectF head;   // the points and the chips, left to right
    std::array<RectF, 3> period{}; // Week, Month, All
    RectF caption; // empty when there is no room
    RectF table_head;
    int row_count = 0;  // lines placed: the ones that fit
    int total_rows = 0; // the table's lines: all of the household's; more than row_count scrolls
    float row_gap = 0.0f;
    RectF rows_view;    // where the lines are, as one window (the table scrolls in it)
    std::array<RectF, board_tok::kMaxRows> rows{};
    RectF footnote; // empty when dropped
    RectF divider;  // the line between the columns; empty with no chart
    RectF chart_label;
    RectF chart;    // empty when dropped
    RectF chart_view; // the button that cycles the chart's views, in the actions' row; empty with no chart
    float row_h = 0.0f;
    bool scrolls() const { return total_rows > row_count; }
};

} // namespace etude::app::ui
