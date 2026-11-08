// SPDX-License-Identifier: GPL-3.0-or-later
// Where the parts of the Board tab's "Your runs on this song" chart go inside the rectangle the page
// layout gave it: the room for the runs, and under it the one line about this run. Pure geometry
// like summary_board_layout.hpp, so a test can check it at every size without ImGui.
#pragma once

#include "summary_board_layout.hpp"

#include <algorithm>

namespace etude::app::ui {

struct ChartParts {
    RectF plot;       // the runs' room
    RectF line;       // the line about this run, at the foot
    float pitch = 0;  // one row of the bars or the ranked list: the plot split in kChartRowsMax
    RectF row(int i) const { return {plot.x, plot.y + static_cast<float>(i) * pitch, plot.w, pitch}; }
};

// `area` is `SummaryBoard::chart` and `sc` the card's scale.
inline ChartParts chart_parts(const RectF& area, float sc) {
    ChartParts c;
    const float gap = tok::kS2 * sc;
    const float line_h = board_tok::kChartLineH * sc;
    c.line = {area.x, area.bottom() - line_h, area.w, line_h};
    c.plot = {area.x, area.y, area.w, std::max(0.0f, c.line.y - gap - area.y)};
    c.pitch = c.plot.h / static_cast<float>(kChartRowsMax);
    return c;
}

// Whether a label `size` tall fits under a line chart's dot at `dot_y` (`reach` below its centre
// before the label starts) and above the hairline over the days, `days_top`: a dot near the foot has
// no room, and its points go over it.
inline bool chart_points_fit_below(float dot_y, float reach, float size, float days_top) {
    return dot_y + reach + size <= days_top;
}

} // namespace etude::app::ui
