// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the summary page's Board tab go: every control a touch target, all inside the
// card, clear of each other and of the table; what a short area gives up, in order.
#include "atkinson_metrics.hpp"
#include "ui/summary_board_text.hpp"
#include "ui/summary_chart_layout.hpp"
#include "ui/summary_layout.hpp"

#include "etude/look.hpp"
#include "etude/panel_size.hpp"
#include "etude/screen_layout.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <vector>

using namespace etude;
using namespace etude::app::ui;

namespace {

struct ScreenSize {
    float w, h;
};
constexpr ScreenSize kSizes[] = {{800, 480}, {1024, 600}, {1366, 768}, {1920, 1080}, {2560, 1440}};

ScreenLayout screen_of(const ScreenSize& s, float scale) {
    return compute_layout(s.w, s.h, kDefaultPxPerMm * scale, LookSettings{}, PinnedBar{});
}

bool inside(const RectF& r, const RectF& area) {
    constexpr float kEps = 0.01f;
    return r.x >= area.x - kEps && r.y >= area.y - kEps && r.right() <= area.right() + kEps &&
           r.bottom() <= area.bottom() + kEps;
}

bool overlap(const RectF& a, const RectF& b) {
    constexpr float kEps = 0.01f;
    return a.x < b.right() - kEps && b.x < a.right() - kEps && a.y < b.bottom() - kEps &&
           b.y < a.bottom() - kEps;
}

BoardContent board_of(int rows, bool chart = true, bool footnote = true) {
    BoardContent b;
    b.on = true;
    b.rows = rows;
    b.chart = chart;
    b.footnote = footnote;
    return b;
}

SummaryLayout board_layout(const ScreenSize& sz, float scale, const BoardContent& b) {
    const ScreenLayout lay = screen_of(sz, scale);
    const float u = ui_scale(sz.w, sz.h, kDefaultPxPerMm * scale);
    return summary_layout(summary_area(lay), u, {3, true, true}, b, card_size(lay.note_area, u).scale);
}

// The page in an area 700 wide (an 800x480 window's, narrowed): its card is under the width from
// which the page has two columns, so it is the narrow form.
SummaryLayout narrow_layout(const BoardContent& b) {
    const ScreenLayout lay = screen_of({800, 480}, 1.0f);
    const float u = ui_scale(800.0f, 480.0f, kDefaultPxPerMm);
    RectF area = summary_area(lay);
    area.w = 700.0f;
    return summary_layout(area, u, {3, true, true}, b, card_size(lay.note_area, u).scale);
}

// The pressables of the page, which must be a hit and clear of each other.
std::vector<RectF> pressables(const SummaryLayout& l) {
    std::vector<RectF> out{l.back, l.pin, l.done, l.actions};
    for (int i = 0; i < l.tab_count; ++i) {
        out.push_back(l.tabs[static_cast<std::size_t>(i)]);
    }
    for (const RectF& p : l.board.period) {
        out.push_back(p);
    }
    for (const RectF& v : {l.board.chart_view, l.runs.view}) {
        if (!v.empty()) {
            out.push_back(v);
        }
    }
    return out;
}

// Every placed part that is not a pressable, for "inside the card" and "clear of the chart".
std::vector<RectF> parts(const SummaryLayout& l) {
    const SummaryBoard& b = l.board;
    std::vector<RectF> out{l.title, b.head};
    if (!b.table_head.empty()) {
        out.push_back(b.table_head);
    }
    for (int i = 0; i < b.row_count; ++i) {
        out.push_back(b.rows[static_cast<std::size_t>(i)]);
    }
    for (const RectF& r : {b.caption, b.footnote, b.chart, b.chart_label}) {
        if (!r.empty()) {
            out.push_back(r);
        }
    }
    return out;
}

} // namespace

TEST_CASE("board layout: every control is a hit, inside the card and clear of the others") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            CAPTURE(sz.w);
            CAPTURE(scale);
            const SummaryLayout l = board_layout(sz, scale, board_of(4));
            REQUIRE_FALSE(l.card.empty());
            if (!l.fits) {
                CHECK(sz.w <= 1024.0f);
                CHECK(scale > 1.0f);
                continue;
            }
            CHECK(l.board.shown);
            const float hit = tok::kHit * l.scale;
            const std::vector<RectF> press = pressables(l);
            for (std::size_t i = 0; i < press.size(); ++i) {
                CHECK(inside(press[i], l.card));
                CHECK(press[i].h >= hit - 0.01f);
                CHECK(press[i].w >= hit - 0.01f);
                for (std::size_t j = i + 1; j < press.size(); ++j) {
                    CHECK_FALSE(overlap(press[i], press[j]));
                }
            }
            const std::vector<RectF> body = parts(l);
            for (std::size_t i = 0; i < body.size(); ++i) {
                CHECK(inside(body[i], l.card));
                for (const RectF& p : press) {
                    CHECK_FALSE(overlap(body[i], p));
                }
                for (std::size_t j = i + 1; j < body.size(); ++j) {
                    // The title and the table's parts are in different rows of the card.
                    CHECK_FALSE(overlap(body[i], body[j]));
                }
            }
        }
    }
}

