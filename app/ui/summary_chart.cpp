// SPDX-License-Identifier: GPL-3.0-or-later
#include "summary_chart.hpp"

#include "card_kit.hpp"
#include "fit_text.hpp"
#include "icon_audit.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

namespace {

namespace chart_tok {
// Design points (times the card's scale).
inline constexpr float kBarThick = 22.0f;   // a bar, at the most
inline constexpr float kTrackThick = 6.0f;  // a ranked row's mini bar
inline constexpr float kRankW = 18.0f;      // a ranked row's number or star
inline constexpr float kBriefBelow = 240.0f; // under this width "Yesterday" is "Yest."
inline constexpr float kDayShare = 0.38f;   // the days' column, of the chart's width, at the most
inline constexpr float kDot = 3.5f;         // a line's dot, its radius
inline constexpr float kDotNow = 5.0f;      // the page's run's dot
inline constexpr float kLineThick = 2.0f;
} // namespace chart_tok

ImVec4 gold(const Kit& k) { return grade_ink(k, Grade::Late).top; }
ImVec4 green(const Kit& k) { return grade_ink(k, Grade::Perfect).top; }
// The bars of the runs before the page's: the accent, dimmed.
ImVec4 dim_accent() { return mix(col::kAccent, col::kSurface, 0.55f); }

int peak_of(const RunsChart& chart) {
    int peak = 1;
    for (const ChartRun& run : chart.runs) {
        peak = std::max(peak, run.score);
    }
    return peak;
}

// What a run's words are drawn in: the page's run bold and bright, the best in gold.
ImVec4 value_ink(const Kit& k, const RunsChart& chart, std::size_t i) {
    if (static_cast<int>(i) == chart.star) {
        return gold(k);
    }
    return chart.runs[i].current ? col::kText : col::kText2;
}

} // namespace

void board_star(ImDrawList* dl, ImVec2 c, float d, ImVec4 ink) {
    constexpr int kPoints = 5;
    constexpr float kInner = 0.42f; // the inner points' radius, over the outer
    constexpr float kPi = 3.14159265f;
    for (int i = 0; i < kPoints * 2; ++i) {
        const float r = d * 0.5f * (i % 2 == 0 ? 1.0f : kInner);
        const float a = -kPi * 0.5f + static_cast<float>(i) * kPi / static_cast<float>(kPoints);
        dl->PathLineTo({c.x + r * std::cos(a), c.y + r * std::sin(a)});
    }
    dl->PathFillConcave(u32(ink));
}

const FitText& SummaryChart::too_short_head(Kit& k, float size, float width) {
    if (short_u_ == k.u && short_width_ == width) {
        return short_head_;
    }
    short_u_ = k.u;
    short_width_ = width;
    const char* words = "This run is too short to score";
    for (const char* shorter : {"Too short to score", "Too short"}) {
        if (text_size(k.bold, size, words).x > width) {
            words = shorter;
        }
    }
    short_head_ = fit(k.bold, size, words, width);
    return short_head_;
}

void SummaryChart::rebuild(const RunsChart& chart, std::int32_t today) {
    shown_ = chart;
    today_ = today;
    built_ = true;
    words_.build(chart, today);
    measured_u_ = 0.0f;
    foot_u_ = 0.0f;
}

void SummaryChart::measure(Kit& k, bool brief) {
    if (measured_u_ == k.u && measured_brief_ == brief) {
        return;
    }
    measured_u_ = k.u;
    measured_brief_ = brief;
    day_w_ = 0.0f;
    points_w_ = 0.0f;
    const float size = k.s(tok::kCaption);
    for (std::size_t i = 0; i < shown_.runs.size(); ++i) {
        const FitText& day = brief ? words_.day_brief[i] : words_.day[i];
        day_w_ = std::max(day_w_, text_size(k.bold, size, day.c_str()).x);
        points_w_ = std::max(points_w_, text_size(k.bold, size, words_.points[i].c_str()).x);
    }
}

