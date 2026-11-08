// SPDX-License-Identifier: GPL-3.0-or-later
#include "run_card.hpp"

#include "bar.hpp" // begin_layer
#include "card_kit.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"

#include "etude/grade_palette.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cstdio>

namespace etude::app::ui {

namespace {

constexpr const char* kDot = "\xC2\xB7";
constexpr const char* kDash = "\xE2\x80\x93";

// An accuracy figure in the colour of its tier, or a dash.
void accuracy_text(Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                   LabelAlign align, int pct) {
    FitText text;
    Grade tier = Grade::Perfect;
    if (pct >= 0 && accuracy_grade(pct, &tier)) {
        text.assign(format_unit(pct, Unit::Percent).c_str());
        text_gradient(k, dl, font, size, label_pen(font, size, slot, align, text.c_str()),
                      grade_ink(k, tier), text.c_str());
    } else {
        label_at(k, dl, font, size, slot, align, col::kText2, kDash);
    }
}

float accuracy_width(ImFont* font, float size, int pct) {
    if (pct < 0) {
        return text_size(font, size, kDash).x;
    }
    return text_size(font, size, format_unit(pct, Unit::Percent).c_str()).x;
}

const char* end_word(BarEnd end) {
    switch (end) {
    case BarEnd::Cleared:
        return "Cleared";
    case BarEnd::Skipped:
        return "Skipped";
    case BarEnd::Ended:
        return "Ended here";
    case BarEnd::Playing:
    case BarEnd::Waiting:
        break;
    }
    return "Not played";
}

// The bar's grade counts as cells, in the results card's order: the five grades, the wrong
// presses and the longest run of hits (Wait Mode: the notes played, the wrong presses and the run).
// Returns how many.
int grade_cells(const BarProgress& p, FigureCell* out) {
    int n = 0;
    if (p.played > 0) {
        out[n++] = {"Played", p.played, Grade::Perfect};
        out[n++] = {"Wrong", p.wrong, Grade::Wrong};
        out[n++] = {"Combo", p.combo, Grade::Combo};
        return n;
    }
    out[n++] = {grade_name(Grade::Perfect).data(), p.perfect, Grade::Perfect};
    out[n++] = {grade_name(Grade::Good).data(), p.good, Grade::Good};
    out[n++] = {grade_name(Grade::Early).data(), p.early, Grade::Early};
    out[n++] = {grade_name(Grade::Late).data(), p.late, Grade::Late};
    out[n++] = {"Miss", p.missed, Grade::Miss};
    out[n++] = {"Wrong", p.wrong, Grade::Wrong};
    out[n++] = {"Combo", p.combo, Grade::Combo};
    return n;
}

constexpr int kMaxGradeCells = 7;

void bar_name(const TroubleBar& bar, FitText* out) {
    if (bar.pass > 0) {
        out->format("Bar %d, %d%s time", bar.written, bar.pass, ordinal_suffix(bar.pass));
    } else {
        out->format("Bar %d", bar.written);
    }
}

void check_in_headline(Kit& k, ImDrawList* dl, const TroubleRun& run, const RunCardLayout& l) {
    const BarProgress& p = run.progress(run.current());
    const int now = p.accuracy_pct();
    const float fsize = k.s(card_tok::kFigureSize);
    accuracy_text(k, dl, k.bold, fsize,
                  {l.figure.x, l.figure.y, l.figure.right(), l.figure.bottom()}, LabelAlign::Left,
                  now);
    const float fw = accuracy_width(k.bold, fsize, now);
    const float text_x = std::min(l.text.x, l.figure.x + fw + k.s(tok::kS3));
    const float text_w = l.text.w + (l.text.x - text_x);

    FitText first;
    FitText name;
    bar_name(run.bar(run.current()), &name);
    first.format("%s cleared", name.c_str());
    FitText second;
    if (run.next_bar() >= 0) {
        FitText next;
        bar_name(run.bar(run.next_bar()), &next);
        second.format("Up next: %s", next.c_str());
    } else {
        second.assign("That was the last bar");
    }
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    const FitText a = fit(k.bold, s1, first, text_w);
    const FitText b = fit(k.regular, s2, second, text_w);
    const StackLine top{k.bold, s1, col::kText, a.c_str()};
    const StackLine bottom{k.regular, s2, col::kText2, b.c_str()};
    label_stack(k, dl, {text_x, l.text.y, l.text.right(), l.text.bottom()}, top, &bottom);
}

// The top of the check-in: how many of each grade the bar got, the results card's counts row.
void check_in_counts(Kit& k, ImDrawList* dl, const TroubleRun& run, const RunCardLayout& l) {
    FigureCell cells[kMaxGradeCells];
    const int n = grade_cells(run.progress(run.current()), cells);
    grade_counts_row(k, dl, l.counts, cells, std::min(l.count_cells, n));
}

// Below the headline: passes, clean passes, and how the bar went before the run.
void check_in_stats(Kit& k, ImDrawList* dl, const TroubleRun& run, const RunCardLayout& l) {
    const BarProgress& p = run.progress(run.current());
    const TroubleBar& bar = run.bar(run.current());
    figure_row_face(k, dl, l.stats);
    const float vs = k.s(tok::kBody);
    const float cs = k.s(tok::kCaption);
    const float cell_w = l.stats.w / static_cast<float>(l.stat_cells);
    for (int i = 0; i < l.stat_cells; ++i) {
        const char* caption = i == 0 ? "Passes" : i == 1 ? "Clean passes" : "Before";
        FitText value;
        if (i == 0) {
            value.format("%d", p.passes);
        } else if (i == 1) {
            value.format("%d", p.clean_passes);
        } else if (bar.before_pct >= 0) {
            value.assign(format_unit(bar.before_pct, Unit::Percent).c_str());
        } else {
            value.assign(kDash);
        }
        const ImVec2 vz = text_size(k.bold, vs, value.c_str());
        const FitText c = fit(k.regular, cs, caption, cell_w);
        const ImVec2 cz = text_size(k.regular, cs, c.c_str());
        const float x0 = l.stats.x + static_cast<float>(i) * cell_w;
        const float y = l.stats.y + (l.stats.h - vz.y - cz.y) * 0.5f;
        // The number over its word, each centred in its line's box by its ink.
        const Box value_slot{x0, y, x0 + cell_w, y + vz.y};
        const Box word_slot{x0, y + vz.y, x0 + cell_w, y + vz.y + cz.y};
        if (i == 1 && p.clean_passes > 0) {
            // A clean pass is a pass with nothing missed: Perfect's colour.
            text_gradient(k, dl, k.bold, vs,
                          label_pen(k.bold, vs, value_slot, LabelAlign::Centre, value.c_str()),
                          grade_ink(k, Grade::Perfect), value.c_str());
        } else if (i == 2) {
            accuracy_text(k, dl, k.bold, vs, value_slot, LabelAlign::Centre, bar.before_pct);
        } else {
            label_at(k, dl, k.bold, vs, value_slot, LabelAlign::Centre,
                     i == 0 && p.passes > 0 ? col::kText : col::kText2, value.c_str());
        }
        label_at(k, dl, k.regular, cs, word_slot, LabelAlign::Centre, col::kText2, c.c_str());
    }
}

void summary_headline(Kit& k, ImDrawList* dl, const TroubleRun& run, const RunCardLayout& l) {
    FitText figure;
    figure.format("%d/%d", run.cleared(), run.count());
    const float fsize = k.s(card_tok::kFigureSize);
    const ImVec2 fs = text_size(k.bold, fsize, figure.c_str());
    // Cleared bars are the good news: the figure is Perfect's colour when every bar was cleared.
    const Box figure_slot{l.figure.x, l.figure.y, l.figure.right(), l.figure.bottom()};
    if (run.cleared() == run.count()) {
        text_gradient(k, dl, k.bold, fsize,
                      label_pen(k.bold, fsize, figure_slot, LabelAlign::Left, figure.c_str()),
                      grade_ink(k, Grade::Perfect), figure.c_str());
    } else {
        label_at(k, dl, k.bold, fsize, figure_slot, LabelAlign::Left, col::kText, figure.c_str());
    }
    const float text_x = std::min(l.text.x, l.figure.x + fs.x + k.s(tok::kS3));
    const float text_w = l.text.w + (l.text.x - text_x);
    FitText first;
    first.assign(run.finished() ? "Run finished" : "Run ended");
    FitText second;
    second.format("%d of %d bars cleared", run.cleared(), run.count());
    if (run.skipped() > 0) {
        char skipped[32];
        std::snprintf(skipped, sizeof(skipped), " %s %d skipped", kDot, run.skipped());
        second.append(skipped);
    }
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    const FitText a = fit(k.bold, s1, first, text_w);
    const FitText b = fit(k.regular, s2, second, text_w);
    const StackLine top{k.bold, s1, col::kText, a.c_str()};
    const StackLine bottom{k.regular, s2, col::kText2, b.c_str()};
    label_stack(k, dl, {text_x, l.text.y, l.text.right(), l.text.bottom()}, top, &bottom);
}

// The bar's grades that happened, each its number in the grade's colour and its word: "9 Perfect  3
// Good  1 Miss". The ones that do not fit the room are left off.
void row_counts_line(Kit& k, ImDrawList* dl, const BarProgress& p, const Box& line) {
    float x = line.x0;
    const float right = line.x1;
    FigureCell cells[kMaxGradeCells];
    const int n = grade_cells(p, cells);
    const float s = k.s(tok::kCaption);
    const float gap = k.s(tok::kS3);
    for (int i = 0; i < n; ++i) {
        if (cells[i].value <= 0) {
            continue;
        }
        FitText value;
        value.format("%d", cells[i].value);
        const float vw = text_size(k.bold, s, value.c_str()).x;
        const float space = k.s(tok::kS1);
        const float cw = text_size(k.regular, s, cells[i].caption).x;
        if (x + vw + space + cw > right) {
            break;
        }
        const Box value_slot{x, line.y0, x + vw, line.y1};
        text_gradient(k, dl, k.bold, s,
                      label_pen(k.bold, s, value_slot, LabelAlign::Left, value.c_str()),
                      grade_ink(k, cells[i].grade), value.c_str());
        label_at(k, dl, k.regular, s, {x + vw + space, line.y0, x + vw + space + cw, line.y1},
                 LabelAlign::Left, col::kText2, cells[i].caption);
        x += vw + space + cw + gap;
    }
}

// One row of the summary: the bar, its accuracy before and after in their colours, and how it
// ended; with `counts`, a second line holds the bar's grade counts.
void summary_row(Kit& k, ImDrawList* dl, const TroubleRun& run, int index, const RectF& cell,
                 bool counts) {
    const TroubleBar& bar = run.bar(index);
    const BarProgress& p = run.progress(index);
    figure_row_face(k, dl, cell);
    const float pad = k.s(tok::kS3);
    // With the counts line the first line sits in the upper part of the row.
    const Box line{cell.x, cell.y, cell.right(), cell.y + cell.h * (counts ? 0.66f : 1.0f)};
    const float cy = line.cy();
    const float s1 = k.s(tok::kLabel);
    const float s2 = k.s(tok::kCaption);
    // The columns: the name, the change, the ending.
    const float name_w = (cell.w - 2.0f * pad) * 0.34f;
    const float change_w = (cell.w - 2.0f * pad) * 0.30f;
    FitText name;
    bar_name(bar, &name);
    const FitText n = fit(k.bold, s1, name, name_w);
    label_at(k, dl, k.bold, s1, {cell.x + pad, line.y0, cell.x + pad + name_w, line.y1},
             LabelAlign::Left, col::kText, n.c_str());

    const float cx = cell.x + pad + name_w;
    const int after = p.passes > 0 ? p.accuracy_pct() : -1;
    const float bw = accuracy_width(k.bold, s1, bar.before_pct);
    const float aw = accuracy_width(k.bold, s1, after);
    const float arrow = k.s(tok::kIconSm);
    const float arrow_w = arrow + 2.0f * k.s(tok::kS1);
    float x = cx + std::max(0.0f, (change_w - bw - arrow_w - aw) * 0.5f);
    accuracy_text(k, dl, k.bold, s1, {x, line.y0, x + bw, line.y1}, LabelAlign::Left,
                  bar.before_pct);
    x += bw + k.s(tok::kS1);
    glyph(k, dl, icon::kChevron, to_vec(x + arrow * 0.5f, cy), arrow, col::kText2);
    x += arrow + k.s(tok::kS1);
    accuracy_text(k, dl, k.bold, s1, {x, line.y0, x + aw, line.y1}, LabelAlign::Left, after);

    FitText end;
    if (p.passes > 0) {
        end.format("%s %s %d pass%s", end_word(p.end), kDot, p.passes, p.passes == 1 ? "" : "es");
    } else {
        end.assign(end_word(p.end));
    }
    const float ex = cx + change_w;
    const FitText e = fit(k.regular, s2, end, cell.right() - pad - ex);
    label_at(k, dl, k.regular, s2, {ex, line.y0, cell.right() - pad, line.y1}, LabelAlign::Right,
             p.end == BarEnd::Cleared ? col::kText : col::kText2, e.c_str());
    if (counts) {
        row_counts_line(k, dl, p,
                        {cell.x + pad, cell.y + cell.h * 0.5f, cell.right() - pad, cell.bottom()});
    }
}

} // namespace

RunCardAction RunCardView::draw(Kit& k, const TroubleRun* run, const RectF& area) {
    RunCardAction action = RunCardAction::None;
    shown_ = false;
    summary_ = false;
    const RunPhase phase = run != nullptr ? run->phase() : RunPhase::Idle;
    if (run == nullptr || (phase != RunPhase::CheckIn && phase != RunPhase::Done)) {
        last_phase_ = phase;
        last_current_ = -1;
        return action;
    }
    const RunCardKind kind = phase == RunPhase::Done ? RunCardKind::Summary : RunCardKind::CheckIn;
    FigureCell unused[kMaxGradeCells];
    const int grade_n =
        kind == RunCardKind::CheckIn ? grade_cells(run->progress(run->current()), unused) : 0;
    const RunCardLayout l = run_card_layout(area, k.u, kind, run->count(), grade_n);
    if (l.card.empty()) {
        return action;
    }
    if (phase != last_phase_ || run->current() != last_current_) {
        last_phase_ = phase;
        last_current_ = run->current();
        progress_ = 0.0f;
    }
    scale_ = l.scale;
    const ScaledKit scaled(k, l.scale);
    progress_ = approach(progress_, 1.0f, k.dt, tok::kMove);
    const float shown_p = ease_out(progress_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown_p);
    if (begin_layer("##run-card", to_vec(l.card.x, l.card.y), to_vec(l.card.w, l.card.h))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float scale = card_tok::kEnterScale + (1.0f - card_tok::kEnterScale) * shown_p;
        const ImVec2 centre = to_vec(l.card.x + l.card.w * 0.5f, l.card.y + l.card.h * 0.5f);
        const ImVec2 half = to_vec(l.card.w * 0.5f * scale, l.card.h * 0.5f * scale);
        ui::overlay_card(k, dl, to_vec(centre.x - half.x, centre.y - half.y),
                         to_vec(centre.x + half.x, centre.y + half.y), k.s(tok::kRadiusLg));
        if (kind == RunCardKind::CheckIn) {
            check_in_headline(k, dl, *run, l);
            if (!l.counts.empty()) {
                check_in_counts(k, dl, *run, l);
            }
            if (!l.stats.empty()) {
                check_in_stats(k, dl, *run, l);
            }
            const bool last = run->next_bar() < 0;
            if (card_button(k, "##run-next", {last ? icon::kCheck : icon::kPlay},
                            last ? "Finish" : "Next bar", l.button_cells[0], true)) {
                action = RunCardAction::Next;
            }
            if (card_button(k, "##run-again", {icon::kReplay}, "Again", l.button_cells[1], false)) {
                action = RunCardAction::Again;
            }
            if (card_button(k, "##run-skip", {icon::kSkipNext}, "Skip", l.button_cells[2], false,
                            !last)) {
                action = RunCardAction::Skip;
            }
            if (card_button(k, "##run-end", {icon::kStop}, "End", l.button_cells[3], false)) {
                action = RunCardAction::End;
            }
        } else {
            summary_headline(k, dl, *run, l);
            for (int i = 0; i < l.row_count; ++i) {
                summary_row(k, dl, *run, i, l.rows[static_cast<std::size_t>(i)], l.row_counts);
            }
            if (card_button(k, "##run-done", {icon::kCheck}, "Done", l.button_cells[0], true)) {
                action = RunCardAction::Done;
            }
        }
        shown_ = true;
        summary_ = kind == RunCardKind::Summary;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return action;
}

} // namespace etude::app::ui