TEST_CASE("board layout: the table's columns stay inside a row and apart") {
    bool slim_seen = false;
    bool full_seen = false;
    for (const SummaryLayout& l : {board_layout({800, 480}, 1.0f, board_of(4)),
                                   board_layout({1366, 768}, 1.0f, board_of(4)),
                                   narrow_layout(board_of(4))}) {
        REQUIRE(l.fits);
        REQUIRE(l.board.row_count >= 3);
        const RectF& row = l.board.rows[0];
        const BoardColumns c = board_columns(row, l.scale);
        const std::array<RectF, 5> cols{c.rank, c.player, c.accuracy, c.speed, c.points};
        for (std::size_t i = 0; i < cols.size(); ++i) {
            if (cols[i].empty()) {
                continue; // a narrow table has no accuracy and speed
            }
            CHECK(inside(cols[i], row));
            for (std::size_t j = i + 1; j < cols.size(); ++j) {
                CHECK_FALSE(overlap(cols[i], cols[j]));
            }
        }
        CHECK_FALSE(c.player.empty());
        CHECK_FALSE(c.points.empty());
        CHECK(c.player.w >= board_tok::kPlayerMinW * l.scale); // room for an avatar and a name
        // The table drops the two figures between the player and the points when what the columns
        // leave the player is under `kPlayerMinW`; the wide table keeps them.
        const float pad = tok::kS3 * l.scale;
        const float left = row.w - 2.0f * pad -
                           (board_tok::kRankW + board_tok::kPointsW + board_tok::kSpeedW +
                            board_tok::kAccuracyW) * l.scale;
        const bool slim = left < board_tok::kPlayerMinW * l.scale;
        CHECK(c.accuracy.empty() == slim);
        CHECK(c.speed.empty() == slim);
        slim_seen = slim_seen || slim;
        full_seen = full_seen || !slim;
        // The player has what is left of the row, with the two figures' room when it keeps them.
        CHECK(c.player.w == doctest::Approx(slim ? left + (board_tok::kSpeedW + board_tok::kAccuracyW) * l.scale
                                                 : left));
    }
    CHECK(slim_seen);
    CHECK(full_seen);
}

TEST_CASE("board layout: a wide card has the chart in its own column, and so does a narrow one") {
    const SummaryLayout wide = board_layout({1366, 768}, 1.0f, board_of(4));
    REQUIRE(wide.fits);
    CHECK(wide.wide);
    REQUIRE_FALSE(wide.board.chart.empty());
    CHECK(wide.board.chart.x > wide.board.rows[0].right());
    CHECK(wide.board.divider.x > wide.board.rows[0].right());
    CHECK(wide.board.divider.x < wide.board.chart.x);
    CHECK_FALSE(wide.board.footnote.empty());
    CHECK(wide.board.chart.h >= board_chart_min_h(wide.scale, tok::kHitGap * wide.scale) - 0.01f);
    // The wide page has its points over the period track.
    CHECK(wide.board.period[0].y > wide.board.head.bottom() - 0.01f);
    const SummaryLayout narrow = narrow_layout(board_of(4));
    REQUIRE(narrow.fits);
    CHECK_FALSE(narrow.wide);
    REQUIRE_FALSE(narrow.board.chart.empty());
    CHECK(narrow.board.chart.x > narrow.board.rows[0].right());
    CHECK(narrow.board.chart.h >= board_chart_min_h(narrow.scale, tok::kHitGap * narrow.scale) - 0.01f);
    CHECK(narrow.board.row_count >= 3); // the table gave up its heads and footnote, not its rows
    // The narrow page's points and track share one row, in the table's column, the track's cells
    // as narrow as a hit allows; the chart's column starts at the row's top.
    CHECK(narrow.board.period[0].y == doctest::Approx(narrow.board.head.y));
    CHECK(narrow.board.period[0].x > narrow.board.head.right());
    CHECK(narrow.board.period[2].right() <= narrow.board.rows[0].right() + 0.01f);
    CHECK(narrow.board.period[0].w == doctest::Approx(board_tok::kPeriodSlimW * narrow.scale));
    CHECK(narrow.board.divider.y == doctest::Approx(narrow.board.head.y));
}

