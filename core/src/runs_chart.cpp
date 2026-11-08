// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/runs_chart.hpp"

#include <algorithm>
#include <cstddef>
#include <numeric>

namespace etude {

RunsChart build_runs_chart(const std::vector<ChartRun>& counting, int current) {
    RunsChart chart;
    const int n = static_cast<int>(counting.size());
    std::vector<int> order(counting.size());
    std::iota(order.begin(), order.end(), 0);
    // Points first, then the later run.
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const std::size_t ia = static_cast<std::size_t>(a);
        const std::size_t ib = static_cast<std::size_t>(b);
        return counting[ia].score != counting[ib].score ? counting[ia].score > counting[ib].score : a > b;
    });
    order.resize(static_cast<std::size_t>(std::min(n, kChartRuns)));

    std::vector<int> played = order; // the same runs, in the order they were played
    std::sort(played.begin(), played.end());
    for (const int at : played) {
        ChartRun run = counting[static_cast<std::size_t>(at)];
        run.current = at == current;
        chart.runs.push_back(run);
    }
    for (const int at : order) {
        chart.ranked.push_back(static_cast<int>(
            std::lower_bound(played.begin(), played.end(), at) - played.begin()));
    }
    chart.star = chart.ranked.empty() ? -1 : chart.ranked.front();

    if (current < 0 || current >= n) {
        return chart;
    }
    const int score = counting[static_cast<std::size_t>(current)].score;
    chart.run_score = score;
    if (std::find(played.begin(), played.end(), current) != played.end()) {
        chart.place = RunPlace::Best;
        chart.rank = 1;
        for (const ChartRun& run : chart.runs) {
            chart.rank += run.score > score ? 1 : 0;
        }
    } else {
        chart.place = RunPlace::Below;
        chart.behind = chart.runs[static_cast<std::size_t>(chart.ranked.back())].score - score;
    }
    return chart;
}

} // namespace etude
