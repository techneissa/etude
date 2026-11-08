// SPDX-License-Identifier: GPL-3.0-or-later
#include "progress_charts.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"

#include "etude/progress_text.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <string>

namespace etude::app::ui {

namespace {

namespace pg = progress_tok;

constexpr float kPi = 3.14159265f;

ImVec2 pt(float x, float y) { return {x, y}; }

// The box inside `box` shrunk by the page's padding.
Box inset(const Kit& k, const Box& box) {
    const float p = k.s(progress_tok::kPad);
    return {box.x0 + p, box.y0 + p, box.x1 - p, box.y1 - p};
}

Box title_slot(const Kit& k, const Box& inner) {
    return {inner.x0, inner.y0, inner.x1, inner.y0 + k.s(progress_tok::kTitleH)};
}

// A card's caption at the right of its title line, cut to what the title leaves (none when that
// is too little to read).
void caption_right(const Kit& k, ImDrawList* dl, const Box& head, const char* title, const char* text,
                   float reserved = 0.0f) {
    const float taken = text_size(k.bold, k.s(tok::kLabel), title).x + k.s(tok::kS3) + reserved;
    const float room = head.w() - taken;
    if (room < k.s(pg::kCaptionMinW)) {
        return;
    }
    const FitText cut = fit(k.regular, k.s(tok::kCaption), text, room);
    label_at(k, dl, k.regular, k.s(tok::kCaption), {head.x0, head.y0, head.x1 - reserved, head.y1},
             LabelAlign::Right, col::kText3, cut.c_str());
}

// A calendar cell's fill: the base grey for no practice, then four steps that each stand out from
// the last (a muted teal, the teal, a strong teal, a bright mint for a long session).
ImVec4 calendar_shade(int level) {
    static constexpr ImVec4 kSteps[etude::kCalendarLevels] = {
        {0.17f, 0.78f, 0.69f, 0.55f},
        {0.17f, 0.78f, 0.69f, 0.78f},
        {0.17f, 0.78f, 0.69f, 1.00f},
        {0.62f, 1.00f, 0.90f, 1.00f}};
    if (level <= 0) {
        return with_alpha(col::kText, 0.05f);
    }
    return kSteps[std::min(level, etude::kCalendarLevels) - 1];
}

// "Less [] [] [] [] [] More" at the right end of the card's title line; returns the room it took.
float calendar_legend(const Kit& k, ImDrawList* dl, const Box& head, float title_w) {
    const float cell = k.s(pg::kLegendCell);
    const float gap = k.s(pg::kCellGap);
    const float word = k.s(tok::kCaption);
    const float less_w = text_size(k.regular, word, "Less").x;
    const float more_w = text_size(k.regular, word, "More").x;
    const int steps = etude::kCalendarLevels + 1;
    const float cells_w = static_cast<float>(steps) * cell + static_cast<float>(steps - 1) * gap;
    const float total = less_w + k.s(tok::kS1) + cells_w + k.s(tok::kS1) + more_w;
    if (title_w + k.s(tok::kS3) + total > head.w()) {
        return 0.0f; // no room beside the title
    }
    float x = head.x1 - total;
    label_at(k, dl, k.regular, word, {x, head.y0, x + less_w, head.y1}, LabelAlign::Left, col::kText3,
             "Less");
    x += less_w + k.s(tok::kS1);
    for (int level = 0; level < steps; ++level) {
        const float y = head.cy() - cell * 0.5f;
        dl->AddRectFilled({x, y}, {x + cell, y + cell}, u32(calendar_shade(level)), k.s(pg::kCellRound));
        x += cell + gap;
    }
    x += k.s(tok::kS1) - gap;
    label_at(k, dl, k.regular, word, {x, head.y0, x + more_w, head.y1}, LabelAlign::Left, col::kText3,
             "More");
    return total + k.s(tok::kS3);
}

} // namespace

void progress_card(Kit& k, ImDrawList* dl, const Box& box, const char* title) {
    card(k, dl, pt(box.x0, box.y0), pt(box.x1, box.y1), k.s(tok::kRadiusCard), col::kSurface,
         ImDrawFlags_RoundCornersAll, 0.0f);
    if (title != nullptr) {
        const Box inner = inset(k, box);
        label_at(k, dl, k.bold, k.s(tok::kLabel), title_slot(k, inner), LabelAlign::Left, col::kText2,
                 title);
    }
}

void draw_calendar(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view) {
    progress_card(k, dl, box, "Practice calendar");
    const Box inner = inset(k, box);
    const Box head = title_slot(k, inner);
    char goal[96];
    if (view.goal.has_goal) {
        std::snprintf(goal, sizeof goal, "goal %d min a day", view.goal.goal_minutes);
    } else {
        std::snprintf(goal, sizeof goal, "no daily goal");
    }
    const float legend =
        calendar_legend(k, dl, head, text_size(k.bold, k.s(tok::kLabel), "Practice calendar").x);
    caption_right(k, dl, head, "Practice calendar", goal, legend);
    if (view.calendar.empty()) {
        return;
    }
    const int offset = etude::weekday_monday0(view.calendar.front().day);
    const int days = static_cast<int>(view.calendar.size());
    const int cols = (days + offset + 6) / 7;
    const float label_w = k.s(pg::kCalendarLabelW);
    const Box grid{inner.x0 + label_w, head.y1 + k.s(tok::kS2), inner.x1, inner.y1};
    const float pitch = std::min(grid.w() / static_cast<float>(cols), grid.h() / 7.0f);
    if (pitch < k.s(pg::kCellMin)) {
        return;
    }
    const float gap = std::max(1.0f, k.s(pg::kCellGap));
    const float cell = pitch - gap;
    const float round = k.s(pg::kCellRound);
    const float x0 = grid.x0;
    for (int i = 0; i < days; ++i) {
        const etude::CalendarCell& c = view.calendar[static_cast<std::size_t>(i)];
        const int col_i = (i + offset) / 7;
        const int row_i = (i + offset) % 7;
        const ImVec2 a{x0 + static_cast<float>(col_i) * pitch, grid.y0 + static_cast<float>(row_i) * pitch};
        dl->AddRectFilled(a, {a.x + cell, a.y + cell}, u32(calendar_shade(c.level)), round);
    }
    static constexpr const char* kDays[7] = {"Mon", "", "Wed", "", "Fri", "", "Sun"};
    for (int r = 0; r < 7; r += 2) {
        const Box slot{inner.x0, grid.y0 + static_cast<float>(r) * pitch, inner.x0 + label_w - k.s(tok::kS1),
                       grid.y0 + static_cast<float>(r) * pitch + cell};
        label_at(k, dl, k.regular, k.s(tok::kCaption), slot, LabelAlign::Left, col::kText3, kDays[r]);
    }
}

void draw_graph(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view) {
    progress_card(k, dl, box, "Accuracy and speed");
    const Box inner = inset(k, box);
    const Box head = title_slot(k, inner);
    const std::vector<etude::AccuracyPoint>& pts = view.me.accuracy;
    if (pts.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kBody),
                 {inner.x0, head.y1, inner.x1, inner.y1}, LabelAlign::Centre, col::kText3,
                 "Play a few songs and your accuracy shows here");
        return;
    }
    char caption[96];
    std::snprintf(caption, sizeof caption, "last %zu runs \xC2\xB7 grey ticks are the speed", pts.size());
    caption_right(k, dl, head, "Accuracy and speed", caption);
    int low = 100;
    for (const etude::AccuracyPoint& p : pts) {
        low = std::min(low, p.accuracy_pct);
    }
    const int floor_pct = low < 50 ? 0 : 50;
    const float axis_w = k.s(pg::kAxisW);
    const float ticks_h = k.s(pg::kTicksH);
    const Box plot{inner.x0 + axis_w, head.y1 + k.s(tok::kS3), inner.x1 - k.s(tok::kS2),
                   inner.y1 - ticks_h - k.s(tok::kS1)};
    if (plot.h() < k.s(pg::kPlotMinH) || plot.w() < k.s(pg::kPlotMinW)) {
        return;
    }
    const auto y_of = [&](int pct) {
        const float t = static_cast<float>(std::clamp(pct, floor_pct, 100) - floor_pct) /
                        static_cast<float>(100 - floor_pct);
        return plot.y1 - t * plot.h();
    };
    for (const int mark : {100, (100 + floor_pct) / 2, floor_pct}) {
        char text[16];
        std::snprintf(text, sizeof text, "%d%%", mark);
        dl->AddLine({plot.x0, y_of(mark)}, {plot.x1, y_of(mark)}, u32(col::kHairline), k.s(tok::kHairline));
        label_at(k, dl, k.regular, k.s(tok::kCaption),
                 {inner.x0, y_of(mark) - k.s(pg::kAxisHalfH), inner.x0 + axis_w - k.s(tok::kS1), y_of(mark) + k.s(pg::kAxisHalfH)},
                 LabelAlign::Left, col::kText3, text);
    }
    const std::size_t n = pts.size();
    const auto x_of = [&](std::size_t i) {
        return n == 1 ? plot.cx() : plot.x0 + plot.w() * static_cast<float>(i) / static_cast<float>(n - 1);
    };
    for (std::size_t i = 0; i + 1 < n; ++i) {
        const ImVec2 a{x_of(i), y_of(pts[i].accuracy_pct)};
        const ImVec2 b{x_of(i + 1), y_of(pts[i + 1].accuracy_pct)};
        dl->AddQuadFilled(a, b, {b.x, plot.y1}, {a.x, plot.y1}, u32(with_alpha(col::kAccent, 0.16f)));
    }
    for (std::size_t i = 0; i + 1 < n; ++i) {
        dl->AddLine({x_of(i), y_of(pts[i].accuracy_pct)}, {x_of(i + 1), y_of(pts[i + 1].accuracy_pct)},
                    u32(col::kAccent), k.s(pg::kLine));
    }
    dl->AddCircleFilled({x_of(n - 1), y_of(pts[n - 1].accuracy_pct)}, k.s(pg::kDotR), u32(col::kAccent));
    // The speed of each run: a tick under it, taller the faster (25 % is the least, 200 % the most).
    const float tick_w = std::max(1.5f, std::min(k.s(pg::kTickW), plot.w() / static_cast<float>(n) * 0.5f));
    for (std::size_t i = 0; i < n; ++i) {
        const float t = std::clamp(static_cast<float>(pts[i].speed_percent - 25) / 175.0f, 0.0f, 1.0f);
        const float h = ticks_h * (0.2f + 0.8f * t);
        const float x = x_of(i);
        dl->AddRectFilled({x - tick_w * 0.5f, inner.y1 - h}, {x + tick_w * 0.5f, inner.y1},
                          u32(with_alpha(col::kText3, 0.7f)), tick_w * 0.4f);
    }
}