TEST_CASE("board layout: 800x480 at 96 dpi keeps the chart beside four table lines and their heads") {
    // The page is 680 wide there, so it is the wide form; short of height it puts the points and the
    // track in one row (before the chart goes), as the narrow form always does.
    const SummaryLayout l = board_layout({800, 480}, 1.0f, board_of(4));
    REQUIRE(l.fits);
    CHECK(l.wide);
    CHECK(l.board.row_count == 4);
    CHECK_FALSE(l.board.scrolls());
    CHECK_FALSE(l.board.table_head.empty());
    CHECK_FALSE(l.board.chart.empty());
    CHECK(l.board.chart.x > l.board.rows[0].right());
    CHECK(l.board.period[0].y == doctest::Approx(l.board.head.y));
    // With all the height of 1366x768 the wide page keeps its points over the track.
    const SummaryLayout big = board_layout({1366, 768}, 1.0f, board_of(4));
    CHECK(big.board.period[0].y > big.board.head.bottom() - 0.01f);
}

TEST_CASE("board layout: the chart's view button is a hit ending the actions' row, under the chart") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.5f}) {
            const SummaryLayout l = board_layout(sz, scale, board_of(4));
            if (!l.fits || l.board.chart.empty()) {
                continue;
            }
            CAPTURE(sz.w);
            CAPTURE(scale);
            const RectF& v = l.board.chart_view;
            CHECK(v.w >= tok::kHit * l.scale - 0.01f);
            CHECK(v.h >= tok::kHit * l.scale - 0.01f);
            CHECK(v.right() == doctest::Approx(l.card.right() - (l.compact ? tok::kS2 : tok::kS4) * l.scale));
            CHECK(v.y >= l.board.chart.bottom() - 0.01f); // under the chart, in the actions' row
            CHECK(v.y == doctest::Approx(l.actions.y));
            CHECK_FALSE(overlap(v, l.actions));
            CHECK(l.actions.right() <= v.x - tok::kHitGap * l.scale + 0.01f); // 2 mm of dead gap
        }
    }
}

TEST_CASE("board layout: the chart's parts stay in its rectangle, and a row is never under its least") {
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            const SummaryLayout l = board_layout(sz, scale, board_of(4));
            if (!l.fits || l.board.chart.empty()) {
                continue;
            }
            CAPTURE(sz.w);
            CAPTURE(scale);
            const ChartParts p = chart_parts(l.board.chart, l.scale);
            CHECK(inside(p.plot, l.board.chart));
            CHECK(inside(p.line, l.board.chart));
            CHECK_FALSE(overlap(p.plot, p.line));
            CHECK(p.line.h == doctest::Approx(board_tok::kChartLineH * l.scale));
            CHECK(p.pitch >= board_tok::kChartRowMinH * l.scale - 0.01f);
            for (int i = 0; i < kChartRowsMax; ++i) {
                CHECK(inside(p.row(i), p.plot));
            }
            CHECK(p.row(kChartRowsMax - 1).bottom() == doctest::Approx(p.plot.bottom()));
        }
    }
}

TEST_CASE("board layout: no chart when the player has no runs to chart") {
    const SummaryLayout l = board_layout({1366, 768}, 1.0f, board_of(4, false));
    REQUIRE(l.fits);
    CHECK(l.board.chart.empty());
    CHECK(l.board.rows[0].w > 600.0f * l.scale * 0.5f);
}

TEST_CASE("board layout: a short area gives up the spacing, the footnote, the chart, then rows "
          "(which scroll)") {
    const ScreenLayout lay = screen_of({1366, 768}, 1.0f);
    const float u = ui_scale(1366.0f, 768.0f, kDefaultPxPerMm);
    RectF area = summary_area(lay);
    const float sc = card_size(lay.note_area, u).scale;
    bool compact_seen = false;
    bool footnote_gone = false;
    bool chart_gone = false;
    bool rows_cut = false;
    bool one_row_seen = false;      // the points beside Week | Month | All
    bool one_row_with_chart = false; // ... while the chart is still drawn
    for (float h = area.h; h > 150.0f; h -= 4.0f) {
        area.h = h;
        const SummaryLayout l = summary_layout(area, u, {3, true, true}, board_of(6), sc);
        if (!l.fits) {
            continue;
        }
        compact_seen = compact_seen || l.compact;
        footnote_gone = footnote_gone || l.board.footnote.empty();
        chart_gone = chart_gone || l.board.chart.empty();
        rows_cut = rows_cut || l.board.row_count < 6;
        // The one-row step (the points and the track on one row) comes after the footnote and before
        // the chart goes: it is what keeps the chart, so no frame has lost the chart without it.
        const bool one_row = l.board.period[0].y == doctest::Approx(l.board.head.y);
        one_row_seen = one_row_seen || one_row;
        one_row_with_chart = one_row_with_chart || (one_row && !l.board.chart.empty());
        if (one_row) {
            CHECK(footnote_gone);
        }
        if (chart_gone) {
            CHECK(one_row_seen);
            CHECK(one_row);
        }
        // The footnote goes before the chart, the chart before any row.
        if (!l.board.footnote.empty()) {
            CHECK_FALSE(chart_gone);
        }
        if (!l.board.chart.empty()) {
            CHECK_FALSE(rows_cut);
        }
        if (!footnote_gone) {
            CHECK_FALSE(rows_cut);
        }
        CHECK(l.board.row_count >= (l.board.table_head.empty() ? 1 : board_tok::kMinRows));
        CHECK(l.board.total_rows == 6); // nobody is left out: the lines that do not fit scroll
        CHECK(l.board.scrolls() == (l.board.row_count < 6));
        CHECK_FALSE(l.back.empty());
        CHECK_FALSE(l.actions.empty());
        CHECK(inside(l.card, area));
    }
    CHECK(compact_seen);
    CHECK(footnote_gone);
    CHECK(chart_gone);
    CHECK(rows_cut);
    CHECK(one_row_seen);
    CHECK(one_row_with_chart);
}

