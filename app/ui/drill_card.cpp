// SPDX-License-Identifier: GPL-3.0-or-later
// A drill's "How it went" card (#373; results_card.hpp, drill_card_layout.hpp): what the run of a Learn
// drill shows in the results card's place: the accuracy, the notes hit, how it compares with last
// time, whether the notes came early or late, and the buttons Again, Faster (the same drill five beats
// a minute quicker) and the Learn card it came from.
#include "results_card.hpp"

#include "bar.hpp" // begin_layer
#include "card_kit.hpp"
#include "drill_card_layout.hpp"
#include "icons.hpp"

#include "etude/drill.hpp"
#include "etude/unit_format.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

namespace {

// The timing: where the notes fell between early and late, as a marker on a track, with the counts.
void timing_block(Kit& k, ImDrawList* dl, const ResultsCard& c, const RectF& r) {
    const int early = c.counts.early;
    const int late = c.counts.late;
    const int off = early + late;
    // 0 at all early, 1 at all late; a run with none early or late sits in the middle.
    const float where = off > 0 ? static_cast<float>(late) / static_cast<float>(off) : 0.5f;
    const bool steady = off == 0 || std::abs(where - 0.5f) < 0.15f;
    const float s1 = k.s(tok::kCaption);
    const float s2 = k.s(tok::kLabel);
    const float line_h = r.h * 0.34f;
    const char* word = steady ? "Steady" : where < 0.5f ? "Early" : "Late";
    label_at(k, dl, k.bold, s1, {r.x, r.y, r.x + r.w * 0.5f, r.y + line_h}, LabelAlign::Left,
             col::kText2, "Timing");
    label_at(k, dl, k.bold, s1, {r.x + r.w * 0.5f, r.y, r.right(), r.y + line_h}, LabelAlign::Right,
             col::kText2, word);
    const float track_y = r.y + r.h * 0.5f;
    const float thick = k.s(tok::kS1);
    dl->AddRectFilled({r.x, track_y - thick * 0.5f}, {r.right(), track_y + thick * 0.5f},
                      u32(col::kFill), thick * 0.5f);
    const float mx = r.x + where * r.w;
    dl->AddCircleFilled({mx, track_y}, k.s(tok::kS2), u32(col::kText), 16);
    char early_text[16];
    char late_text[16];
    std::snprintf(early_text, sizeof early_text, "Early %d", early);
    std::snprintf(late_text, sizeof late_text, "Late %d", late);
    const GradeInk early_ink = grade_ink(k, Grade::Early);
    const GradeInk late_ink = grade_ink(k, Grade::Late);
    label_at(k, dl, k.regular, s1, {r.x, r.bottom() - line_h, r.x + r.w * 0.5f, r.bottom()},
             LabelAlign::Left, early_ink.top, early_text);
    label_at(k, dl, k.regular, s1, {r.x + r.w * 0.5f, r.bottom() - line_h, r.right(), r.bottom()},
             LabelAlign::Right, late_ink.top, late_text);
    (void)s2;
}

// "How it went" over the drill's title and its tempo, and the check that closes the card.
CardChoice drill_head(Kit& k, ImDrawList* dl, const ResultsCard& result, const DrillCardLayout& l) {
    CardChoice choice;
    FitText subtitle;
    subtitle.format("%s %s %d", result.drill_title.c_str(), "\xC2\xB7", result.drill_bpm);
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    const FitText sub = fit(k.regular, s2, subtitle.view(), l.title.w);
    const StackLine top{k.bold, s1, col::kText, "How it went"};
    const StackLine bottom{k.regular, s2, col::kText2, sub.c_str()};
    label_stack(k, dl, {l.title.x, l.title.y, l.title.right(), l.title.bottom()}, top, &bottom);
    if (icon_toggle(k, "##drill-done", icon::kCheck, "Done",
                    {l.done.x + l.done.w * 0.5f, l.done.y + l.done.h * 0.5f}, {l.done.w, l.done.h},
                    k.s(tok::kFace), false, true)) {
        choice = {CardAction::Done, HandFilter::Both, 0, {}, 0};
    }
    return choice;
}

// The figure, the notes and how they compare, and the timing.
void drill_figures(Kit& k, ImDrawList* dl, const ResultsCard& result, const DrillCardLayout& l) {
    FitText figure;
    if (result.accuracy_pct >= 0) {
        figure.assign(format_unit(result.accuracy_pct, Unit::Percent).c_str());
    } else {
        figure.assign("\xE2\x80\x93");
    }
    const float fsize = k.s(card_tok::kFigureSize);
    label_at(k, dl, k.bold, fsize, {l.figure.x, l.figure.y, l.figure.right(), l.figure.bottom()},
             LabelAlign::Left, col::kText, figure.c_str());
    FitText notes;
    const RunCounts& n = result.counts;
    const int hit = n.perfect + n.good + n.early + n.late;
    if (result.ungraded || hit + n.missed == 0) {
        notes.assign("No notes graded");
    } else {
        notes.format("%d of %d notes", hit, hit + n.missed);
    }
    FitText versus;
    card_comparison_line(result, &versus);
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    const FitText a = fit(k.bold, s1, notes.view(), l.notes.w);
    const FitText b = fit(k.regular, s2, versus.view(), l.notes.w);
    const StackLine first{k.bold, s1, col::kText, a.c_str()};
    const StackLine second{k.regular, s2, col::kText2, b.c_str()};
    label_stack(k, dl, {l.notes.x, l.notes.y, l.notes.right(), l.notes.bottom()}, first,
                b.empty() ? nullptr : &second);
    if (l.timing.w > 0.0f && !result.ungraded) {
        timing_block(k, dl, result, l.timing);
    }
}

// Again, Faster (its new tempo) and the Learn card.
CardChoice drill_buttons(Kit& k, const ResultsCard& result, const DrillCardLayout& l) {
    CardChoice choice;
    if (card_button(k, "##drill-again", {icon::kReplay}, "Again", l.again, true)) {
        choice = {CardAction::Replay, HandFilter::Both, 0, {}, 0};
    }
    FitText faster;
    faster.format("%d", std::min(result.drill_bpm + kDrillBpmStep, kDrillMaxBpm));
    if (card_button(k, "##drill-faster", {icon::kArrowUp}, faster.c_str(), l.faster, false,
                    result.drill_bpm < kDrillMaxBpm)) {
        choice = {CardAction::Faster, HandFilter::Both, 0, {}, 0};
    }
    if (card_button(k, "##drill-card", {icon::kEnter}, "Card", l.to_card, false)) {
        choice = {CardAction::ToCard, HandFilter::Both, 0, {}, 0};
    }
    return choice;
}

} // namespace