// Newest at the top: the day, a bar as long as the run's points are of the best's, then the star
// (on the best) and the points at the bar's end.
void SummaryChart::draw_bars(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts) {
    const bool brief = parts.plot.w < chart_tok::kBriefBelow * k.u;
    measure(k, brief);
    const float size = k.s(tok::kCaption);
    const float gap = k.s(tok::kS2);
    const float star = k.s(tok::kIconSm);
    const float day_w = std::min(day_w_, parts.plot.w * chart_tok::kDayShare);
    const float bar_x = parts.plot.x + day_w + gap;
    const float room = std::max(0.0f, parts.plot.right() - points_w_ - star - 2.0f * k.s(tok::kS1) - bar_x);
    const float peak = static_cast<float>(peak_of(chart));
    const float thick = std::min(k.s(chart_tok::kBarThick), parts.pitch - k.s(tok::kS1));
    const int n = static_cast<int>(chart.runs.size());
    for (int row = 0; row < n; ++row) {
        const std::size_t i = static_cast<std::size_t>(n - 1 - row); // newest first
        const RectF r = parts.row(row);
        const float cy = r.y + r.h * 0.5f;
        const ChartRun& run = chart.runs[i];
        const FitText day = fit(run.current ? k.bold : k.regular, size,
                                brief ? words_.day_brief[i] : words_.day[i], day_w);
        label_at(k, dl, run.current ? k.bold : k.regular, size, {r.x, r.y, r.x + day_w, r.bottom()},
                 LabelAlign::Left, run.current ? col::kText : col::kText3, day.c_str());
        const float len = std::max(k.s(tok::kS1), room * static_cast<float>(run.score) / peak);
        dl->AddRectFilled({bar_x, cy - thick * 0.5f}, {bar_x + len, cy + thick * 0.5f},
                          u32(run.current ? col::kAccent : dim_accent()), k.s(tok::kRadiusInner));
        float x = bar_x + len + k.s(tok::kS1);
        if (static_cast<int>(i) == chart.star) {
            board_star(dl, {x + star * 0.5f, cy}, star, gold(k));
            x += star + k.s(tok::kS1);
        }
        label_at(k, dl, run.current || static_cast<int>(i) == chart.star ? k.bold : k.regular, size,
                 {x, r.y, x + points_w_, r.bottom()}, LabelAlign::Left, value_ink(k, chart, i),
                 words_.points[i].c_str());
    }
}

// Best first: the place (a star on the best), the day, a mini bar and the points at the right; the
// page's run in a lit row.
void SummaryChart::draw_ranked(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts) {
    const bool brief = parts.plot.w < chart_tok::kBriefBelow * k.u;
    measure(k, brief);
    const float size = k.s(tok::kCaption);
    const float gap = k.s(tok::kS2);
    const float pad = k.s(tok::kS2);
    const float peak = static_cast<float>(peak_of(chart));
    const float day_w = std::min(day_w_, parts.plot.w * chart_tok::kDayShare);
    const int n = static_cast<int>(chart.ranked.size());
    for (int row = 0; row < n; ++row) {
        const std::size_t i = static_cast<std::size_t>(chart.ranked[static_cast<std::size_t>(row)]);
        const RectF r = parts.row(row);
        const RectF face{r.x, r.y + k.s(tok::kS1) * 0.5f, r.w, r.h - k.s(tok::kS1)};
        const ChartRun& run = chart.runs[i];
        if (run.current) {
            dl->AddRectFilled({face.x, face.y}, {face.right(), face.bottom()}, u32(col::kSelected),
                              k.s(tok::kRadiusControl));
            dl->AddRect({face.x, face.y}, {face.right(), face.bottom()}, u32(col::kSelectedLine),
                        k.s(tok::kRadiusControl), 0, k.s(tok::kHairline));
        }
        const float cy = face.y + face.h * 0.5f;
        float x = face.x + pad;
        const float rank_w = k.s(chart_tok::kRankW);
        if (static_cast<int>(i) == chart.star) {
            const float star = k.s(tok::kIconSm);
            board_star(dl, {x + star * 0.5f, cy}, star, gold(k));
        } else {
            label_at(k, dl, k.bold, size, {x, face.y, x + rank_w, face.bottom()}, LabelAlign::Left,
                     col::kText2, words_.place[i].c_str());
        }
        x += rank_w;
        const FitText day = fit(run.current ? k.bold : k.regular, size,
                                brief ? words_.day_brief[i] : words_.day[i], day_w);
        label_at(k, dl, run.current ? k.bold : k.regular, size, {x, face.y, x + day_w, face.bottom()},
                 LabelAlign::Left, run.current ? col::kText : col::kText2, day.c_str());
        x += day_w + gap;
        const float right = face.right() - pad;
        label_at(k, dl, k.bold, size, {right - points_w_, face.y, right, face.bottom()}, LabelAlign::Right,
                 value_ink(k, chart, i), words_.points[i].c_str());
        const float track_r = right - points_w_ - gap;
        const float thick = k.s(chart_tok::kTrackThick);
        if (track_r > x) {
            const float full = track_r - x;
            dl->AddRectFilled({x, cy - thick * 0.5f}, {track_r, cy + thick * 0.5f}, u32(col::kFill),
                              thick * 0.5f);
            dl->AddRectFilled({x, cy - thick * 0.5f},
                              {x + std::max(thick, full * static_cast<float>(run.score) / peak), cy + thick * 0.5f},
                              u32(run.current ? col::kAccent : dim_accent()), thick * 0.5f);
        }
    }
}

