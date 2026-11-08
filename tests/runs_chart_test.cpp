// SPDX-License-Identifier: GPL-3.0-or-later
// The song-runs chart: the best five runs of a song, where the page's run stands among them, how far
// it is from the fifth best, a run too short to score, and the view the button cycles to
// (runs_chart.hpp).
#include "etude/runs_chart.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

// Runs of consecutive days with these scores, oldest first.
std::vector<ChartRun> runs_of(const std::vector<int>& scores) {
    std::vector<ChartRun> out;
    std::int32_t day = 100;
    for (const int score : scores) {
        out.push_back({score, day++, false});
    }
    return out;
}

std::vector<int> scores_of(const RunsChart& chart) {
    std::vector<int> out;
    for (const ChartRun& run : chart.runs) {
        out.push_back(run.score);
    }
    return out;
}

std::vector<int> ranked_scores(const RunsChart& chart) {
    std::vector<int> out;
    for (const int i : chart.ranked) {
        out.push_back(chart.runs[static_cast<std::size_t>(i)].score);
    }
    return out;
}

} // namespace

TEST_CASE("runs chart: the best five, drawn in the order they were played") {
    const RunsChart chart = build_runs_chart(runs_of({640, 702, 655, 780, 811, 885, 933}), 6);
    CHECK(scores_of(chart) == std::vector<int>{702, 780, 811, 885, 933});
    CHECK(ranked_scores(chart) == std::vector<int>{933, 885, 811, 780, 702});
    CHECK(chart.star == 4);
}

TEST_CASE("runs chart: a best score older than every other run stays in the chart") {
    // Twenty ordinary runs after one great one: the great one is still among the best five.
    std::vector<int> scores{990};
    for (int i = 0; i < 20; ++i) {
        scores.push_back(400 + i);
    }
    const RunsChart chart = build_runs_chart(runs_of(scores), 20);
    REQUIRE(chart.runs.size() == 5);
    CHECK(chart.runs.front().score == 990);
    CHECK(chart.star == 0);
}

TEST_CASE("runs chart: fewer than five runs are all shown") {
    const RunsChart chart = build_runs_chart(runs_of({500, 450}), 1);
    CHECK(scores_of(chart) == std::vector<int>{500, 450});
    CHECK(chart.place == RunPlace::Best);
    CHECK(chart.rank == 2);
    CHECK(build_runs_chart({}, -1).runs.empty());
    CHECK(build_runs_chart({}, -1).star == -1);
}

TEST_CASE("runs chart: equal points, the newer run wins the last place and is listed first") {
    // Six runs, the older two tie for the fifth place: the newer one is kept.
    const RunsChart chart = build_runs_chart(runs_of({800, 800, 900, 910, 920, 930}), 5);
    CHECK(scores_of(chart) == std::vector<int>{800, 900, 910, 920, 930});
    CHECK(chart.runs.front().day == 101); // the second 800, not the first
    const RunsChart ties = build_runs_chart(runs_of({700, 700, 700}), 2);
    CHECK(ranked_scores(ties) == std::vector<int>{700, 700, 700});
    CHECK(ties.ranked == std::vector<int>{2, 1, 0}); // the newest first
    CHECK(ties.star == 2);
}

TEST_CASE("runs chart: this run's place among the best, equal points share one") {
    const std::vector<ChartRun> history = runs_of({640, 702, 655, 780, 811, 885, 933});
    RunsChart chart = build_runs_chart(history, 6);
    CHECK(chart.place == RunPlace::Best);
    CHECK(chart.run_score == 933);
    CHECK(chart.rank == 1);
    CHECK(chart.runs.back().current);
    // The card is about an older run: 811 is third of the five.
    chart = build_runs_chart(history, 4);
    CHECK(chart.place == RunPlace::Best);
    CHECK(chart.rank == 3);
    CHECK(chart.runs[2].current);
    // A tie shares the place of the run it ties.
    chart = build_runs_chart(runs_of({900, 900, 500}), 1);
    CHECK(chart.rank == 1);
    chart = build_runs_chart(runs_of({900, 900, 500}), 2);
    CHECK(chart.rank == 3);
}

TEST_CASE("runs chart: a run below the best five says how far it is from the fifth") {
    const RunsChart chart = build_runs_chart(runs_of({900, 880, 860, 840, 820, 790}), 5);
    CHECK(chart.place == RunPlace::Below);
    CHECK(chart.run_score == 790);
    CHECK(chart.behind == 30); // 820 is the fifth best
    CHECK(scores_of(chart) == std::vector<int>{900, 880, 860, 840, 820});
    for (const ChartRun& run : chart.runs) {
        CHECK_FALSE(run.current); // the run has no bar of its own
    }
    // Under the fifth by a point, and level with it (an older run that ties the last place).
    CHECK(build_runs_chart(runs_of({900, 880, 860, 840, 820, 819}), 5).behind == 1);
    const RunsChart level = build_runs_chart(runs_of({820, 900, 880, 860, 840, 820}), 0);
    CHECK(level.place == RunPlace::Below);
    CHECK(level.behind == 0);
}

TEST_CASE("runs chart: a run too short to score has no place and no bar") {
    const RunsChart chart = build_runs_chart(runs_of({700, 750}), -1);
    CHECK(chart.place == RunPlace::TooShort);
    CHECK(chart.run_score == 0);
    CHECK(scores_of(chart) == std::vector<int>{700, 750});
    for (const ChartRun& run : chart.runs) {
        CHECK_FALSE(run.current);
    }
    CHECK(chart.star == 1);
}

TEST_CASE("runs chart: the view button cycles bars, line, ranked and round again") {
    CHECK(next_runs_chart_view(RunsChartView::Bars) == RunsChartView::Line);
    CHECK(next_runs_chart_view(RunsChartView::Line) == RunsChartView::Ranked);
    CHECK(next_runs_chart_view(RunsChartView::Ranked) == RunsChartView::Bars);
}
