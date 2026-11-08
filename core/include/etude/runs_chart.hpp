// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <vector>

namespace etude {

// The Board tab's "Your runs on this song": a player's best runs of one song and where the run the
// page is about stands among them. Pure, built once when the board is built, never per frame.

// Runs the chart holds: the player's best on the song. A run that is no longer among them is not
// forgotten (the history keeps every run), it just is not drawn; a better one pushes it out.
inline constexpr int kChartRuns = 5;

// One drawn run.
struct ChartRun {
    int score = 0;
    std::int32_t day = 0;   // the local day it was played
    bool current = false;   // it is the run the page is about
    friend bool operator==(const ChartRun&, const ChartRun&) = default;
};

// Where the page's run stands: no score (too few notes graded), among the best runs, or below them.
enum class RunPlace : std::uint8_t { TooShort, Best, Below };

struct RunsChart {
    // The best runs, oldest first (the order the bars and the line draw), at most kChartRuns.
    std::vector<ChartRun> runs;
    // The same runs best first, as indices into `runs`: the ranked list. Equal points put the
    // newer run first.
    std::vector<int> ranked;
    // The index in `runs` of the best run (the newer of equals), -1 with none.
    int star = -1;
    RunPlace place = RunPlace::TooShort;
    int run_score = 0;  // the page's run's points (Best, Below)
    int rank = 0;       // Best: 1 plus the runs here that have more points; equal points share a place
    int behind = 0;     // Below: the points the run is under the last of the best runs
    friend bool operator==(const RunsChart&, const RunsChart&) = default;
};

// How the chart draws the runs, and the order the view button cycles them in: bars (newest at the
// top), a line over the runs, the ranked list (best first).
enum class RunsChartView : std::uint8_t { Bars, Line, Ranked };

constexpr RunsChartView next_runs_chart_view(RunsChartView view) {
    switch (view) {
    case RunsChartView::Bars:
        return RunsChartView::Line;
    case RunsChartView::Line:
        return RunsChartView::Ranked;
    case RunsChartView::Ranked:
        break;
    }
    return RunsChartView::Bars;
}

// The chart of `counting`, a player's counting runs of the song in the order they were played;
// `current` indexes the one the page is about, -1 when it has no score. The best kChartRuns by
// points win, the newer run of equal points first, so a run that ties the last place takes it.
RunsChart build_runs_chart(const std::vector<ChartRun>& counting, int current);

} // namespace etude