// The score over the runs, oldest at the left: a dot a run, the page's run bigger, the best a star
// with its points over it. Days stand under the dots where they fit, else under the two ends.
void SummaryChart::draw_line(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts) {
    const bool brief = parts.plot.w < chart_tok::kBriefBelow * k.u;
    measure(k, brief);
    const float size = k.s(tok::kCaption);
    const float gap = k.s(tok::kS1);
    const int n = static_cast<int>(chart.runs.size());
    const float star = k.s(tok::kIconSm);
    const float top_inset = star + size + 2.0f * gap; // the best's star and its points above it
    const float foot_inset = size + 2.0f * gap;       // the days
    const float day_w = std::min(day_w_, parts.plot.w * chart_tok::kDayShare);
    const float margin = std::max(points_w_, day_w) * 0.5f + gap;
    const RectF& p = parts.plot;
    const float x0 = p.x + margin;
    const float x1 = p.right() - margin;
    const float y_top = p.y + top_inset;
    const float y_bottom = p.bottom() - foot_inset;
    int lo = chart.runs.empty() ? 0 : chart.runs[0].score;
    int hi = lo;
    for (const ChartRun& run : chart.runs) {
        lo = std::min(lo, run.score);
        hi = std::max(hi, run.score);
    }
    const float pad = std::max(0.12f * static_cast<float>(hi - lo), 1.0f);
    const float span = static_cast<float>(hi - lo) + 2.0f * pad;
    std::array<ImVec2, kChartRuns> at{};
    for (int i = 0; i < n; ++i) {
        const float fx = n > 1 ? static_cast<float>(i) / static_cast<float>(n - 1) : 0.5f;
        const float fy = (static_cast<float>(chart.runs[static_cast<std::size_t>(i)].score - lo) + pad) / span;
        at[static_cast<std::size_t>(i)] = {x0 + (x1 - x0) * fx, y_bottom - (y_bottom - y_top) * fy};
    }
    dl->AddLine({p.x, y_bottom + gap}, {p.right(), y_bottom + gap}, u32(col::kHairline), k.s(tok::kHairline));
    if (n > 1) {
        dl->AddPolyline(at.data(), n, u32(col::kAccent), 0, k.s(chart_tok::kLineThick));
    }
    const float step = n > 1 ? (x1 - x0) / static_cast<float>(n - 1) : x1 - x0;
    const bool every_value = points_w_ + gap <= step;
    const bool every_day = day_w + gap <= step;
    for (int i = 0; i < n; ++i) {
        const std::size_t c = static_cast<std::size_t>(i);
        const ChartRun& run = chart.runs[c];
        const ImVec2 pt = at[c];
        const bool best = i == chart.star;
        if (best) {
            board_star(dl, pt, star, gold(k));
        } else {
            const float r = k.s(run.current ? chart_tok::kDotNow : chart_tok::kDot);
            dl->AddCircleFilled(pt, r, u32(col::kAccent));
        }
        // The points: over every dot where they all fit, else over the best's and under the
        // page's run, unless a dot near the foot leaves no room under it before the days: then
        // over it.
        const float reach = (best ? star : k.s(chart_tok::kDotNow)) * 0.5f + gap;
        const bool room_below = chart_points_fit_below(pt.y, reach, size, y_bottom + gap);
        const bool above = every_value || best || (run.current && !room_below);
        if (above || run.current) {
            const float top = above ? pt.y - reach - size : pt.y + reach;
            label_at(k, dl, run.current || best ? k.bold : k.regular, size,
                     {pt.x - points_w_ * 0.5f, top, pt.x + points_w_ * 0.5f, top + size},
                     LabelAlign::Centre, value_ink(k, chart, c), words_.points[c].c_str());
        }
        const bool end = i == 0 || i == n - 1;
        if (every_day || end) {
            const FitText day = fit(run.current ? k.bold : k.regular, size,
                                    brief ? words_.day_brief[c] : words_.day[c], day_w);
            const float left = every_day || n == 1 ? pt.x - day_w * 0.5f : (i == 0 ? p.x : p.right() - day_w);
            label_at(k, dl, run.current ? k.bold : k.regular, size,
                     {left, p.bottom() - foot_inset + gap, left + day_w, p.bottom()},
                     every_day ? LabelAlign::Centre : (i == 0 ? LabelAlign::Left : LabelAlign::Right),
                     run.current ? col::kText : col::kText3, day.c_str());
        }
    }
}