CardChoice ResultsCardView::draw_drill(Kit& k, const ResultsCard& result, const RectF& area) {
    CardChoice choice;
    const float unit = drill_card_unit(area, k.u);
    const ScaledKit scaled(k, unit);
    const DrillCardLayout l = drill_card_layout(area, unit);
    if (!l.fits) {
        return choice;
    }
    scale_ = unit;
    progress_ = approach(progress_, 1.0f, k.dt, tok::kMove);
    const float shown_p = ease_out(progress_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown_p);
    if (begin_layer("##drill-card", to_vec(l.card.x, l.card.y), to_vec(l.card.w, l.card.h))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float grow = card_tok::kEnterScale + (1.0f - card_tok::kEnterScale) * shown_p;
        const ImVec2 centre = to_vec(l.card.x + l.card.w * 0.5f, l.card.y + l.card.h * 0.5f);
        const ImVec2 half = to_vec(l.card.w * 0.5f * grow, l.card.h * 0.5f * grow);
        ui::overlay_card(k, dl, to_vec(centre.x - half.x, centre.y - half.y),
                         to_vec(centre.x + half.x, centre.y + half.y), k.s(tok::kRadiusLg));
        if (!l.compact) {
            choice = drill_head(k, dl, result, l);
        }
        drill_figures(k, dl, result, l);
        const CardChoice pressed = drill_buttons(k, result, l);
        if (pressed.action != CardAction::None) {
            choice = pressed;
        }
        shown_ = true;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return choice;
}

} // namespace etude::app::ui
