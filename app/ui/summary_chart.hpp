// SPDX-License-Identifier: GPL-3.0-or-later
// The Board tab's "Your runs on this song": the player's best runs of the song as bars (newest at
// the top), a line over the runs, or a ranked list, then one line about the run the page is about.
// What it shows is core's `RunsChart` (runs_chart.hpp), built with the board; where it goes is
// summary_chart_layout.hpp. The words of the rows are worked out when the chart changes, so a frame
// only draws.
#pragma once

#include "kit.hpp"
#include "summary_board_text.hpp"
#include "summary_chart_layout.hpp"

#include "etude/runs_chart.hpp"

namespace etude::app::ui {

// A five-pointed star `d` across, centred on `c`: the mark of the best run.
void board_star(ImDrawList* dl, ImVec2 c, float d, ImVec4 ink);

class SummaryChart {
  public:
    // Draws the chart of `chart` as `view` in `area` (`SummaryBoard::chart`), `today` the local day
    // for "Today" and "Oct 16".
    void draw(Kit& k, ImDrawList* dl, const RunsChart& chart, std::int32_t today,
              RunsChartView view, const RectF& area);

    // The head's words for a run that did not count, cut to `width` (the full sentence, a shorter
    // one, then a cut), measured when the size or the width changes and not every frame.
    const FitText& too_short_head(Kit& k, float size, float width);

  private:
    void rebuild(const RunsChart& chart, std::int32_t today);
    void measure(Kit& k, bool brief);
    void draw_bars(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts);
    void draw_ranked(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts);
    void draw_line(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts);
    void draw_foot(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts);

    RunsChart shown_;           // the chart the words below are of
    std::int32_t today_ = 0;
    bool built_ = false;
    ChartWords words_;
    // The widest day and points words at the size drawn, measured when the chart or the scale changes.
    float measured_u_ = 0.0f;
    bool measured_brief_ = false;
    float day_w_ = 0.0f;
    float points_w_ = 0.0f;
    // Whether the foot is its brief form, for the width and scale it was measured at.
    float foot_u_ = 0.0f;
    float foot_width_ = 0.0f;
    bool foot_brief_ = false;
    FitText short_head_;
    float short_u_ = 0.0f;
    float short_width_ = -1.0f;
};

} // namespace etude::app::ui