void draw_best(Kit& k, ImDrawList* dl, const Box& box, std::span<const Box> rows,
               const etude::ProgressView& view) {
    progress_card(k, dl, box, "Best per song");
    const Box inner = inset(k, box);
    label_at(k, dl, k.regular, k.s(tok::kCaption), title_slot(k, inner), LabelAlign::Right, col::kText3,
             "points");
    if (view.bests.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kBody), {inner.x0, inner.y0 + k.s(progress_tok::kTitleH), inner.x1, inner.y1},
                 LabelAlign::Centre, col::kText3, "Your best runs show here");
        return;
    }
    const float points_w = k.s(pg::kPointsW);
    const float pill_w = view.household_size > 1 ? k.s(pg::kPillW) : 0.0f;
    const float acc_w = k.s(pg::kAccW);
    const float speed_w = k.s(pg::kSpeedW);
    const float trend_w = k.s(pg::kTrendW);
    for (std::size_t i = 0; i < rows.size() && i < view.bests.size(); ++i) {
        const Box r = rows[i];
        const etude::SongBestRow& b = view.bests[i];
        const float gap = k.s(tok::kS2);
        float right = r.x1;
        const Box points{right - points_w, r.y0, right, r.y1};
        const std::string score = etude::thousands(b.score);
        label_at(k, dl, k.bold, k.s(tok::kBody), points, LabelAlign::Right, col::kText, score.c_str());
        right = points.x0 - gap;
        if (pill_w > 0.0f) {
            const Box pill{right - pill_w, r.cy() - k.s(pg::kPillHalfH), right, r.cy() + k.s(pg::kPillHalfH)};
            if (b.rank == 1) {
                dl->AddRectFilled({pill.x0, pill.y0}, {pill.x1, pill.y1},
                                  u32(with_alpha(progress_col::kGold, 0.18f)), k.s(pg::kPillHalfH));
                label_at(k, dl, k.bold, k.s(tok::kCaption), pill, LabelAlign::Centre, progress_col::kGold,
                         "1st");
            }
            right = pill.x0 - gap;
        }
        const Box acc{right - acc_w, r.y0, right, r.y1};
        char text[48];
        std::snprintf(text, sizeof text, "%d%%", b.accuracy_pct);
        label_at(k, dl, k.regular, k.s(tok::kBody), acc, LabelAlign::Right, col::kText2, text);
        right = acc.x0 - gap;
        float title_right = right;
        const float title_min = k.s(pg::kBestTitleMinW);
        const bool show_speed = right - r.x0 - speed_w >= title_min;
        if (show_speed) {
            const Box speed{right - speed_w, r.y0, right, r.y1};
            std::snprintf(text, sizeof text, "speed %d%%", b.speed_percent);
            label_at(k, dl, k.regular, k.s(tok::kCaption), speed, LabelAlign::Right, col::kText3, text);
            right = speed.x0 - gap;
            title_right = right;
        }
        if (right - r.x0 - trend_w >= title_min && b.trend.size() >= 2) {
            const Box t{right - trend_w, r.y0 + k.s(pg::kTrendInset), right, r.y1 - k.s(pg::kTrendInset)};
            const auto [lo_it, hi_it] = std::minmax_element(b.trend.begin(), b.trend.end());
            const float lo = static_cast<float>(*lo_it);
            const float span = std::max(1.0f, static_cast<float>(*hi_it) - lo);
            const std::size_t n = b.trend.size();
            for (std::size_t j = 0; j + 1 < n; ++j) {
                const auto pos = [&](std::size_t q) {
                    return ImVec2{t.x0 + t.w() * static_cast<float>(q) / static_cast<float>(n - 1),
                                  t.y1 - t.h() * (static_cast<float>(b.trend[q]) - lo) / span};
                };
                dl->AddLine(pos(j), pos(j + 1), u32(col::kAccent), k.s(pg::kTrendLine));
            }
            title_right = t.x0 - gap;
        }
        const Box title{r.x0, r.y0, title_right, r.y1};
        const FitText name = fit(k.regular, k.s(tok::kBody), b.title, title.w());
        label_at(k, dl, k.regular, k.s(tok::kBody), title, LabelAlign::Left, col::kText, name.c_str());
    }
}