// Every line of the table can be brought fully into its window by scrolling: where `l` scrolls, to
// `board_scroll_to(i)`, else as placed.
void check_every_line_reachable(const SummaryLayout& l, int players) {
    const SummaryBoard& b = l.board;
    REQUIRE(b.total_rows == players);
    REQUIRE(b.row_count >= 1);
    REQUIRE(b.row_count <= b.total_rows);
    const float pitch = b.row_h + b.row_gap;
    const float view_h = b.rows_view.h;
    CHECK(view_h == doctest::Approx(static_cast<float>(b.row_count) * pitch - b.row_gap));
    CHECK(inside(b.rows_view, l.card));
    CHECK(b.scrolls() == (b.row_count < players));
    for (int i = 0; i < players; ++i) {
        const float scroll = board_scroll_to(i, players, b.row_h, b.row_gap, view_h);
        CHECK(scroll >= 0.0f);
        CHECK(scroll <= board_scroll_max(players, b.row_h, b.row_gap, view_h) + 0.01f);
        const float top = static_cast<float>(i) * pitch - scroll;
        CAPTURE(i);
        CHECK(top >= -0.01f);
        CHECK(top + b.row_h <= view_h + 0.01f);
    }
}

TEST_CASE("board layout: a big household is all in the table, the lines that do not fit scroll") {
    const SummaryLayout l = board_layout({800, 480}, 1.0f, board_of(board_tok::kMaxRows));
    REQUIRE(l.fits);
    CHECK(l.board.row_count >= board_tok::kMinRows);
    CHECK(l.board.row_count < board_tok::kMaxRows);
    check_every_line_reachable(l, board_tok::kMaxRows);
}

TEST_CASE("board layout: four players are all reachable at 800x480 and at 1366x768, three lines "
          "at the least") {
    for (const ScreenSize& sz : {ScreenSize{800, 480}, ScreenSize{1366, 768}}) {
        CAPTURE(sz.w);
        const SummaryLayout l = board_layout(sz, 1.0f, board_of(4));
        REQUIRE(l.fits);
        CHECK(l.board.row_count >= board_tok::kMinRows);
        check_every_line_reachable(l, 4);
        // The table is a window of its own under the points and the track, and over the footnote.
        CHECK(l.board.rows_view.y >= l.board.head.bottom() - 0.01f);
        if (!l.board.footnote.empty()) {
            CHECK(l.board.rows_view.bottom() <= l.board.footnote.y + 0.01f);
        }
    }
}

TEST_CASE("board layout: on the 7 inch panel (154x86 mm, 800x480) four players are all reachable by "
          "scrolling") {
    // The panel's points are fewer than a 96 dpi 800x480 window's: the page may cover the chord strip
    // and the keyboard's top two thirds, and still has room for one line there; the other three are
    // a drag away, and the chart does not fit.
    const float px_per_mm = panel_px_per_mm(800.0f, 480.0f, {154.0f, 86.0f});
    const ScreenLayout lay = compute_layout(800.0f, 480.0f, px_per_mm, LookSettings{}, PinnedBar{});
    const float u = ui_scale(800.0f, 480.0f, px_per_mm);
    const SummaryLayout l = summary_layout(summary_area(lay), u, {3, true, true}, board_of(4),
                                           card_size(lay.note_area, u).scale);
    REQUIRE(l.fits);
    CHECK(l.board.row_count >= 1);
    CHECK(l.board.scrolls());
    check_every_line_reachable(l, 4);
    // Nor is there room for the chart's five rows and its button: it is not drawn, as the card's
    // short body goes to the table.
    CHECK(l.board.chart.empty());
    CHECK(l.board.chart_view.empty());
}

