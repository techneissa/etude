// SPDX-License-Identifier: GPL-3.0-or-later
// Where a settings row's control starts inside a page column: one x for every row of the column,
// whatever its label.
#include "../app/ui/column_check.hpp"
#include "../app/ui/row_columns.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude::app::ui;
using etude::app::ui::column_probe::Row;

namespace {

constexpr float kWidths[] = {200.0f, 272.0f, 314.0f, 372.0f, 520.0f, 760.0f};
constexpr float kScales[] = {1.0f, 1.25f, 1.5f};

// What the widest label of a page column reaches, design points.
constexpr float kLabelNeeds[] = {0.0f, 60.0f, 116.0f, 134.0f, 400.0f};

} // namespace

TEST_CASE("row columns: every wide control of a column starts and ends at one x") {
    for (const float u : kScales) {
        for (const float width : kWidths) {
            for (const float need : kLabelNeeds) {
                const ColumnPlan plan = plan_column(100.0f, 100.0f + width * u, need * u, u);
                // Rows with different labels, cell counts and kinds of control all ask the plan.
                std::vector<Span> beside;
                for (int row = 0; row < 6; ++row) {
                    beside.push_back(wide_span(plan, false, u));
                }
                for (const Span& s : beside) {
                    CHECK(s.x0 == beside.front().x0);
                    CHECK(s.x1 == beside.front().x1);
                }
                CHECK(beside.front().x0 >= plan.left);
                CHECK(beside.front().x1 <= plan.right);
            }
        }
    }
}

TEST_CASE("row columns: the label side fits the widest label, up to a share of the column") {
    const ColumnPlan fits = plan_column(0.0f, 314.0f, 116.0f, 1.0f);
    CHECK(fits.control_x == doctest::Approx(116.0f + kZoneGap));
    CHECK_FALSE(fits.stacked);
    // A label that wants more than the share is cut to it, never the whole column.
    const ColumnPlan huge = plan_column(0.0f, 314.0f, 400.0f, 1.0f);
    CHECK(huge.control_x == doctest::Approx(kMaxZoneShare * 314.0f));
    // Not measured yet: the default share.
    const ColumnPlan unknown = plan_column(0.0f, 314.0f, 0.0f, 1.0f);
    CHECK(unknown.control_x == doctest::Approx(kDefaultZoneShare * 314.0f));
}

TEST_CASE("row columns: compact controls share their right edge, blocks run from the icons to it") {
    const ColumnPlan plan = plan_column(50.0f, 364.0f, 116.0f, 1.0f);
    const Span sw = compact_span(plan, 40.0f);
    const Span pill = compact_span(plan, 68.0f);
    CHECK(sw.x1 == pill.x1);
    CHECK(sw.x1 == plan.control_right);
    CHECK(sw.x0 > pill.x0);
    // Rows' icons start `kRowInset` in from the column's left; a block starts there and ends where
    // every other control does.
    const Span block = block_span(plan, 1.0f);
    CHECK(block.x0 == plan.left + kRowInset);
    CHECK(block.x1 == plan.control_right);
    // The wide controls end where the compact ones do.
    CHECK(wide_span(plan, false, 1.0f).x1 == sw.x1);
}

TEST_CASE("row columns: a column too narrow for a control beside the label stacks them all") {
    // Exactly at the least room, and one point under it.
    const float least = kMinWideControl;
    const float need = 100.0f;
    const float zone = need + kZoneGap;
    const float right_at = zone + kRowInset + least; // control_right - control_x == least
    CHECK_FALSE(plan_column(0.0f, right_at, need, 1.0f).stacked);
    CHECK(plan_column(0.0f, right_at - 1.0f, need, 1.0f).stacked);
    const ColumnPlan narrow = plan_column(10.0f, right_at - 1.0f + 10.0f, need, 1.0f);
    // Every Wide control under its label, at the column's edge, one x for them all.
    CHECK(wide_span(narrow, false, 1.0f).x0 == narrow.left + kRowInset);
    CHECK(wide_span(narrow, true, 1.0f).x0 == narrow.left + kRowInset);
    // A stepper keeps its row: it ends where the compact controls do.
    CHECK(inline_span(narrow, 1.0f).x1 == narrow.control_right);
    CHECK(inline_span(narrow, 1.0f).w() == doctest::Approx(kMinStepper));
}