void draw_ring(Kit& k, ImDrawList* dl, const Box& box, const etude::ProgressView& view) {
    progress_card(k, dl, box, nullptr);
    const Box inner = inset(k, box);
    const float d = std::min(k.s(progress_tok::kRing), inner.h());
    const ImVec2 c{inner.x0 + d * 0.5f, inner.cy()};
    const float stroke = k.s(pg::kRingStroke);
    const float r = d * 0.5f - stroke * 0.5f;
    const bool met = view.goal.has_goal && view.goal.met;
    const ImVec4 ring_col = met ? progress_col::kTeal : col::kAccent;
    dl->AddCircle(c, r, u32(with_alpha(col::kText, 0.12f)), 0, stroke);
    const float fraction = view.goal.has_goal ? static_cast<float>(view.goal.fraction) : 0.0f;
    if (fraction > 0.0f) {
        dl->PathArcTo(c, r, -kPi * 0.5f, -kPi * 0.5f + 2.0f * kPi * std::min(fraction, 0.999f), 48);
        dl->PathStroke(u32(ring_col), 0, stroke);
    }
    if (met) {
        glyph(k, dl, icon::kCheck, c, k.s(tok::kIcon), progress_col::kTeal);
    }
    const float text_x = inner.x0 + d + k.s(tok::kS3);
    const float line_h = inner.h() / 3.0f;
    const std::string week = etude::minutes_words(view.me.minutes_week);
    label_at(k, dl, k.bold, k.s(tok::kTitle), {text_x, inner.y0, inner.x1, inner.y0 + line_h * 1.2f},
             LabelAlign::Left, col::kText, week.c_str());
    const std::string month = etude::minutes_words(view.me.minutes_month);
    FitText sub;
    sub.format("this week \xC2\xB7 %s this month", month.c_str());
    const FitText sub_cut = fit(k.regular, k.s(tok::kCaption), sub.view(), inner.x1 - text_x);
    label_at(k, dl, k.regular, k.s(tok::kCaption),
             {text_x, inner.y0 + line_h * 1.2f, inner.x1, inner.y0 + line_h * 2.1f}, LabelAlign::Left,
             col::kText2, sub_cut.c_str());
    FitText goal;
    if (!view.goal.has_goal) {
        goal.format("no daily goal");
    } else if (met) {
        goal.format("goal %d min a day, met today", view.goal.goal_minutes);
    } else {
        goal.format("today %d of %d min", view.me.minutes_today, view.goal.goal_minutes);
    }
    const FitText goal_cut = fit(k.regular, k.s(tok::kCaption), goal.view(), inner.x1 - text_x);
    label_at(k, dl, k.regular, k.s(tok::kCaption),
             {text_x, inner.y0 + line_h * 2.1f, inner.x1, inner.y1}, LabelAlign::Left,
             met ? progress_col::kTeal : col::kText3, goal_cut.c_str());
}

