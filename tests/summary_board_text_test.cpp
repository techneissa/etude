// SPDX-License-Identifier: GPL-3.0-or-later
// The Board tab's words that come from numbers: thousands, the day of a best run, the footnote.
#include "ui/summary_board_text.hpp"

#include "etude/run_score.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app::ui;

namespace {

std::string points(int p) {
    FitText t;
    board_points_words(p, &t);
    return std::string(t.view());
}

std::string day(std::int32_t d, std::int32_t today) {
    FitText t;
    board_day_words(d, today, &t);
    return std::string(t.view());
}

std::string footnote(int speed) {
    FitText t;
    board_footnote_words(speed, &t);
    return std::string(t.view());
}

} // namespace

TEST_CASE("board words: points get thousands separators") {
    CHECK(points(0) == "0");
    CHECK(points(412) == "412");
    CHECK(points(933) == "933");
    CHECK(points(1012) == "1,012");
    CHECK(points(12345) == "12,345");
    CHECK(points(1000000) == "1,000,000");
    CHECK(points(-1500) == "-1,500");
}

TEST_CASE("board words: a day is Today, Yesterday, or its date") {
    const std::int32_t today = day_from_civil({2026, 10, 19});
    CHECK(day(today, today) == "Today");
    CHECK(day(today - 1, today) == "Yesterday");
    CHECK(day(day_from_civil({2026, 10, 17}), today) == "Oct 17");
    CHECK(day(day_from_civil({2026, 1, 2}), today) == "Jan 2");
    CHECK(day(day_from_civil({2025, 12, 31}), today) == "Dec 31, 2025");
}

TEST_CASE("board words: the footnote says what the slowest speed counts for, from core's factor") {
    CHECK(footnote(50) == "Slower earns less, never nothing: 50% speed counts for 0.625 of the points.");
    CHECK(footnote(80) == "Slower earns less, never nothing: 80% speed counts for 0.85 of the points.");
    CHECK(footnote(25) == "Slower earns less, never nothing: 25% speed counts for 0.438 of the points.");
    CHECK(speed_factor(50) == doctest::Approx(0.625));
    // Without a slower run, or with no run at all, the rule alone.
    CHECK(footnote(100) == "Slower earns less, never nothing.");
    CHECK(footnote(150) == "Slower earns less, never nothing.");
    CHECK(footnote(0) == "Slower earns less, never nothing.");
}

namespace {

RunsChart chart_of(const std::vector<int>& scores, int current) {
    std::vector<ChartRun> runs;
    std::int32_t d = 90;
    for (const int s : scores) {
        runs.push_back({s, d++, false});
    }
    return build_runs_chart(runs, current);
}

std::string chart_line(const RunsChart& chart, bool brief = false) {
    FitText t;
    board_chart_line_words(chart, &t, brief);
    return std::string(t.view());
}

} // namespace

TEST_CASE("board text: an ordinal") {
    FitText t;
    const std::pair<int, const char*> cases[] = {{1, "1st"},  {2, "2nd"},   {3, "3rd"},   {4, "4th"},
                                                 {5, "5th"},  {11, "11th"}, {12, "12th"}, {13, "13th"},
                                                 {21, "21st"}, {22, "22nd"}, {101, "101st"}, {111, "111th"}};
    for (const auto& [n, words] : cases) {
        board_ordinal_words(n, &t);
        CHECK(t.view() == words);
    }
}

TEST_CASE("board text: the line under the chart says the place, the distance or that the run is short") {
    // 933 is the best of seven runs; 885 is second best of the five; 790 is under the fifth.
    CHECK(chart_line(chart_of({640, 702, 655, 780, 811, 885, 933}, 6)) == "933 \xC2\xB7 #1 of your best");
    CHECK(chart_line(chart_of({640, 702, 655, 780, 811, 933, 885}, 6)) == "885 \xC2\xB7 #2 of your best");
    CHECK(chart_line(chart_of({1012, 900, 880, 860, 840, 790}, 5)) == "790 \xC2\xB7 50 under your 5th best");
    CHECK(chart_line(chart_of({1012, 900, 880, 860, 840, 790}, 5)) != chart_line(chart_of({1012, 900}, 1)));
    CHECK(chart_line(chart_of({820, 900, 880, 860, 840, 820}, 0)) == "820 \xC2\xB7 level with your 5th best");
    // The distance has its separator like the points do.
    CHECK(chart_line(chart_of({2100, 2000, 1900, 1800, 1700, 650}, 5)) ==
          "650 \xC2\xB7 1,050 under your 5th best");
    // The brief forms, for a narrow column.
    CHECK(chart_line(chart_of({640, 702, 655, 780, 811, 933, 885}, 6), true) == "885 \xC2\xB7 #2");
    CHECK(chart_line(chart_of({2100, 2000, 1900, 1800, 1700, 650}, 5), true) ==
          "650 \xC2\xB7 1,050 under 5th");
    CHECK(chart_line(chart_of({820, 900, 880, 860, 840, 820}, 0), true) == "820 \xC2\xB7 level with 5th");
    CHECK(chart_line(chart_of({700, 750}, -1), true) == "Too short to score");
    CHECK(chart_line(chart_of({700, 750}, -1)) == "Too short to score");
    CHECK(chart_line(chart_of({}, -1)) == "Too short to score");
    CHECK(chart_line(chart_of({1012, 900}, 0)) == "1,012 \xC2\xB7 #1 of your best");
}

TEST_CASE("board text: the chart's words are made once, for every run, with Yesterday cut for narrow charts") {
    const RunsChart chart = chart_of({640, 702, 1012}, 2);
    ChartWords words;
    words.build(chart, 92); // days 90, 91, 92 against today 92
    CHECK(words.day[0].view() == "Apr 1"); // epoch day 90
    CHECK(words.day[1].view() == "Yesterday");
    CHECK(words.day_brief[1].view() == "Yest.");
    CHECK(words.day[2].view() == "Today");
    CHECK(words.day_brief[2].view() == "Today");
    CHECK(words.points[2].view() == "1,012");
    CHECK(words.points[0].view() == "640");
    CHECK(words.line.view() == "1,012 \xC2\xB7 #1 of your best");
    // Places by points, equal points sharing one: 1012, 702, 640 and 702 again.
    const RunsChart tie = chart_of({702, 640, 702, 1012}, 3);
    ChartWords tied;
    tied.build(tie, 92);
    REQUIRE(tie.runs.size() == 4);
    CHECK(tied.place[0].view() == "2");
    CHECK(tied.place[1].view() == "4");
    CHECK(tied.place[2].view() == "2");
    CHECK(tied.place[3].view() == "1");
}