TEST_CASE("row columns: one row whose cells cannot share the room goes under its label") {
    const ColumnPlan plan = plan_column(0.0f, 314.0f, 116.0f, 1.0f);
    CHECK_FALSE(plan.stacked);
    const Span beside = wide_span(plan, false, 1.0f);
    const Span below = wide_span(plan, true, 1.0f);
    CHECK(beside.x0 == plan.control_x);
    CHECK(below.x0 == plan.left + kRowInset);
    CHECK(below.x1 == plan.control_right);
    // The same x and edge as a row with no label.
    CHECK(below.x0 == block_span(plan, 1.0f).x0);
    CHECK(below.x1 == block_span(plan, 1.0f).x1);
}

TEST_CASE("label zones: a frame's rows read what the frame before reported, the widest of it") {
    LabelZones z;
    // Frame 1: nothing measured yet.
    CHECK(z.need(7, 1) == 0.0f);
    z.report(7, 1, 90.0f);
    z.report(7, 1, 120.0f);
    z.report(7, 1, 100.0f);
    CHECK(z.need(7, 1) == 0.0f); // still frame 1: every row of it reads the same value
    // Frame 2: the widest of frame 1, for every row.
    CHECK(z.need(7, 2) == 120.0f);
    z.report(7, 2, 80.0f);
    CHECK(z.need(7, 2) == 120.0f);
    // Frame 3 sees frame 2's widest, not frame 1's.
    CHECK(z.need(7, 3) == 80.0f);
}

TEST_CASE("label zones: columns are kept apart, a page not drawn for a while keeps its zone") {
    LabelZones z;
    z.report(1, 5, 140.0f);
    z.report(2, 5, 90.0f);
    CHECK(z.need(1, 6) == 140.0f);
    CHECK(z.need(2, 6) == 90.0f);
    // Page 1 is drawn on frame 6 and not again until frame 16: its zone is there at once.
    z.report(1, 6, 140.0f);
    CHECK(z.need(1, 16) == 140.0f);
}

TEST_CASE("label zones: a full table still answers") {
    LabelZones z;
    for (std::uint32_t slot = 0; slot < 200; ++slot) {
        z.report(slot, 1, static_cast<float>(slot));
    }
    CHECK(z.need(3, 2) >= 0.0f); // no crash, no out of range
}

namespace {
Row row(int scope, int column, RowKind kind, float x0, float x1, bool stacked = false,
        bool radio = false) {
    Row r;
    r.scope = scope;
    r.column = column;
    r.kind = kind;
    r.stacked = stacked;
    r.radio = radio;
    r.span = {x0, x1};
    return r;
}
} // namespace

TEST_CASE("column check: rows whose controls start at the label's end are caught") {
    // The Notes page before the columns kept one x: each row's track began wherever its label
    // ended (201, 245, 280, 322 on one page).
    const std::vector<Row> ragged = {
        row(1, 1, RowKind::Wide, 201.0f, 480.0f), row(1, 1, RowKind::Wide, 245.0f, 480.0f),
        row(1, 1, RowKind::Wide, 280.0f, 480.0f), row(1, 1, RowKind::Wide, 322.0f, 480.0f)};
    CHECK(column_probe::check(ragged).size() == 3);
    // Slider tracks at 333 and 341 are the same defect.
    CHECK(column_probe::check({row(2, 0, RowKind::Wide, 333.0f, 480.0f),
                               row(2, 0, RowKind::Wide, 341.0f, 480.0f)})
              .size() == 1);
}

TEST_CASE("column check: rows from the plan pass, each kind and column on its own") {
    const ColumnPlan left = plan_column(100.0f, 414.0f, 116.0f, 1.0f);
    const ColumnPlan right = plan_column(438.0f, 752.0f, 98.0f, 1.0f);
    std::vector<Row> rows;
    for (const ColumnPlan* c : {&left, &right}) {
        const int column = c == &left ? 0 : 1;
        for (int i = 0; i < 4; ++i) {
            const Span wide = wide_span(*c, false, 1.0f);
            rows.push_back(row(3, column, RowKind::Wide, wide.x0, wide.x1));
            const Span sw = compact_span(*c, 40.0f);
            const Span pill = compact_span(*c, 68.0f);
            rows.push_back(row(3, column, RowKind::Compact, sw.x0, sw.x1));
            rows.push_back(row(3, column, RowKind::Compact, pill.x0, pill.x1));
            const Span block = block_span(*c, 1.0f);
            rows.push_back(row(3, column, RowKind::Block, block.x0, block.x1));
            const Span below = wide_span(*c, true, 1.0f);
            rows.push_back(row(3, column, RowKind::Wide, below.x0, below.x1, true));
        }
    }
    CHECK(column_probe::check(rows).empty());
}