void draw_tile(Kit& k, ImDrawList* dl, const Box& box, const char* icon_glyph, const char* bold,
               const char* caption) {
    progress_card(k, dl, box, nullptr);
    const Box inner = inset(k, box);
    const float d = std::min(k.s(pg::kIconBox), inner.h());
    const ImVec2 c{inner.x0 + d * 0.5f, inner.cy()};
    dl->AddRectFilled({c.x - d * 0.5f, c.y - d * 0.5f}, {c.x + d * 0.5f, c.y + d * 0.5f},
                      u32(with_alpha(col::kText, 0.06f)), k.s(tok::kRadiusControl));
    glyph(k, dl, icon_glyph, c, k.s(tok::kIcon), col::kAccent);
    const Box text{inner.x0 + d + k.s(tok::kS2), inner.y0, inner.x1, inner.y1};
    const FitText head = fit(k.bold, k.s(pg::kTileHead), bold, text.w());
    const FitText tail = fit(k.regular, k.s(tok::kCaption), caption, text.w());
    const StackLine top{k.bold, k.s(pg::kTileHead), col::kText, head.c_str()};
    const StackLine bottom{k.regular, k.s(tok::kCaption), col::kText2, tail.c_str()};
    label_stack(k, dl, text, top, &bottom);
}

} // namespace etude::app::ui