// This run's points and place, under the runs.
void SummaryChart::draw_foot(Kit& k, ImDrawList* dl, const RunsChart& chart, const ChartParts& parts) {
    const RectF& r = parts.line;
    float x = r.x;
    ImVec4 ink = col::kText2;
    if (chart.place == RunPlace::Best) {
        ink = chart.rank == 1 ? gold(k) : green(k);
    }
    if (chart.place == RunPlace::Best && chart.rank == 1) {
        const float star = k.s(tok::kIconSm);
        board_star(dl, {x + star * 0.5f, r.y + r.h * 0.5f}, star, ink);
        x += star + k.s(tok::kS1);
    }
    const float room = std::max(0.0f, r.right() - x);
    if (foot_u_ != k.u || foot_width_ != room) {
        foot_u_ = k.u;
        foot_width_ = room;
        foot_brief_ = text_size(k.bold, k.s(tok::kCaption), words_.line.c_str()).x > room;
    }
    const FitText shown =
        fit(k.bold, k.s(tok::kCaption), foot_brief_ ? words_.line_brief : words_.line, room);
    label_at(k, dl, k.bold, k.s(tok::kCaption), {x, r.y, r.right(), r.bottom()}, LabelAlign::Left, ink,
             shown.c_str());
}

void SummaryChart::draw(Kit& k, ImDrawList* dl, const RunsChart& chart, std::int32_t today,
                        RunsChartView view, const RectF& area) {
    if (!built_ || today != today_ || !(chart == shown_)) {
        rebuild(chart, today);
    }
    const ChartParts parts = chart_parts(area, k.u);
    switch (view) {
    case RunsChartView::Bars:
        draw_bars(k, dl, chart, parts);
        break;
    case RunsChartView::Line:
        draw_line(k, dl, chart, parts);
        break;
    case RunsChartView::Ranked:
        draw_ranked(k, dl, chart, parts);
        break;
    }
    draw_foot(k, dl, chart, parts);
    audit::note("##board-chart", {area.x, area.y}, {area.right(), area.bottom()}, InkKind::Target);
}

} // namespace etude::app::ui
