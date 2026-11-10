// SPDX-License-Identifier: GPL-3.0-or-later
// The rule the settings pages' columns keep, as a check over the rows a page drew: in one column of
// one page, every row of one kind has its control at one x (compact controls, by their right edge).
// No ImGui in it; `etude --check-settings` feeds it
// what the pages really drew (row_layout.hpp).
#pragma once

#include "row_columns.hpp"

#include <string>
#include <vector>

namespace etude::app::ui::column_probe {

struct Row {
    int scope = 0;        // the page
    int column = 0;       // the table column on it
    RowKind kind = RowKind::Wide;
    bool stacked = false; // a control under its label
    bool radio = false;   // a Block row that is one choice of a list
    bool visible = true;  // false: drawn in a page's measuring frame, which nobody sees
    Span span;
    bool zone_measured = false; // the zone came from the column's labels, not the default share
    float zone = 0.0f;    // the label side's width in this row's column (control_x - left)
};

const char* kind_name(RowKind kind);

// The column a row is filed under: the table column it is in, and, for a page that lays its
// columns out as child windows rather than a table (the Effects page), which child it is in
// (`page_column`, 0 for the first). Rows of two children never share a column, whatever their
// table column.
inline constexpr int kPageColumnStride = 100;
inline int column_of(int table_column, int page_column) {
    return table_column + page_column * kPageColumnStride;
}

// What a check looked at, so a run that looked at nothing can fail.
struct Stats {
    int comparisons = 0; // row against the first row of its group, or against its column's edges
    int radio_lists = 0; // pages with a list of choices
};

// Problems found among the visible `rows` (one string each): two rows of one column and kind that
// disagree where their controls are (beyond `tolerance` points); in one column, rows that do not
// end at one right edge, or full-width rows (a control under its label, a row of choices with no
// label) that do not start at one x; a list of choices that continues in a second column.
std::vector<std::string> check(const std::vector<Row>& rows, float tolerance = 0.01f,
                               Stats* stats = nullptr);

} // namespace etude::app::ui::column_probe