TEST_CASE("column check: compact controls are judged by their right edge only") {
    CHECK(column_probe::check({row(1, 0, RowKind::Compact, 300.0f, 340.0f),
                               row(1, 0, RowKind::Compact, 272.0f, 340.0f)})
              .empty());
    CHECK(column_probe::check({row(1, 0, RowKind::Compact, 300.0f, 340.0f),
                               row(1, 0, RowKind::Compact, 270.0f, 338.0f)})
              .size() >= 1); // the group and the column's edge both see it
}

TEST_CASE("column check: the tolerance is exact at the edge") {
    CHECK(column_probe::check({row(1, 0, RowKind::Wide, 200.0f, 400.0f),
                               row(1, 0, RowKind::Wide, 200.01f, 400.0f)},
                              0.01f)
              .empty());
    CHECK(column_probe::check({row(1, 0, RowKind::Wide, 200.0f, 400.0f),
                               row(1, 0, RowKind::Wide, 200.02f, 400.0f)},
                              0.01f)
              .size() == 1);
}

TEST_CASE("column check: one list of choices stays in one column") {
    // The old Keyboard and Microphone pages put "Auto" in the left column and "Manual" in the
    // right, with nothing else on the page.
    const std::vector<Row> split = {row(7, 0, RowKind::Block, 100.0f, 414.0f, false, true),
                                    row(7, 1, RowKind::Block, 438.0f, 752.0f, false, true)};
    const auto problems = column_probe::check(split);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("second column") != std::string::npos);
    CHECK(column_probe::check({row(7, 1, RowKind::Block, 438.0f, 752.0f, false, true),
                               row(7, 1, RowKind::Block, 438.0f, 752.0f, false, true)})
              .empty());
    // Two pages may each keep their list in a different column.
    CHECK(column_probe::check({row(7, 0, RowKind::Block, 100.0f, 414.0f, false, true),
                               row(8, 1, RowKind::Block, 438.0f, 752.0f, false, true)})
              .empty());
}

TEST_CASE("column check: full-width rows start at the icons' x and end at the shared right edge") {
    const ColumnPlan plan = plan_column(783.0f, 1167.0f, 100.0f, 1.0f);
    const Span sw = compact_span(plan, 40.0f);
    const Span wide = wide_span(plan, false, 1.0f);
    const Span block = block_span(plan, 1.0f);
    const std::vector<Row> good = {row(4, 1, RowKind::Compact, sw.x0, sw.x1),
                                   row(4, 1, RowKind::Wide, wide.x0, wide.x1),
                                   row(4, 1, RowKind::Block, block.x0, block.x1),
                                   row(4, 1, RowKind::Wide, block.x0, block.x1, true)};
    column_probe::Stats stats;
    CHECK(column_probe::check(good, 0.01f, &stats).empty());
    CHECK(stats.comparisons >= 3);
    // A block that ends 12 past the switches' right edge (the Subdivide row's old span).
    std::vector<Row> past = good;
    past[2].span.x1 = plan.right;
    CHECK(column_probe::check(past).size() >= 1);
    // A stacked control that starts 12 left of the row icons (the Resume after row's old span).
    std::vector<Row> left = good;
    left[3].span.x0 = plan.left;
    const auto problems = column_probe::check(left);
    REQUIRE(problems.size() == 1);
    CHECK(problems[0].find("starts at") != std::string::npos);
    // The same two spans were the old full-width rows.
    CHECK(column_probe::check({row(4, 1, RowKind::Compact, sw.x0, sw.x1),
                               row(4, 1, RowKind::Block, plan.left, plan.right)})
              .size() == 1);
}

TEST_CASE("column check: a measuring frame is not judged, and the counts say what was seen") {
    Row hidden = row(5, 0, RowKind::Wide, 10.0f, 300.0f);
    hidden.visible = false;
    column_probe::Stats stats;
    CHECK(column_probe::check({row(5, 0, RowKind::Wide, 200.0f, 300.0f), hidden}, 0.01f, &stats)
              .empty());
    CHECK(stats.comparisons == 0);
    CHECK(stats.radio_lists == 0);
    column_probe::check({row(5, 0, RowKind::Block, 12.0f, 300.0f, false, true),
                         row(5, 0, RowKind::Block, 12.0f, 300.0f, false, true)},
                        0.01f, &stats);
    CHECK(stats.radio_lists == 1);
    CHECK(stats.comparisons >= 1);
}