TEST_CASE("board layout: a short narrow card cuts the table's footnote, heads and rows, and the chart "
          "is the last to go") {
    const ScreenLayout lay = screen_of({800, 480}, 1.0f);
    const float u = ui_scale(800.0f, 480.0f, kDefaultPxPerMm);
    RectF area = summary_area(lay);
    area.w = 700.0f; // a narrow card
    const float sc = card_size(lay.note_area, u).scale;
    bool chart_gone = false;
    bool heads_gone = false;
    bool rows_cut = false;
    for (float h = area.h; h > 120.0f; h -= 4.0f) {
        area.h = h;
        const SummaryLayout l = summary_layout(area, u, {3, true, true}, board_of(6), sc);
        const SummaryLayout bare = summary_layout(area, u, {3, true, true}, board_of(6, false), sc);
        if (!l.fits || !bare.fits) {
            continue;
        }
        CAPTURE(h);
        chart_gone = chart_gone || l.board.chart.empty();
        heads_gone = heads_gone || l.board.table_head.empty();
        rows_cut = rows_cut || l.board.row_count < board_tok::kMinRows;
        // The chart costs the table nothing: the same rows and heads as without it. So no row is
        // cut and no head dropped to make room for it, and none is kept for it either.
        CHECK(l.board.row_count == bare.board.row_count);
        CHECK(l.board.table_head.empty() == bare.board.table_head.empty());
        CHECK(l.board.total_rows == 6);
        // It is present exactly while the table's column is tall enough for its five rows: no
        // earlier cut, in height or in the table, takes it away.
        const SummaryBoard& b = l.board;
        const float bottom = b.footnote.empty() ? b.rows_view.bottom() : b.footnote.bottom();
        const float body_h = bottom - b.head.y;
        const bool fits = board_chart_fits(body_h, l.scale, tok::kHitGap * l.scale);
        CHECK(b.chart.empty() == !fits);
        if (!b.chart.empty()) {
            CHECK(b.row_count >= board_tok::kMinRows);
            CHECK(b.chart.h >= board_chart_min_h(l.scale, tok::kHitGap * l.scale) - 0.01f);
        }
        // Once the chart is gone the table is on its own for the rest of the way down.
        if (chart_gone) {
            CHECK(b.chart.empty());
        }
        // No table row is cut while the chart is still drawn, so the chart outlasts the cut from
        // three rows to one.
        if (rows_cut) {
            CHECK(chart_gone);
        }
    }
    CHECK(heads_gone);
    CHECK(rows_cut);
    CHECK(chart_gone);
}

TEST_CASE("board layout: a table that scrolls opens at the player's own line, never past the end") {
    const float row_h = 34.0f;
    const float gap = 4.0f;
    const float view_h = 3.0f * row_h + 2.0f * gap; // three lines show
    // The leader or no profile in use: the top.
    CHECK(board_first_scroll(6, 0, row_h, gap, view_h) == doctest::Approx(0.0f));
    CHECK(board_first_scroll(6, -1, row_h, gap, view_h) == doctest::Approx(0.0f));
    // A line that is on show stays where it is.
    CHECK(board_first_scroll(6, 2, row_h, gap, view_h) == doctest::Approx(0.0f));
    // Further down: the line is the last one in view.
    CHECK(board_first_scroll(6, 4, row_h, gap, view_h) ==
          doctest::Approx(5.0f * (row_h + gap) - gap - view_h));
    // The last line: the end of the table, no further.
    CHECK(board_first_scroll(6, 5, row_h, gap, view_h) ==
          doctest::Approx(board_scroll_max(6, row_h, gap, view_h)));
    CHECK(board_first_scroll(6, 99, row_h, gap, view_h) ==
          doctest::Approx(board_scroll_max(6, row_h, gap, view_h)));
    // A table that fits does not scroll.
    CHECK(board_scroll_max(3, row_h, gap, view_h) == doctest::Approx(0.0f));
    CHECK(board_first_scroll(3, 2, row_h, gap, view_h) == doctest::Approx(0.0f));
}

