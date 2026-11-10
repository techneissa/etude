// SPDX-License-Identifier: GPL-3.0-or-later
// The ImGui side of row_columns.hpp: each settings row asks here for its column's plan, reports how
// far its label reaches, and tells the probe where its control went.
//
// The probe is for `etude --check-settings`: with it on, every row records its kind and span, and
// the check fails when two rows of one column of one page disagree where their controls start.
#pragma once

#include "column_check.hpp"
#include "row_columns.hpp"

#include <cstdint>
#include <string>
#include <vector>

namespace etude::app::ui {

// The page the rows drawn from now on belong to (one id per page; 0 outside the sheet). The sheet
// sets it around the page it draws, so two pages never share a column's measured label zone.
void row_scope(int scope);

// What a row sees of its column: its plan and the slot its reports go to.
struct RowFrame {
    ColumnPlan plan;
    std::uint32_t slot = 0;
    bool measured = false; // some row of this column has reported its label before
};

// The plan for a row whose column starts at `left` and is `width` wide (ImGui's cursor and
// available width, read before the row draws); `u` is the kit's unit.
RowFrame row_frame(float left, float width, float u);
// A Wide row reports where its label ends (icon, text, info button), from `left`.
void row_report(const RowFrame& f, float label_end_from_left);
// A row with a reset button says so: from the next frame its column keeps the gutter for it.
void row_report_reset(const RowFrame& f);

// A page's columns are measured by its first frame, so that frame is never seen: drawn with
// `RowMeasure` around the page, it is invisible and takes no touch, and the next frame (the first
// anyone sees) already has every column's label zone. Scope 0 is never measured this way.
class RowMeasure {
  public:
    explicit RowMeasure(int scope);
    ~RowMeasure();
    RowMeasure(const RowMeasure&) = delete;
    RowMeasure& operator=(const RowMeasure&) = delete;
    bool hidden() const { return hidden_; }

  private:
    int scope_;
    bool hidden_;
};

// A page that lays its columns out as child windows (the Effects page) tells the column check which
// one it is drawing: 0 for the first, 1 for the second (`column_probe::column_of`).
void row_column_offset(int offset);

namespace column_probe {
bool on();
void start();
std::vector<Row> stop();
void note(const RowFrame& f, RowKind kind, Span span, bool stacked = false, bool radio = false);
} // namespace column_probe

} // namespace etude::app::ui
