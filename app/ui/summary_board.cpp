// SPDX-License-Identifier: GPL-3.0-or-later
#include "summary_board.hpp"

#include "card_kit.hpp"
#include "fit_text.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "profile_ui.hpp"
#include "segment_layout.hpp"
#include "summary_chart.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>

namespace etude::app::ui {

namespace {

constexpr const char* kDash = "\xE2\x80\x93";

Box slot_of(const RectF& r) { return {r.x, r.y, r.right(), r.bottom()}; }

const Profile* profile_of(const BoardInfo& board, std::int64_t id) {
    return board.profiles != nullptr ? board.profiles->find(id) : nullptr;
}

// ---- The points and what they beat --------------------------------------------------------------

// A passive pill (not pressable): `text` in `ink` on a faint wash of it, an icon before the words.
// Returns its width, or 0 when it does not fit in `room`.
float pill(Kit& k, ImDrawList* dl, float x, float cy, float room, const char* icon,
           const char* text, ImVec4 ink) {
    const float size = k.s(tok::kLabel);
    const float h = k.s(tok::kChip);
    const float pad = k.s(tok::kS3);
    const float icon_w = icon != nullptr ? k.s(tok::kIconSm) + k.s(tok::kS1) : 0.0f;
    const float text_w = text_size(k.bold, size, text).x;
    const float w = pad * 2.0f + icon_w + text_w;
    if (w > room) {
        return 0.0f;
    }
    dl->AddRectFilled({x, cy - h * 0.5f}, {x + w, cy + h * 0.5f}, u32(with_alpha(ink, 0.16f)),
                      h * 0.5f);
    float at = x + pad;
    if (icon != nullptr) {
        glyph(k, dl, icon, {at + k.s(tok::kIconSm) * 0.5f, cy}, k.s(tok::kIconSm), ink);
        at += icon_w;
    }
    label_at(k, dl, k.bold, size, {at, cy - h * 0.5f, at + text_w, cy + h * 0.5f},
             LabelAlign::Left, ink, text);
    return w;
}

// What the run did against the one before it, as the pill's words (empty: nothing to say); `brief`
// for where the long words do not fit.
void compare_words(const SongBoard& b, bool brief, FitText* out) {
    switch (b.vs) {
    case ScoreVs::First:
        out->assign(brief ? "First run" : "First run on this song");
        break;
    case ScoreVs::Beat:
        if (brief) {
            out->format("+%d", b.delta);
        } else {
            out->format("Beat your last run +%d", b.delta);
        }
        break;
    case ScoreVs::Tied:
        out->assign(brief ? "Same" : "Same as your last run");
        break;
    case ScoreVs::Behind:
        if (brief) {
            out->format("%d", b.delta);
        } else {
            out->format("%d under your last run", -b.delta);
        }
        break;
    case ScoreVs::NotCounted:
        break;
    }
}

// The run's points big, "points" beside them, then the pills that fit: against the last run, then
// the personal best.
void head_part(Kit& k, ImDrawList* dl, const SongBoard& b, const RectF& head, SummaryChart& chart) {
    const float cy = head.y + head.h * 0.5f;
    if (!b.has_run) {
        // Where the head is narrow (the chart shares the row) the short words stand in, then a cut.
        const float size = k.s(tok::kBody);
        label_at(k, dl, k.bold, size, slot_of(head), LabelAlign::Left, col::kText2,
                 chart.too_short_head(k, size, head.w).c_str());
        return;
    }
    FitText points;
    board_points_words(b.run_score, &points);
    const float big = k.s(tok::kDisplay);
    const float pw = text_size(k.bold, big, points.c_str()).x;
    label_at(k, dl, k.bold, big, {head.x, head.y, head.x + pw, head.bottom()}, LabelAlign::Left,
             col::kText, points.c_str());
    float x = head.x + pw + k.s(tok::kS2);
    const float word = k.s(tok::kCaption);
    const float ww = text_size(k.regular, word, "points").x;
    label_at(k, dl, k.regular, word, {x, head.y, x + ww, head.bottom()}, LabelAlign::Left,
             col::kText2, "points");
    x += ww + k.s(tok::kS3);
    const ImVec4 ink = b.vs == ScoreVs::Behind ? col::kText2 : grade_ink(k, Grade::Perfect).top;
    const char* arrow = b.vs == ScoreVs::Behind ? icon::kArrowDown : icon::kArrowUp;
    for (const bool brief : {false, true}) {
        FitText vs;
        compare_words(b, brief, &vs);
        const float w = vs.empty() ? 0.0f : pill(k, dl, x, cy, head.right() - x, arrow, vs.c_str(), ink);
        if (w > 0.0f) {
            x += w + k.s(tok::kS2);
            break;
        }
    }
    if (b.is_personal_best && b.vs != ScoreVs::First) {
        pill(k, dl, x, cy, head.right() - x, icon::kSparks, "Personal best",
             grade_ink(k, Grade::Late).top);
    }
}

// The icon and the words of the button that switches the chart to `view`.
const char* chart_view_glyph(RunsChartView view) {
    switch (view) {
    case RunsChartView::Bars:
        return icon::kChartBars;
    case RunsChartView::Line:
        return icon::kChartLine;
    case RunsChartView::Ranked:
        break;
    }
    return icon::kOneByOne; // list-ordered
}

const char* chart_view_label(RunsChartView view) {
    switch (view) {
    case RunsChartView::Bars:
        return "Show your runs as bars";
    case RunsChartView::Line:
        return "Show your runs as a line";
    case RunsChartView::Ranked:
        break;
    }
    return "Show your runs best first";
}

// ---- The table ----------------------------------------------------------------------------------

void column_heads(Kit& k, ImDrawList* dl, const RectF& head) {
    const BoardColumns c = board_columns(head, k.u);
    const float size = k.s(tok::kCaption);
    const auto at = [&](const RectF& r, LabelAlign a, const char* text) {
        label_at(k, dl, k.bold, size, slot_of(r), a, col::kText3, text);
    };
    at(c.rank, LabelAlign::Left, "#");
    at({c.player.x, head.y, c.player.w, head.h}, LabelAlign::Left, "Player");
    if (!c.accuracy.empty()) {
        at(c.accuracy, LabelAlign::Right, "Accuracy");
        at(c.speed, LabelAlign::Right, "Speed");
    }
    at(c.points, LabelAlign::Right, "Points");
}

// The player's cell: the avatar, the name over the day of the best run.
void player_cell(Kit& k, ImDrawList* dl, const BoardInfo& board, const SongBoardRow& row,
                 bool mine, const BoardColumns& c) {
    const Profile* who = profile_of(board, row.profile_id);
    const float d = std::min(k.s(tok::kChip) + k.s(tok::kS1), c.player.h - k.s(tok::kS2));
    const float cy = c.player.y + c.player.h * 0.5f;
    float x = c.player.x;
    if (who != nullptr) {
        profile_avatar(k, dl, {x + d * 0.5f, cy}, d, *who);
    }
    x += d + k.s(tok::kS2);
    const float avail = std::max(0.0f, c.player.right() - x);
    FitText name;
    name.assign(who != nullptr ? who->name : "?");
    if (mine) {
        name.append(" (you)");
    }
    FitText day;
    if (row.runs > 0) {
        board_day_words(row.day, board.today, &day);
    }
    const FitText n = fit(k.bold, k.s(tok::kLabel), name, avail);
    const FitText dw = fit(k.regular, k.s(tok::kCaption), day, avail);
    const StackLine top{k.bold, k.s(tok::kLabel), col::kText, n.c_str()};
    const StackLine bottom{k.regular, k.s(tok::kCaption), col::kText2, dw.c_str()};
    label_stack(k, dl, {x, c.player.y, x + avail, c.player.bottom()}, top,
                row.runs > 0 ? &bottom : nullptr);
}

void figure_cells(Kit& k, ImDrawList* dl, const SongBoardRow& row, const BoardColumns& c) {
    const float size = k.s(tok::kLabel);
    if (row.runs == 0) {
        for (const RectF* r : {&c.accuracy, &c.speed, &c.points}) {
            if (!r->empty()) {
                label_at(k, dl, k.regular, size, slot_of(*r), LabelAlign::Right, col::kText3, kDash);
            }
        }
        return;
    }
    FitText text;
    if (!c.accuracy.empty()) {
        text.format("%d%%", row.accuracy_pct);
        label_at(k, dl, k.regular, size, slot_of(c.accuracy), LabelAlign::Right, col::kText,
                 text.c_str());
        text.format("%d%%", row.speed_percent);
        label_at(k, dl, k.regular, size, slot_of(c.speed), LabelAlign::Right, col::kText2,
                 text.c_str());
    }
    board_points_words(row.score, &text);
    label_at(k, dl, k.bold, k.s(tok::kBody), slot_of(c.points), LabelAlign::Right, col::kText,
             text.c_str());
}

void table_row(Kit& k, ImDrawList* dl, const BoardInfo& board, const SongBoardRow& row,
               const RectF& face) {
    const bool mine = row.profile_id == board.active;
    const float radius = k.s(tok::kRadiusControl);
    if (mine) {
        dl->AddRectFilled({face.x, face.y}, {face.right(), face.bottom()}, u32(col::kSelected), radius);
        dl->AddRect({face.x, face.y}, {face.right(), face.bottom()}, u32(col::kSelectedLine), radius,
                    0, k.s(tok::kHairline));
    } else {
        figure_row_face(k, dl, face);
    }
    const BoardColumns c = board_columns(face, k.u);
    FitText rank;
    if (row.runs > 0) {
        rank.format("%d", row.rank);
    } else {
        rank.assign(kDash);
    }
    const bool leader = row.runs > 0 && row.rank == 1;
    label_at(k, dl, k.bold, k.s(tok::kLabel), slot_of(c.rank), LabelAlign::Left,
             leader ? col::kAccent : col::kText2, rank.c_str());
    player_cell(k, dl, board, row, mine, c);
    figure_cells(k, dl, row, c);
}

// The lowest speed among the lines shown (0: none has a run).
int lowest_speed(const SongBoard& b, std::span<const int> shown) {
    int lowest = 0;
    for (const int i : shown) {
        const SongBoardRow& row = b.rows[static_cast<std::size_t>(i)];
        if (row.runs > 0 && (lowest == 0 || row.speed_percent < lowest)) {
            lowest = row.speed_percent;
        }
    }
    return lowest;
}

// The table's lines, one per profile. Those that fit stand where the layout put them; where the
// area has no room for all, they are in one window that scrolls with a drag, opening at the
// player's own line (so no player is lost to a small screen).
void table_rows(Kit& k, ImDrawList* dl, const BoardInfo& board, const SongBoard& b,
                const SummaryBoard& l) {
    const int count = static_cast<int>(b.rows.size());
    if (count <= l.row_count) {
        for (int i = 0; i < count; ++i) {
            const RectF& face = l.rows[static_cast<std::size_t>(i)];
            table_row(k, dl, board, b.rows[static_cast<std::size_t>(i)], face);
            char id[24];
            std::snprintf(id, sizeof(id), "##board-row%d", i);
            audit::note(id, {face.x, face.y}, {face.right(), face.bottom()}, InkKind::Target);
        }
        return;
    }
    const RectF& view = l.rows_view;
    const float pitch = l.row_h + l.row_gap;
    ImGui::SetCursorScreenPos({view.x, view.y});
    // The window opens at the player's own line: asked for before it begins, so its first frame is
    // already there (a scroll set inside it would apply a frame late). It is new when it was not
    // drawn on the frame before.
    ImGuiStorage* seen = ImGui::GetStateStorage();
    const ImGuiID key = ImGui::GetID("##board-rows-seen");
    const int frame = ImGui::GetFrameCount();
    if (seen->GetInt(key, -2) < frame - 1) {
        int active = -1;
        for (int i = 0; i < count; ++i) {
            if (b.rows[static_cast<std::size_t>(i)].profile_id == board.active) {
                active = i;
            }
        }
        ImGui::SetNextWindowScroll({-1.0f, board_first_scroll(count, active, l.row_h, l.row_gap, view.h)});
    }
    seen->SetInt(key, frame);
    ImGui::BeginChild("##board-rows", {view.w, view.h}, ImGuiChildFlags_None,
                      ImGuiWindowFlags_NoBackground);
    ImDrawList* cdl = ImGui::GetWindowDrawList();
    const float scroll = ImGui::GetScrollY();
    for (int i = 0; i < count; ++i) {
        const RectF face{view.x, view.y + static_cast<float>(i) * pitch - scroll, view.w, l.row_h};
        if (face.bottom() < view.y || face.y > view.bottom()) {
            continue;
        }
        table_row(k, cdl, board, b.rows[static_cast<std::size_t>(i)], face);
        if (face.y >= view.y && face.bottom() <= view.bottom()) {
            char id[24];
            std::snprintf(id, sizeof(id), "##board-row%d", i);
            audit::note(id, {face.x, face.y}, {face.right(), face.bottom()}, InkKind::Target);
        }
    }
    ImGui::SetCursorPos({0.0f, static_cast<float>(count) * pitch - l.row_gap});
    ImGui::Dummy({1.0f, 1.0f});
    drag_scroll(k);
    ImGui::EndChild();
}

// The column heads, the lines (`table_rows`), then the footnote.
void table_part(Kit& k, ImDrawList* dl, const BoardInfo& board, const SongBoard& b,
                const SummaryBoard& l) {
    if (!l.table_head.empty()) {
        column_heads(k, dl, l.table_head);
    }
    table_rows(k, dl, board, b, l);
    if (!l.footnote.empty()) {
        std::array<int, board_tok::kMaxRows> all{};
        const int n = std::clamp(static_cast<int>(b.rows.size()), 0, board_tok::kMaxRows);
        for (int i = 0; i < n; ++i) {
            all[static_cast<std::size_t>(i)] = i;
        }
        FitText words;
        board_footnote_words(lowest_speed(b, std::span<const int>(all.data(), static_cast<std::size_t>(n))),
                             &words);
        const FitText shown_words = fit(k.regular, k.s(tok::kCaption), words, l.footnote.w);
        label_at(k, dl, k.regular, k.s(tok::kCaption), slot_of(l.footnote), LabelAlign::Left,
                 col::kText3, shown_words.c_str());
    }
}

// The player's runs: the label over them (empty: none), the chart, and the button that switches
// its view. True when the button was tapped.
bool chart_part(Kit& k, ImDrawList* dl, const BoardInfo& board, const SongBoard& data,
                const RectF& label, const RectF& area, const RectF& view, SummaryChart* chart) {
    if (!label.empty()) {
        label_at(k, dl, k.bold, k.s(tok::kLabel), slot_of(label), LabelAlign::Left, col::kText2,
                 "Your runs on this song");
    }
    chart->draw(k, dl, data.chart, board.today, board.chart_view, area);
    // The button shows the view it switches to.
    const RunsChartView next = next_runs_chart_view(board.chart_view);
    ImGui::SetCursorScreenPos({view.x, view.y});
    return icon_action(k, "##chart-view", chart_view_glyph(next), chart_view_label(next), view.w, true);
}

} // namespace

bool segment_cells(Kit& k, const char* id_prefix, std::span<const RectF> cells,
                   std::span<const char* const> labels, int* current, float dead,
                   SegmentFit* fits) {
    if (cells.empty()) {
        return false;
    }
    const float word_size = k.s(tok::kLabel);
    const float word_w = cells[0].w - k.s(tok::kS2);
    SegmentFit local;
    if (fits == nullptr) {
        fits = &local;
    }
    if (fits->size != word_size || fits->width != word_w || fits->labels != labels.data() ||
        fits->count != cells.size()) {
        fits->size = word_size;
        fits->width = word_w;
        fits->labels = labels.data();
        fits->count = cells.size();
        for (std::size_t i = 0; i < cells.size() && i < SegmentFit::kMax; ++i) {
            fits->regular[i] = fit(k.regular, word_size, labels[i], word_w);
            fits->bold[i] = fit(k.bold, word_size, labels[i], word_w);
        }
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ch = std::min(cells[0].h, k.s(tok::kFace));
    const float face_y = cells[0].y + (cells[0].h - ch) * 0.5f;
    dl->AddRectFilled({cells[0].x, face_y}, {cells.back().right(), face_y + ch}, u32(col::kFill),
                      k.s(tok::kRadiusControl));
    const float inset = segment_pill_inset(k.u); // the pill sits this far in on all four sides
    const ImGuiID key = ImGui::GetID(id_prefix);
    int bold_cell = -1;
    if (*current >= 0 && *current < static_cast<int>(cells.size())) {
        // The chosen pill, sliding from the cell it was on to the one now chosen.
        const RectF& at = cells[static_cast<std::size_t>(*current)];
        const SegmentCellBoxes b = segment_cell_boxes(at.x, at.right(), k.u);
        const Box target{b.pill_x0, face_y + inset, b.pill_x1, face_y + ch - inset};
        const Box pill = slide_pill(k, key, *current, 0, target);
        State chosen;
        chosen.selected = 1.0f;
        draw_state(k, dl, "##pill", pill, slot_of(at), chosen, Idle::None, k.s(tok::kRadiusInner));
        // The words are bold in the cell nearest the pill's centre: one face of each word in a frame.
        std::array<float, SegmentFit::kMax> centres{};
        const std::size_t count = std::min(cells.size(), centres.size());
        for (std::size_t i = 0; i < count; ++i) {
            centres[i] = cells[i].x + cells[i].w * 0.5f;
        }
        bold_cell = pill_bold_cell((pill.x0 + pill.x1) * 0.5f,
                                   std::span<const float>(centres.data(), count));
    }
    bool changed = false;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const RectF& cell = cells[i];
        char id[32];
        std::snprintf(id, sizeof(id), "%s%d", id_prefix, static_cast<int>(i));
        // The hit area leaves the dead gap between two cells (half of it at each side).
        const RectF hit_rect = segment_hit(cell, static_cast<int>(i), static_cast<int>(cells.size()), dead);
        const Box hit{hit_rect.x, hit_rect.y, hit_rect.right(), hit_rect.bottom()};
        const bool pressed = press_area(k, id, hit);
        audit::note(id, {hit.x0, hit.y0}, {hit.x1, hit.y1}, InkKind::Target);
        const bool on = static_cast<int>(i) == *current;
        // The chosen pill is drawn above; each cell draws its own hover and press.
        const State st = item_state(k, false, true, false);
        const SegmentCellBoxes pill = segment_cell_boxes(cell.x, cell.right(), k.u);
        draw_state(k, dl, id, {pill.pill_x0, face_y + inset, pill.pill_x1, face_y + ch - inset},
                   slot_of(cell), st, Idle::None, k.s(tok::kRadiusInner));
        const float weight = cell_weight(k, key, static_cast<int>(i), on);
        const bool bold = static_cast<int>(i) == bold_cell;
        const FitText& text = bold ? fits->bold[i] : fits->regular[i];
        fade_label(k, dl, k.s(tok::kLabel), {hit.x0, face_y, hit.x1, face_y + ch}, LabelAlign::Centre,
                   text.c_str(), bold, weight, state_ink(st, col::kText2), state_ink(st, col::kText));
        if (tapped(pressed) && !on) {
            *current = static_cast<int>(i);
            changed = true;
        }
    }
    return changed;
}

bool draw_board_tab(Kit& k, ImDrawList* dl, const BoardInfo& board, const SummaryLayout& l,
                    Period* period, SummaryChart* chart, SegmentFit* period_fit) {
    static constexpr std::array<const char*, 3> kPeriods{"Week", "Month", "All"};
    const SummaryBoard& b = l.board;
    int chosen = static_cast<int>(*period);
    if (segment_cells(k, "##board-period", b.period, kPeriods, &chosen, tok::kHitGap * l.scale,
                      period_fit)) {
        *period = static_cast<Period>(chosen);
    }
    if (!b.caption.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kCaption), slot_of(b.caption), LabelAlign::Left,
                 col::kText3, "Best run of each person on this song");
    }
    const SongBoard* data = board.board(*period);
    if (data == nullptr) {
        label_at(k, dl, k.regular, k.s(tok::kLabel), slot_of(b.head), LabelAlign::Left, col::kText2,
                 "Working it out");
        return false;
    }
    head_part(k, dl, *data, b.head, *chart);
    table_part(k, dl, board, *data, b);
    if (b.chart.empty() || data->chart.runs.empty()) {
        return false;
    }
    dl->AddLine({b.divider.x, b.divider.y}, {b.divider.x, b.divider.bottom()}, u32(col::kHairline),
                k.s(tok::kHairline));
    return chart_part(k, dl, board, *data, b.chart_label, b.chart, b.chart_view, chart);
}

bool draw_runs_tab(Kit& k, ImDrawList* dl, const BoardInfo& board, const SummaryLayout& l,
                   Period period, SummaryChart* chart) {
    const SummaryRuns& r = l.runs;
    const SongBoard* data = board.board(period);
    if (data == nullptr || data->chart.runs.empty()) {
        label_at(k, dl, k.regular, k.s(tok::kLabel), slot_of(r.chart), LabelAlign::Left, col::kText2,
                 "Working it out");
        return false;
    }
    return chart_part(k, dl, board, *data, r.label, r.chart, r.view, chart);
}

} // namespace etude::app::ui