TEST_CASE("board layout: the chart's longest words fit its column at the real font") {
    // The foot's longest line is in bold at the caption size, in its brief form when the long one
    // does not fit; the rows' points are the widest "12,345", the day the longest of the week (a
    // date of another year is cut by the column).
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            const SummaryLayout l = board_layout(sz, scale, board_of(4));
            if (!l.fits || l.board.chart.empty()) {
                continue;
            }
            CAPTURE(sz.w);
            CAPTURE(scale);
            const float size = tok::kCaption * l.scale;
            RunsChart chart;
            chart.place = RunPlace::Below;
            chart.run_score = 12'345;
            chart.behind = 1'050;
            FitText full;
            FitText brief;
            board_chart_line_words(chart, &full);
            board_chart_line_words(chart, &brief, true);
            CHECK(full.view() == "12,345 \xC2\xB7 1,050 under your 5th best");
            CHECK(brief.view() == "12,345 \xC2\xB7 1,050 under 5th");
            // Either the long line fits, or the brief one does.
            const float long_w = test::text_em(true, full.view()) * size;
            const float brief_w = test::text_em(true, brief.view()) * size;
            CHECK(std::min(long_w, brief_w) <= l.board.chart.w);
            // A bar has room beside the day's and the points' words: at least 15% of the width.
            const ChartParts p = chart_parts(l.board.chart, l.scale);
            const float day = test::text_em(true, "Yesterday") * size;
            const float points = test::text_em(true, "12,345") * size;
            const float room = p.plot.w - std::min(day, p.plot.w * 0.38f) - points -
                               (tok::kIconSm + 3.0f * tok::kS2) * l.scale;
            CHECK(room >= p.plot.w * 0.15f);
        }
    }
}

TEST_CASE("board layout: a line chart's points go over a dot that has no room under it before the days") {
    const float size = 13.0f;
    const float reach = 13.0f; // from the dot's centre to the label's top
    const float days_top = 100.0f;
    CHECK(chart_points_fit_below(40.0f, reach, size, days_top));  // high in the plot: under it
    CHECK(chart_points_fit_below(74.0f, reach, size, days_top));  // exactly touching the line
    CHECK_FALSE(chart_points_fit_below(75.0f, reach, size, days_top));
    CHECK_FALSE(chart_points_fit_below(95.0f, reach, size, days_top)); // the lowest dot: over it
}

// ---- The Runs tab: the chart alone, where the Board tab has no room for it ----------------------

namespace {

// The real 7 inch panel at 800x480 (154x86 mm), or a window of `sz` at 96 dpi times `scale`.
struct PagePlace {
    ScreenLayout lay;
    float u;
    RectF area;
    float card_scale;
};

PagePlace page_place(const ScreenSize& sz, float scale, bool panel_7in = false) {
    const float px_per_mm =
        panel_7in ? panel_px_per_mm(sz.w, sz.h, {154.0f, 86.0f}) : kDefaultPxPerMm * scale;
    PagePlace p;
    p.lay = compute_layout(sz.w, sz.h, px_per_mm, LookSettings{}, PinnedBar{});
    p.u = ui_scale(sz.w, sz.h, px_per_mm);
    p.area = summary_area(p.lay);
    p.card_scale = card_size(p.lay.note_area, p.u).scale;
    return p;
}

BoardContent runs_of(int rows, bool chart = true) {
    BoardContent b = board_of(rows, chart);
    b.on = false;
    b.runs = true;
    return b;
}

SummaryLayout runs_layout(const PagePlace& p, int rows = 4, bool chart = true) {
    return summary_layout(p.area, p.u, {3, true, true}, runs_of(rows, chart), p.card_scale);
}
SummaryLayout board_layout_at(const PagePlace& p, int rows = 4, bool chart = true) {
    return summary_layout(p.area, p.u, {3, true, true}, board_of(rows, chart), p.card_scale);
}

// The least gap between two rectangles that do not overlap (0 when they touch).
float gap_between(const RectF& a, const RectF& b) {
    const float dx = std::max({a.x - b.right(), b.x - a.right(), 0.0f});
    const float dy = std::max({a.y - b.bottom(), b.y - a.bottom(), 0.0f});
    return std::max(dx, dy);
}

// The hit areas of the header's track, as `segment_cells` makes them (`segment_hit`).
std::vector<RectF> tab_hits(const SummaryLayout& l) {
    std::vector<RectF> out;
    for (int i = 0; i < l.tab_count; ++i) {
        out.push_back(segment_hit(l.tabs[static_cast<std::size_t>(i)], i, l.tab_count,
                                  tok::kHitGap * l.scale));
    }
    return out;
}

// Whether the Runs body (the chart at its least, a card no shorter than the Board's) fits `area`.
bool runs_plan_fits(const RectF& area, float u, float card_scale, int rows) {
    const CardSize size = summary_size(area, u, card_scale);
    const summary_detail::Plan plan =
        summary_detail::fit_plan(area, size, {3, true, true}, runs_of(rows));
    return plan.total() <= area.h - 2.0f * card_tok::kMargin * plan.sc;
}

} // namespace