TEST_CASE("column check: the Effects page's two child columns are checked apart") {
    // The Effects page draws two child windows, not a table, so every row reports the same table
    // column. Its Combo row ends at the left column's edge, its effect rows at the right one's: apart
    // that is two columns each at one right edge, merged it is one column that does not end at one.
    const float left_end = 1060.0f;
    const float right_end = 1621.0f;
    const auto row = [&](int page_column, float end) {
        Row r;
        r.scope = 2;
        r.column = column_probe::column_of(-1, page_column);
        r.kind = RowKind::Compact;
        r.span = {end - 56.0f, end};
        return r;
    };
    const std::vector<Row> apart{row(0, left_end), row(1, right_end), row(1, right_end),
                                 row(1, right_end)};
    CHECK(column_probe::check(apart).empty());
    // The same rows with the offset dropped (every row in the one column) are a finding.
    const std::vector<Row> merged{row(0, left_end), row(0, right_end), row(0, right_end),
                                  row(0, right_end)};
    CHECK_FALSE(column_probe::check(merged).empty());
    CHECK(column_probe::column_of(-1, 0) != column_probe::column_of(-1, 1));
    // Page columns never collide with a table's own columns.
    CHECK(column_probe::column_of(3, 0) != column_probe::column_of(0, 1));
}

TEST_CASE("row columns: a column with a reset button keeps a gutter for it at its trailing end") {
    for (const float u : kScales) {
        for (const float width : kWidths) {
            CAPTURE(u);
            CAPTURE(width);
            const ColumnPlan plain = plan_column(100.0f, 100.0f + width * u, 116.0f * u, u);
            const ColumnPlan gutter = plan_column(100.0f, 100.0f + width * u, 116.0f * u, u, true);
            // Every control ends the gutter sooner, still at one x for all kinds.
            CHECK(gutter.control_right == doctest::Approx(plain.control_right - kResetReserve * u));
            CHECK(compact_span(gutter, 40.0f).x1 == gutter.control_right);
            CHECK(block_span(gutter, u).x1 == gutter.control_right);
            CHECK(wide_span(gutter, false, u).x1 == gutter.control_right);
            CHECK(inline_span(gutter, u).x1 == gutter.control_right);
            // The button's hit area is a finger wide, a dead gap clear of the control, and ends
            // at the column's edge, where the plain column's controls end a row inset short of it.
            const Span hit = reset_span(gutter, u);
            CHECK(hit.w() == doctest::Approx(tok::kHit * u));
            CHECK(hit.x0 - gutter.control_right ==
                  doctest::Approx(tok::kHitGap * u));
            CHECK(hit.x1 == doctest::Approx(gutter.right));
        }
    }
}

TEST_CASE("row columns: a row with a reset button makes its column keep the gutter from the next frame") {
    LabelZones z;
    CHECK_FALSE(z.reset(5, 1));
    z.report_reset(5, 1);
    CHECK_FALSE(z.reset(5, 1)); // still frame 1: every row of it reads the same answer
    CHECK(z.reset(5, 2));
    CHECK_FALSE(z.reset(6, 2)); // another column has none
    CHECK_FALSE(z.reset(5, 3)); // no row reported in frame 2: the gutter goes
}

TEST_CASE("row columns: a slider track under its label keeps 2 mm clear of the reset button") {
    for (const float u : kScales) {
        for (const float width : kWidths) {
            CAPTURE(u);
            CAPTURE(width);
            const ColumnPlan gutter = plan_column(100.0f, 100.0f + width * u, 116.0f * u, u, true);
            const Span hit = reset_span(gutter, u);
            // A stacked track stops a knob's reach short of the row's end, so its knob and its touch
            // area end by `control_right`, 2 mm before the button's hit area.
            const float end = slider_track_end(gutter, block_span(gutter, u).x1, true, u);
            const float stacked_hit = track_hit_right(gutter, end, u);
            CHECK(end + kKnobReach * u <= gutter.control_right + 1e-3f); // a knob at the end fits
            CHECK(stacked_hit <= gutter.control_right + 1e-3f);
            CHECK(hit.x0 - stacked_hit >= tok::kHitGap * u - 1e-3f);
            // Where the column has no gutter, the track runs to the end as before.
            CHECK(slider_track_end(plan_column(100.0f, 100.0f + width * u, 116.0f * u, u),
                                   block_span(gutter, u).x1, true, u) == block_span(gutter, u).x1);
            // A track beside its label ends a value's width short: the knob's reach, as before.
            const float value_w = 52.0f * u;
            const float beside_end = gutter.control_right - value_w;
            CHECK(track_hit_right(gutter, beside_end, u) == doctest::Approx(beside_end + kKnobReach * u));
            CHECK(hit.x0 - track_hit_right(gutter, beside_end, u) >= tok::kHitGap * u);
            // A column with no reset keeps the knob's reach past the end.
            const ColumnPlan plain = plan_column(100.0f, 100.0f + width * u, 116.0f * u, u);
            CHECK(track_hit_right(plain, plain.control_right, u) ==
                  doctest::Approx(plain.control_right + kKnobReach * u));
        }
    }
}
