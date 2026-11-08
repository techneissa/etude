// SPDX-License-Identifier: GPL-3.0-or-later
// The words of the summary page's Board tab that are worked out from numbers: pure, so
// tests/summary_board_text_test.cpp checks them without a font.
#pragma once

#include "fit_text.hpp"

#include "etude/leaderboard.hpp"
#include "etude/run_record.hpp"
#include "etude/run_score.hpp"
#include "etude/runs_chart.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>

namespace etude::app::ui {

// 1012 as "1,012", a negative number with its sign.
inline void board_points_words(int points, FitText* out) {
    char digits[16];
    std::snprintf(digits, sizeof(digits), "%d", points < 0 ? -points : points);
    const std::size_t n = std::strlen(digits);
    char text[24];
    std::size_t at = 0;
    if (points < 0) {
        text[at++] = '-';
    }
    for (std::size_t i = 0; i < n; ++i) {
        if (i > 0 && (n - i) % 3 == 0) {
            text[at++] = ',';
        }
        text[at++] = digits[i];
    }
    text[at] = '\0';
    out->assign(text);
}

// "Today", "Yesterday", "Oct 17"; a day of another year has it: "Oct 17, 2025". `brief` shortens
// Yesterday to "Yest." for where the long word does not fit.
inline void board_day_words(std::int32_t day, std::int32_t today, FitText* out, bool brief = false) {
    if (day == today) {
        out->assign("Today");
        return;
    }
    if (day == today - 1) {
        out->assign(brief ? "Yest." : "Yesterday");
        return;
    }
    static constexpr const char* kMonths[12] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                                "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
    const CivilDate date = civil_from_day(day);
    const CivilDate now = civil_from_day(today);
    const int month = date.month >= 1 && date.month <= 12 ? date.month : 1;
    if (date.year == now.year) {
        out->format("%s %d", kMonths[month - 1], date.day);
    } else {
        out->format("%s %d, %d", kMonths[month - 1], date.day, date.year);
    }
}

// A speed factor as the shortest decimal of up to three places: 0.625, 0.8, 1.
inline void board_factor_words(double factor, FitText* out) {
    char text[16];
    std::snprintf(text, sizeof(text), "%.3f", factor);
    std::size_t n = std::strlen(text);
    while (n > 1 && text[n - 1] == '0') {
        text[--n] = '\0';
    }
    if (n > 1 && text[n - 1] == '.') {
        text[--n] = '\0';
    }
    out->assign(text);
}

// The footnote under the table: with a slower run in it, "Slower earns less, never nothing: 50%
// speed counts for 0.625 of the points." (the factor is core's speed_factor); without one, the rule
// alone. `lowest_speed_percent` 0 is no run at all.
inline void board_footnote_words(int lowest_speed_percent, FitText* out) {
    if (lowest_speed_percent <= 0 || lowest_speed_percent >= 100) {
        out->assign("Slower earns less, never nothing.");
        return;
    }
    FitText factor;
    board_factor_words(speed_factor(lowest_speed_percent), &factor);
    out->format("Slower earns less, never nothing: %d%% speed counts for %s of the points.",
                lowest_speed_percent, factor.c_str());
}

// 1 as "1st", 2 as "2nd", 5 as "5th", 11 as "11th".
inline void board_ordinal_words(int n, FitText* out) {
    const int tail = n % 100;
    const char* suffix = "th";
    if (tail < 11 || tail > 13) {
        suffix = n % 10 == 1 ? "st" : n % 10 == 2 ? "nd" : n % 10 == 3 ? "rd" : "th";
    }
    out->format("%d%s", n, suffix);
}

// The line under the chart about the page's run: its points and, if it made the best runs, its
// place ("933 · #2 of your best"); otherwise how far it is under the last of them ("790 · 30
// under your 5th best"); a run with no score says so.
// `brief` is for a column the long line does not fit: "933 \xC2\xB7 #2", "790 \xC2\xB7 30 under 5th".
inline void board_chart_line_words(const RunsChart& chart, FitText* out, bool brief = false) {
    if (chart.place == RunPlace::TooShort) {
        out->assign("Too short to score");
        return;
    }
    FitText points;
    board_points_words(chart.run_score, &points);
    if (chart.place == RunPlace::Best) {
        out->format(brief ? "%s \xC2\xB7 #%d" : "%s \xC2\xB7 #%d of your best", points.c_str(), chart.rank);
        return;
    }
    FitText last;
    board_ordinal_words(kChartRuns, &last);
    FitText behind;
    board_points_words(chart.behind, &behind);
    if (chart.behind <= 0) {
        out->format(brief ? "%s \xC2\xB7 level with %s" : "%s \xC2\xB7 level with your %s best",
                    points.c_str(), last.c_str());
    } else {
        out->format(brief ? "%s \xC2\xB7 %s under %s" : "%s \xC2\xB7 %s under your %s best",
                    points.c_str(), behind.c_str(), last.c_str());
    }
}

// The words of the chart's rows, worked out when the chart changes and drawn from after that, so
// no frame formats a number.
struct ChartWords {
    std::array<FitText, kChartRuns> day;       // "Today", "Yesterday", "Oct 16"
    std::array<FitText, kChartRuns> day_brief; // "Yest." for Yesterday
    std::array<FitText, kChartRuns> points;    // "1,012"
    std::array<FitText, kChartRuns> place;     // "1", "2": the run's place by points, ties alike
    FitText line;                              // what board_chart_line_words says
    FitText line_brief;                        // and its brief form, for a narrow column

    void build(const RunsChart& chart, std::int32_t today) {
        for (std::size_t i = 0; i < chart.runs.size() && i < day.size(); ++i) {
            board_day_words(chart.runs[i].day, today, &day[i]);
            board_day_words(chart.runs[i].day, today, &day_brief[i], true);
            board_points_words(chart.runs[i].score, &points[i]);
            int above = 0;
            for (const ChartRun& other : chart.runs) {
                above += other.score > chart.runs[i].score ? 1 : 0;
            }
            place[i].format("%d", above + 1);
        }
        board_chart_line_words(chart, &line);
        board_chart_line_words(chart, &line_brief, true);
    }
};

} // namespace etude::app::ui