TEST_CASE("runs tab: it is there exactly where the Board tab has no room for the chart beside its table") {
    int with_runs = 0;
    int without = 0;
    for (const ScreenSize& sz : kSizes) {
        for (const float scale : {1.0f, 1.25f, 1.5f}) {
            for (const bool panel : {false, true}) {
                if (panel && (sz.w != 800.0f || scale != 1.0f)) {
                    continue; // the panel's own size is the 7 inch one at 800x480 only
                }
                CAPTURE(sz.w);
                CAPTURE(scale);
                CAPTURE(panel);
                const PagePlace p = page_place(sz, scale, panel);
                for (const int rows : {1, 4, board_tok::kMaxRows}) {
                    const SummaryLayout board = board_layout_at(p, rows);
                    const SummaryLayout runs = runs_layout(p, rows);
                    REQUIRE_FALSE(board.card.empty());
                    CHECK(board.runs_tab ==
                          (board.board.chart.empty() && runs_plan_fits(p.area, p.u, p.card_scale, rows)));
                    CHECK(runs.runs_tab == board.runs_tab);
                    CHECK(board.tab_count == (board.runs_tab ? 3 : 2));
                    CHECK(runs.tab_count == board.tab_count);
                    CHECK(runs.runs.shown == board.runs_tab);
                    // Where there is no Runs tab, asking for one gives the Board tab as it was.
                    CHECK(runs.board.shown == !board.runs_tab);
                    (board.runs_tab ? with_runs : without) += 1;
                    // The Timing tab has the same track as the Board's.
                    BoardContent on_timing;
                    on_timing.rows = rows;
                    const SummaryLayout timing =
                        summary_layout(p.area, p.u, {3, true, true}, on_timing, p.card_scale);
                    CHECK(timing.runs_tab == board.runs_tab);
                    // A player with no runs has nothing to chart: no Runs tab at all.
                    const SummaryLayout bare = runs_layout(p, rows, false);
                    CHECK_FALSE(bare.runs_tab);
                    CHECK(bare.tab_count == 2);
                    CHECK_FALSE(bare.runs.shown);
                }
            }
        }
    }
    CHECK(with_runs > 0);
    CHECK(without > 0);
}

TEST_CASE("runs tab: a short page gets it where the chart goes, whatever the height") {
    const PagePlace p = page_place({1366, 768}, 1.0f);
    RectF area = p.area;
    bool seen_with = false;
    bool seen_without = false;
    bool seen_squeezed = false;
    for (float h = area.h; h > 150.0f; h -= 4.0f) {
        area.h = h;
        for (const bool narrow : {false, true}) {
            RectF a = area;
            if (narrow) {
                a.w = 700.0f;
            }
            const SummaryLayout board =
                summary_layout(a, p.u, {3, true, true}, board_of(6), p.card_scale);
            const SummaryLayout runs =
                summary_layout(a, p.u, {3, true, true}, runs_of(6), p.card_scale);
            CAPTURE(h);
            CAPTURE(narrow);
            // Offered exactly where the Board drops the chart and the Runs body fits.
            const bool fits_runs = runs_plan_fits(a, p.u, p.card_scale, 6);
            CHECK(board.runs_tab == (board.board.chart.empty() && fits_runs));
            CHECK(runs.runs_tab == board.runs_tab);
            seen_with = seen_with || board.runs_tab;
            seen_without = seen_without || !board.runs_tab;
            seen_squeezed = seen_squeezed || (board.board.chart.empty() && !board.runs_tab);
            if (runs.runs_tab) {
                // A Runs tab that is offered really fits: the chart at its least, clear of the
                // actions' row, inside the card and the area, and a card no shorter than the Board's.
                CHECK(runs.runs.shown);
                CHECK(runs.fits);
                CHECK(inside(runs.card, a));
                CHECK(runs.runs.chart.h >= board_runs_min_h(runs.scale) - 0.01f);
                CHECK(runs.runs.chart.bottom() <= runs.actions.y - tok::kHitGap * runs.scale + 0.01f);
                CHECK(runs.runs.view.bottom() <= runs.card.bottom() + 0.01f);
                CHECK(runs.card.h >= board.card.h - 0.01f);
            } else {
                // Asked for where there is none, the page shows the Board tab.
                CHECK(runs.board.shown);
                CHECK_FALSE(runs.runs.shown);
            }
        }
    }
    CHECK(seen_with);
    CHECK(seen_without);
    CHECK(seen_squeezed); // short of the room for either: no Runs tab, as no chart
}

TEST_CASE("runs tab: on the 7 inch panel (154x86 mm, 800x480) the chart and its button fill the body") {
    const PagePlace p = page_place({800, 480}, 1.0f, true);
    const SummaryLayout board = board_layout_at(p);
    REQUIRE(board.fits);
    CHECK(board.board.chart.empty()); // the table's body holds one line, not the chart
    CHECK(board.runs_tab);
    CHECK(board.tab_count == 3);
    const SummaryLayout l = runs_layout(p);
    REQUIRE(l.fits);
    REQUIRE(l.runs.shown);
    CHECK_FALSE(l.board.shown);
    // The chart gets every row of the bars at its least height and the line about this run.
    CHECK(l.runs.chart.h >= board_runs_min_h(l.scale) - 0.01f);
    const ChartParts parts_of_chart = chart_parts(l.runs.chart, l.scale);
    CHECK(parts_of_chart.pitch >= board_tok::kRunsRowMinH * l.scale - 0.01f);
    CHECK(inside(parts_of_chart.plot, l.runs.chart));
    CHECK(inside(parts_of_chart.line, l.runs.chart));
    CHECK(l.runs.chart.w >= l.card.w * 0.8f); // the whole width, not a column
    // The card is as tall as the Board tab's, or taller for the chart's rows.
    CHECK(l.card.h >= board.card.h - 0.01f);
    // The view button is a hit ending the actions' row, 2 mm from the chips.
    const RectF& v = l.runs.view;
    CHECK(v.w >= tok::kHit * l.scale - 0.01f);
    CHECK(v.h >= tok::kHit * l.scale - 0.01f);
    CHECK(v.y == doctest::Approx(l.actions.y));
    CHECK(v.right() == doctest::Approx(l.card.right() - (l.compact ? tok::kS2 : tok::kS4) * l.scale));
    CHECK(l.actions.right() <= v.x - tok::kHitGap * l.scale + 0.01f);
    CHECK(v.y >= l.runs.chart.bottom() - 0.01f);
    if (!l.runs.label.empty()) {
        CHECK(inside(l.runs.label, l.card));
        CHECK_FALSE(overlap(l.runs.label, l.runs.chart));
    }
    CHECK(inside(l.runs.chart, l.card));
    CHECK(inside(l.title, l.card));
}

TEST_CASE("runs tab: the three-tab track keeps the header's 12 mm hits and 2 mm gaps") {
    const PagePlace panel = page_place({800, 480}, 1.0f, true);
    const PagePlace big = page_place({1366, 768}, 1.0f);
    for (const PagePlace* p : {&panel, &big}) {
        for (const SummaryLayout& l : {runs_layout(*p), board_layout_at(*p)}) {
            REQUIRE(l.fits);
            const bool three = p == &panel;
            CAPTURE(three);
            CHECK(l.tab_count == (three ? 3 : 2));
            CHECK(l.tabs[2].empty() == !three);
            // Every pressable is a hit, inside the card and 2 mm from the others.
            std::vector<RectF> hits{l.back, l.pin, l.done, l.actions};
            for (const RectF& t : tab_hits(l)) {
                hits.push_back(t);
            }
            for (const RectF& v : {l.runs.view, l.board.chart_view}) {
                if (!v.empty()) {
                    hits.push_back(v);
                }
            }
            const float hit = tok::kHit * l.scale;
            const float dead = tok::kHitGap * l.scale;
            for (std::size_t i = 0; i < hits.size(); ++i) {
                CHECK(inside(hits[i], l.card));
                CHECK(hits[i].w >= hit - 0.01f);
                CHECK(hits[i].h >= hit - 0.01f);
                for (std::size_t j = i + 1; j < hits.size(); ++j) {
                    CHECK_FALSE(overlap(hits[i], hits[j]));
                    CHECK(gap_between(hits[i], hits[j]) >= dead - 0.01f);
                }
            }
            // The track sits between the title and the pin, left to right Timing, Board, Runs.
            for (int i = 1; i < l.tab_count; ++i) {
                CHECK(l.tabs[static_cast<std::size_t>(i)].x ==
                      doctest::Approx(l.tabs[static_cast<std::size_t>(i) - 1].right()));
            }
            CHECK(l.title.right() <= l.tabs[0].x + 0.01f);
            CHECK(l.tabs[static_cast<std::size_t>(l.tab_count) - 1].right() <= l.pin.x - dead + 0.01f);
            CHECK(inside(l.title, l.card));
            // The title keeps room for "How it went" at the real font.
            CHECK(l.title.w >= test::text_em(true, "How it went") * tok::kBody * l.scale);
        }
    }
}

TEST_CASE("runs tab: a tab's name fits its cell at the real font") {
    for (const float cell : {board_tok::kTabCellW, board_tok::kTabCell3W}) {
        for (const char* name : {"Timing", "Board", "Runs"}) {
            CAPTURE(cell);
            CAPTURE(name);
            // The selected name is bold; the cell keeps `kS2` of air in all.
            CHECK(test::text_em(true, name) * tok::kLabel <= cell - tok::kS2);
        }
    }
    // A cell is a hit wide even with the dead gap taken off both its sides.
    CHECK(board_tok::kTabCell3W - tok::kHitGap >= tok::kHit);
}
