// SPDX-License-Identifier: GPL-3.0-or-later
#include "results_card.hpp"

#include "bar.hpp" // begin_layer
#include "card_kit.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"

#include "etude/bar_order.hpp"
#include "etude/grade_palette.hpp"

#include "etude/unit_format.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>

namespace etude::app::ui {

namespace {

constexpr const char* kDot = "\xC2\xB7";
constexpr const char* kDash = "\xE2\x80\x93";

const char* mode_name(RunMode mode) { return mode == RunMode::Wait ? "Wait" : "Flow"; }

const char* hands_name(HandFilter hands) {
    switch (hands) {
    case HandFilter::Left:
        return "Left hand";
    case HandFilter::Right:
        return "Right hand";
    case HandFilter::Both:
        break;
    }
    return "Both hands";
}

// The plain and the weighted score side by side, each over its caption, in the figure's room.
void score_pair(Kit& k, ImDrawList* dl, const ResultsCard& c, const CardLayout& l) {
    const float gap = k.s(tok::kS3);
    const float col_w = (l.figure.w - gap) * 0.5f;
    const float fig_h = l.figure.h * card_tok::kPairShare;
    const float fsize = k.s(card_tok::kPairFigureSize);
    const float csize = k.s(tok::kCaption);
    const int pct[2] = {c.accuracy_pct, c.weighted_pct};
    const char* caption[2] = {"Score", "Weighted"};
    for (int i = 0; i < 2; ++i) {
        const float x = l.figure.x + static_cast<float>(i) * (col_w + gap);
        const FitText figure = fit(k.bold, fsize, format_unit(pct[i], Unit::Percent).view(), col_w);
        label_at(k, dl, k.bold, fsize, {x, l.figure.y, x + col_w, l.figure.y + fig_h},
                 LabelAlign::Left, col::kText, figure.c_str());
        const FitText cap = fit(k.regular, csize, caption[i], col_w);
        label_at(k, dl, k.regular, csize, {x, l.figure.y + fig_h, x + col_w, l.figure.bottom()},
                 LabelAlign::Left, col::kText2, cap.c_str());
    }
}

void headline(Kit& k, ImDrawList* dl, const ResultsCard& c, const CardLayout& l) {
    float text_x = l.text.x;
    float text_w = l.text.w;
    if (c.weighted_pct >= 0 && c.accuracy_pct >= 0) {
        score_pair(k, dl, c, l);
    } else {
        FitText figure;
        if (c.accuracy_pct >= 0) {
            figure.assign(format_unit(c.accuracy_pct, Unit::Percent).c_str());
        } else {
            figure.assign(kDash);
        }
        const float fsize = k.s(card_tok::kFigureSize);
        const ImVec2 fs = text_size(k.bold, fsize, figure.c_str());
        label_at(k, dl, k.bold, fsize,
                 {l.figure.x, l.figure.y, l.figure.right(), l.figure.bottom()}, LabelAlign::Left,
                 col::kText, figure.c_str());
        // The lines start right after the figure, not after the room kept for "100%".
        text_x = std::min(l.text.x, l.figure.x + fs.x + k.s(tok::kS3));
        text_w = l.text.w + (l.text.x - text_x);
    }

    FitText mode;
    mode.format("%s %s %s%s", mode_name(c.mode), kDot, hands_name(c.hands),
                c.in_plan ? " \xC2\xB7 Practice list" : "");
    FitText first;
    FitText second;
    if (c.ungraded) {
        first.assign("No notes graded");
        second.assign(mode.view());
    } else {
        first.assign(mode.view());
        card_comparison_line(c, &second);
    }
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    const FitText a = fit(k.bold, s1, first, text_w);
    const FitText b = fit(k.regular, s2, second, text_w);
    const StackLine top{k.bold, s1, col::kText, a.c_str()};
    const StackLine bottom{k.regular, s2, col::kText2, b.c_str()};
    label_stack(k, dl, {text_x, l.text.y, l.text.right(), l.text.bottom()}, top,
                b.empty() ? nullptr : &bottom);

    // Each hand's own figure, in its colour, at the right end.
    if (!l.hands.empty()) {
        const float isize = k.s(tok::kIconSm);
        const float gap = k.s(tok::kS1);
        const float between = k.s(tok::kS3);
        const float cy = l.hands.y + l.hands.h * 0.5f;
        const int pct[2] = {c.left_pct, c.right_pct};
        FitText text[2];
        float width = between;
        for (int i = 0; i < 2; ++i) {
            text[i].assign(format_unit(pct[i], Unit::Percent).c_str());
            width += isize + gap + text_size(k.bold, s2, text[i].c_str()).x;
        }
        float x = l.hands.right() - width;
        for (int i = 0; i < 2; ++i) {
            glyph(k, dl, i == 0 ? icon::kLeftHand : icon::kRightHand, to_vec(x + isize * 0.5f, cy),
                  isize, i == 0 ? k.hand_left : k.hand_right);
            x += isize + gap;
            const ImVec2 ts = text_size(k.bold, s2, text[i].c_str());
            // The figure is in the colour of the grade its accuracy earns; the number is the cue.
            Grade tier = Grade::Perfect;
            const Box slot{x, l.hands.y, x + ts.x, l.hands.bottom()};
            if (accuracy_grade(pct[i], &tier)) {
                text_gradient(k, dl, k.bold, s2,
                              label_pen(k.bold, s2, slot, LabelAlign::Left, text[i].c_str()),
                              grade_ink(k, tier), text[i].c_str());
            } else {
                label_at(k, dl, k.bold, s2, slot, LabelAlign::Left, col::kText, text[i].c_str());
            }
            x += ts.x + between;
        }
    }
}

void counts_row(Kit& k, ImDrawList* dl, const ResultsCard& c, const CardLayout& l) {
    FigureCell cells[7];
    int n = 0;
    if (c.mode == RunMode::Wait) {
        cells[n++] = {"Played", c.counts.played, Grade::Perfect};
        cells[n++] = {"Wrong", c.wrong, Grade::Wrong};
        cells[n++] = {"Combo", c.longest_combo, Grade::Combo};
    } else {
        for (const Grade g : {Grade::Perfect, Grade::Good, Grade::Early, Grade::Late}) {
            const int value = g == Grade::Perfect ? c.counts.perfect
                              : g == Grade::Good  ? c.counts.good
                              : g == Grade::Early ? c.counts.early
                                                  : c.counts.late;
            cells[n++] = {grade_name(g).data(), value, g};
        }
        cells[n++] = {"Miss", c.counts.missed, Grade::Miss};
        cells[n++] = {"Wrong", c.wrong, Grade::Wrong};
        cells[n++] = {"Combo", c.longest_combo, Grade::Combo};
    }
    grade_counts_row(k, dl, l.counts, cells, std::min(l.count_cells, n));
}

// How a bar went, as the line under its name: the time through a repeated passage, then the misses
// or wrong presses; a suggested bar (no miss) says it is hard and by how many stars.
void bar_detail(const CardBar& bar, FitText& out) {
    if (bar.pass > 0) {
        out.format("%d%s time \xC2\xB7 ", bar.pass, ordinal_suffix(bar.pass));
    }
    char count[32];
    if (bar.suggested) {
        std::snprintf(count, sizeof(count), "Hard \xC2\xB7 %d%s stars", bar.half_stars / 2,
                      bar.half_stars % 2 != 0 ? ".5" : "");
    } else if (bar.missed > 0) {
        std::snprintf(count, sizeof(count), "%d missed", bar.missed);
    } else {
        std::snprintf(count, sizeof(count), "%d wrong", bar.wrong);
    }
    out.append(count);
}

// One trouble bar: a toggle. A chosen bar shows a check and the selected state; one left out an
// empty ring. The bar and how it went are the words.
bool bar_chip(Kit& k, int index, const CardBar& bar, const RectF& cell, bool chosen) {
    char id[24];
    std::snprintf(id, sizeof(id), "##results-bar%d", index);
    ImGui::SetCursorScreenPos(to_vec(cell.x, cell.y));
    const bool pressed = ImGui::InvisibleButton(id, to_vec(cell.w, cell.h));
    const State st = item_state(k, chosen);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float face_h = std::min(cell.h, k.s(tok::kFace));
    const ImVec2 a = to_vec(cell.x, cell.y + (cell.h - face_h) * 0.5f);
    const ImVec2 b = to_vec(cell.right(), a.y + face_h);
    draw_state(k, dl, id, {a.x, a.y, b.x, b.y}, {cell.x, cell.y, cell.right(), cell.bottom()}, st,
               Idle::Fill);
    const float isize = k.s(tok::kIconSm);
    const float pad = k.s(tok::kS3);
    glyph(k, dl, chosen ? icon::kChecked : icon::kUnchecked,
          to_vec(a.x + pad + isize * 0.5f, (a.y + b.y) * 0.5f), isize,
          state_ink(st, chosen ? col::kAccent : col::kText3));
    {
        char note[40];
        std::snprintf(note, sizeof(note), "%s-icon", id);
        const float cy = (a.y + b.y) * 0.5f;
        audit::note(note, to_vec(a.x + pad, cy - isize * 0.5f),
                    to_vec(a.x + pad + isize, cy + isize * 0.5f));
    }
    const float tx = a.x + pad + isize + k.s(tok::kS2);
    const float avail = b.x - k.s(tok::kS2) - tx;
    FitText name;
    name.format("Bar %d", bar.written);
    FitText detail;
    bar_detail(bar, detail);
    const float s1 = k.s(tok::kLabel);
    const float s2 = k.s(tok::kCaption);
    const FitText n = fit(k.bold, s1, name, avail);
    const FitText d = fit(k.regular, s2, detail, avail);
    const StackLine top{k.bold, s1, state_ink(st, chosen ? col::kText : col::kText2), n.c_str()};
    const StackLine bottom{k.regular, s2, chosen ? col::kText2 : col::kText3, d.c_str()};
    label_stack(k, dl, {tx, a.y, b.x, b.y}, top, &bottom);
    return tapped(pressed);
}

const char* hand_cell_id(HandFilter hands) {
    switch (hands) {
    case HandFilter::Left:
        return "##results-left";
    case HandFilter::Right:
        return "##results-right";
    case HandFilter::Both:
        break;
    }
    return "##results-both";
}

// One of the three hand choices: the drawn hands (backs of hands, the L or R mark, the look's hand
// colours; both hands side by side), lit in the hand's colour when it is the one just practised.
// A hand with no notes is grey and takes no tap but a hint.
bool hand_cell(Kit& k, HandFilter hands, const RectF& cell, bool marked, bool offered) {
    const char* id = hand_cell_id(hands);
    ImGui::SetCursorScreenPos(to_vec(cell.x, cell.y));
    const bool pressed = ImGui::InvisibleButton(id, to_vec(cell.w, cell.h));
    const ImVec4* tint = hands == HandFilter::Left    ? &k.hand_left
                         : hands == HandFilter::Right ? &k.hand_right
                                                      : nullptr;
    const State st = item_state(k, marked, true, !offered, tint);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float face_h = std::min(cell.h, k.s(tok::kFace));
    const Box face{cell.x, cell.y + (cell.h - face_h) * 0.5f, cell.right(),
                   cell.y + (cell.h + face_h) * 0.5f};
    draw_state(k, dl, id, face, {cell.x, cell.y, cell.right(), cell.bottom()}, st, Idle::Fill);
    const float size = k.s(tok::kIconSm);
    const float cy = (face.y0 + face.y1) * 0.5f;
    const float cx = (face.x0 + face.x1) * 0.5f;
    if (hands == HandFilter::Both) {
        glyph(k, dl, icon::kLeftHand, to_vec(cx - size * 0.5f, cy), size,
              state_ink(st, k.hand_left));
        glyph(k, dl, icon::kRightHand, to_vec(cx + size * 0.5f, cy), size,
              state_ink(st, k.hand_right));
    } else {
        const bool left = hands == HandFilter::Left;
        const char* mark = left ? "L" : "R";
        const float msize = k.s(tok::kMark);
        const ImVec2 ms = text_size(k.bold, msize, mark);
        const float gap = k.s(tok::kS1);
        const float x = cx - (size + gap + ms.x) * 0.5f;
        const ImVec4 ink = state_ink(st, left ? k.hand_left : k.hand_right);
        glyph(k, dl, left ? icon::kLeftHand : icon::kRightHand, to_vec(x + size * 0.5f, cy), size,
              ink);
        label_at(k, dl, k.bold, msize, {x + size + gap, face.y0, x + size + gap + ms.x, face.y1},
                 LabelAlign::Left, ink, mark);
    }
    if (pressed && !offered) {
        k.say(hand_hint(hands));
    }
    return offered && tapped(pressed);
}

} // namespace

std::span<const CardIcon> card_warm_icons() {
    static constexpr CardIcon kIcons[] = {
        {icon::kChecked, tok::kIconSm},  {icon::kUnchecked, tok::kIconSm},
        {icon::kOneByOne, tok::kIconSm}, {icon::kRepeat, tok::kIconSm},
        {icon::kCheck, tok::kIconSm},    {icon::kArrowUp, tok::kIcon},
        {icon::kArrowDown, tok::kIcon},  {icon::kPlay, tok::kIconSm},
        {icon::kBack, tok::kIconSm},     {icon::kSkipNext, tok::kIconSm},
        {icon::kStop, tok::kIconSm},     {icon::kChevron, tok::kIconSm}};
    return kIcons;
}

void ResultsCardView::start_order(const ResultsCard& result) {
    if (result.serial != demo_serial_ && result.bar_count >= 2) {
        demo_request_ = result.serial; // taken by the next draw, once it knows the card
    }
}

// The trouble bars: the caption, the two actions and a chip for each bar.
CardChoice ResultsCardView::draw_bars(Kit& k, ImDrawList* dl, const ResultsCard& card_in,
                                      const CardLayout& l) {
    CardChoice choice;
    const ResultsCard* result = &card_in;
        // Only the bars that have a chip count: the others stay out of the actions.
        const std::uint32_t visible = card_all_bars(l.bars_shown);
        const std::uint32_t picked = chosen_ & visible;
        const int picked_count = std::popcount(picked);
        FitText head;
        // With no miss to name, the bars are the song's hardest ones.
        const bool any_trouble = !result->bars[0].suggested;
        head.assign(any_trouble ? "Trouble bars" : "Hardest bars");
        const float s1 = k.s(tok::kLabel);
        const float s2 = k.s(tok::kCaption);
        const FitText h1 = fit(k.bold, s1, head, l.bars_caption.w);
        FitText count;
        count.format("%d of %d chosen", picked_count, l.bars_shown);
        const FitText h2 = fit(k.regular, s2, count, l.bars_caption.w);
        const StackLine top{k.bold, s1, col::kText, h1.c_str()};
        const StackLine bottom{k.regular, s2, col::kText2, h2.c_str()};
        label_stack(k, dl,
                    {l.bars_caption.x, l.bars_caption.y, l.bars_caption.right(),
                     l.bars_caption.bottom()},
                    top, &bottom);
        if (card_button(k, "##results-onebyone", {icon::kOneByOne}, "One by one",
                        l.action_cells[0], false, picked != 0)) {
            if (picked_count >= 2) {
                // Two bars or more: the player may choose which comes first.
                ordering_ = true;
                order_ = song_order(*result, picked);
                drag_ = {};
                key_ = 0;
            } else {
                choice = {CardAction::OneByOne, HandFilter::Both, picked, {}, 0};
            }
        }
        if (card_button(k, "##results-together", {icon::kRepeat}, "Together", l.action_cells[1],
                        false, picked != 0)) {
            choice = {CardAction::Together, HandFilter::Both, picked, {}, 0};
        }
        for (int i = 0; i < l.bars_shown; ++i) {
            const std::uint32_t bit = 1u << i;
            if (bar_chip(k, i, result->bars[static_cast<std::size_t>(i)],
                         l.bar_cells[static_cast<std::size_t>(i)], (chosen_ & bit) != 0)) {
                chosen_ ^= bit;
            }
        }
    return choice;
}

// The "How it went" chip at the headline's right (its words when the room allows).
void ResultsCardView::review_chip(Kit& k, const ReviewInfo& review, const CardLayout& l) {
    if (l.review.empty() || !review.offered) {
        return;
    }
    ChipOpts opts;
    opts.icon = icon::kGauge;
    opts.trail = l.review_words ? icon::kChevron : nullptr;
    opts.width = l.review.w;
    if (chip(k, "##results-review", l.review_words ? "How it went" : "",
             to_vec(l.review.x, l.review.y), opts)) {
        open_summary();
    }
}

// The buttons' row: Replay, the hands when they are chosen here, Done.
CardChoice ResultsCardView::draw_buttons(Kit& k, const ResultsCard& result, const CardLayout& l) {
    CardChoice choice;
    int cell = 0;
    if (card_button(k, "##results-replay", {icon::kReplay}, "Replay",
                    l.button_cells[static_cast<std::size_t>(cell++)], true)) {
        choice = {CardAction::Replay, HandFilter::Both, 0, {}, 0};
    }
    if (l.hand_cells) {
        for (const HandFilter hands : {HandFilter::Left, HandFilter::Right, HandFilter::Both}) {
            if (hand_cell(k, hands, l.button_cells[static_cast<std::size_t>(cell++)],
                          result.hands == hands, hand_offered(result.present, hands))) {
                choice = {CardAction::Hands, hands, 0, {}, 0};
            }
        }
    }
    if (card_button(k, "##results-done", {icon::kCheck}, "Done",
                    l.button_cells[static_cast<std::size_t>(cell++)], false)) {
        choice = {CardAction::Done, HandFilter::Both, 0, {}, 0};
    }
    return choice;
}

// The summary page, in the card's place; Back returns to the card, Done dismisses it.
CardChoice ResultsCardView::draw_summary(Kit& k, const ReviewInfo& review, const RectF& area,
                                         const RectF& page_area) {
    CardChoice choice;
    const ReviewChoice tapped = summary_.draw(k, review, page_area.empty() ? area : page_area,
                                              card_size(area, k.u).scale);
    shown_ = summary_.shown();
    scale_ = summary_.scale();
    if (tapped.action == ReviewAction::Done) {
        summary_open_ = false;
        choice = {CardAction::Done, HandFilter::Both, 0, {}, 0};
    } else if (tapped.action != ReviewAction::None) {
        review_choice_ = tapped;
    } else if (summary_.back()) {
        summary_open_ = false;
    }
    return choice;
}

ReviewChoice ResultsCardView::take_review_choice() {
    return std::exchange(review_choice_, ReviewChoice{});
}

void ResultsCardView::open_summary(bool board, bool runs) {
    if (!summary_open_) {
        summary_open_ = true;
        summary_.open();
        if (runs) {
            summary_.show_runs();
        } else if (board) {
            summary_.show_board();
        }
    }
}

CardChoice ResultsCardView::draw(Kit& k, const ResultsCard* result, const RectF& area,
                                 const ReviewInfo& review, const RectF& page_area) {
    CardChoice choice;
    shown_ = false;
    if (result == nullptr) {
        had_card_ = false;
        ordering_ = false;
        summary_open_ = false;
        drag_ = {};
        return choice;
    }
    if (!had_card_ || result->serial != serial_) {
        serial_ = result->serial;
        had_card_ = true;
        progress_ = 0.0f;
        chosen_ = result->start_bars;
        ordering_ = false;
        summary_open_ = false;
        drag_ = {};
    }
    if (demo_request_ == result->serial && demo_serial_ != result->serial) {
        demo_serial_ = result->serial;
        ordering_ = true;
        order_ = song_order(*result, result->start_bars);
        drag_ = {};
        key_ = 0;
    }
    if (result->drill_bpm > 0) {
        return draw_drill(k, *result, area);
    }
    if (ordering_) {
        const RectF order_area = area;
        return draw_order(k, *result, order_area);
    }
    if (summary_open_) {
        return draw_summary(k, review, area, page_area);
    }
    const CardLayout l = results_card_layout(area, k.u, *result);
    if (l.card.empty()) {
        return choice;
    }
    scale_ = l.scale;
    const ScaledKit scaled(k, l.scale);
    progress_ = approach(progress_, 1.0f, k.dt, tok::kMove);
    const float shown_p = ease_out(progress_);

    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown_p);
    if (begin_layer("##results", to_vec(l.card.x, l.card.y), to_vec(l.card.w, l.card.h))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float scale = card_tok::kEnterScale + (1.0f - card_tok::kEnterScale) * shown_p;
        const ImVec2 centre = to_vec(l.card.x + l.card.w * 0.5f, l.card.y + l.card.h * 0.5f);
        const ImVec2 half = to_vec(l.card.w * 0.5f * scale, l.card.h * 0.5f * scale);
        ui::overlay_card(k, dl, to_vec(centre.x - half.x, centre.y - half.y),
                         to_vec(centre.x + half.x, centre.y + half.y), k.s(tok::kRadiusLg));

        headline(k, dl, *result, l);
        review_chip(k, review, l);
        if (!l.counts.empty()) {
            counts_row(k, dl, *result, l);
        }
        if (!l.bars.empty()) {
            const CardChoice picked = draw_bars(k, dl, *result, l);
            if (picked.action != CardAction::None) {
                choice = picked;
            }
        }
        const CardChoice pressed = draw_buttons(k, *result, l);
        if (pressed.action != CardAction::None) {
            choice = pressed;
        }
        shown_ = true;
    }
    ImGui::End();
    ImGui::PopStyleVar();
    return choice;
}

namespace {

// Six dots in two columns: the grip that says a row can be dragged.
void draw_grip(const Kit& k, ImDrawList* dl, ImVec2 centre, ImVec4 ink) {
    const float r = k.s(card_tok::kGripDot);
    const float dx = k.s(card_tok::kGripGapX);
    const float dy = k.s(card_tok::kGripGapY);
    for (int column = -1; column <= 1; column += 2) {
        for (int row = -1; row <= 1; ++row) {
            dl->AddCircleFilled({centre.x + static_cast<float>(column) * dx * 0.5f,
                                 centre.y + static_cast<float>(row) * dy},
                                r, u32(ink), 8);
        }
    }
}

} // namespace

CardChoice ResultsCardView::draw_order(Kit& k, const ResultsCard& result, const RectF& area) {
    CardChoice choice;
    const OrderLayout l = order_layout(area, k.u, order_.count);
    if (l.card.empty()) {
        ordering_ = false;
        return choice;
    }
    scale_ = l.scale;
    const ScaledKit scaled(k, l.scale);
    shown_ = true;
    const int key = std::exchange(key_, 0);
    bool start = key > 0;
    bool back = key < 0;
    if (begin_layer("##results-order", to_vec(l.card.x, l.card.y), to_vec(l.card.w, l.card.h))) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        ui::overlay_card(k, dl, to_vec(l.card.x, l.card.y), to_vec(l.card.right(), l.card.bottom()),
                         k.s(tok::kRadiusLg));

        // The head: what this is and how to change it, then Back and Start.
        {
            const float s1 = k.s(tok::kLabel);
            const float s2 = k.s(tok::kCaption);
            const FitText h1 = fit(k.bold, s1, "Order of the bars", l.caption.w);
            const FitText h2 = fit(k.regular, s2, "Drag a bar, or use the arrows", l.caption.w);
            const StackLine top{k.bold, s1, col::kText, h1.c_str()};
            const StackLine bottom{k.regular, s2, col::kText2, h2.c_str()};
            label_stack(k, dl, {l.caption.x, l.caption.y, l.caption.right(), l.caption.bottom()},
                        top, &bottom);
        }
        back |= card_button(k, "##order-back", {icon::kBack}, "Back", l.back, false);
        start |= card_button(k, "##order-start", {icon::kPlay}, "Start", l.start, true);

        // The rows, in the order they will play. Each row's id is its bar's, so a held row keeps
        // its touch while the rows move round it.
        const ReorderDrag::Rows rows{l.rows_top,    l.pitch,    order_.count, l.columns,
                                     l.column_left, l.column_w, l.column_gap};
        const float slop = k.s(kTouchSlopPt);
        const ImVec2 mouse = ImGui::GetIO().MousePos;
        int move_up = -1; // a button was tapped on this place
        int move_down = -1;
        for (int place = 0; place < order_.count; ++place) {
            const auto j = static_cast<std::size_t>(place);
            const int bar_index = order_.slot[j];
            const CardBar& bar = result.bars[static_cast<std::size_t>(bar_index)];
            const RectF& grab = l.grabs[j];
            char id[24];
            std::snprintf(id, sizeof(id), "##order-bar%d", bar_index);
            ImGui::SetCursorScreenPos(to_vec(grab.x, grab.y));
            ImGui::InvisibleButton(id, to_vec(grab.w, grab.h));
            const bool held = ImGui::IsItemActive();
            if (ImGui::IsItemActivated()) {
                drag_.press(place, mouse.x, mouse.y);
            }
            const State st = item_state(k, held && drag_.dragging());
            const float face_h = std::min(grab.h, k.s(tok::kFace));
            const Box face{grab.x, grab.y + (grab.h - face_h) * 0.5f, grab.right(),
                           grab.y + (grab.h + face_h) * 0.5f};
            draw_state(k, dl, id, face, {grab.x, grab.y, grab.right(), grab.bottom()}, st,
                       Idle::Fill);

            const float cy = (face.y0 + face.y1) * 0.5f;
            const float pad = k.s(tok::kS3);
            draw_grip(k, dl, to_vec(face.x0 + pad + k.s(card_tok::kGripInset), cy),
                      state_ink(st, col::kText3));
            // The place it will be played in, then the bar and how it went.
            FitText number;
            number.format("%d", place + 1);
            const float ns = k.s(tok::kLabel);
            const ImVec2 nz = text_size(k.bold, ns, number.c_str());
            const float nx = face.x0 + pad + k.s(card_tok::kNumberInset);
            label_at(k, dl, k.bold, ns, {nx, face.y0, nx + nz.x, face.y1}, LabelAlign::Left,
                     state_ink(st, col::kAccent), number.c_str());
            const float tx = nx + k.s(card_tok::kNumberWidth) + k.s(tok::kS3);
            const float avail = face.x1 - k.s(tok::kS2) - tx;
            FitText name;
            name.format("Bar %d", bar.written);
            FitText detail;
            bar_detail(bar, detail);
            const float s1 = k.s(tok::kLabel);
            const float s2 = k.s(tok::kCaption);
            const FitText n = fit(k.bold, s1, name, avail);
            const FitText d = fit(k.regular, s2, detail, avail);
            const StackLine top{k.bold, s1, state_ink(st, col::kText), n.c_str()};
            const StackLine bottom{k.regular, s2, col::kText2, d.c_str()};
            label_stack(k, dl, {tx, face.y0, face.x1, face.y1}, top, &bottom);

            // The move buttons: the arrows keep touch exact where a drag would be fiddly.
            const RectF& up = l.ups[j];
            const RectF& down = l.downs[j];
            const float drawn = k.s(tok::kFace);
            char up_id[24];
            char down_id[24];
            std::snprintf(up_id, sizeof(up_id), "##order-up%d", bar_index);
            std::snprintf(down_id, sizeof(down_id), "##order-down%d", bar_index);
            ImGui::BeginDisabled(place == 0);
            if (icon_toggle(k, up_id, icon::kArrowUp, "Earlier",
                            to_vec(up.x + up.w * 0.5f, up.y + up.h * 0.5f), to_vec(up.w, up.h),
                            drawn, false, true) &&
                place > 0) {
                move_up = place;
            }
            ImGui::EndDisabled();
            ImGui::BeginDisabled(place + 1 >= order_.count);
            if (icon_toggle(k, down_id, icon::kArrowDown, "Later",
                            to_vec(down.x + down.w * 0.5f, down.y + down.h * 0.5f),
                            to_vec(down.w, down.h), drawn, false, true) &&
                place + 1 < order_.count) {
                move_down = place;
            }
            ImGui::EndDisabled();
        }
        // A held row follows the pointer, place by place.
        if (drag_.active()) {
            if (ImGui::IsMouseDown(0)) {
                const int from = drag_.place();
                if (const std::optional<int> to = drag_.move(mouse.x, mouse.y, rows, slop)) {
                    order_move(order_, from, *to);
                }
            } else {
                drag_.release();
            }
        }
        if (move_up >= 0) {
            order_up(order_, move_up);
        } else if (move_down >= 0) {
            order_down(order_, move_down);
        }
    }
    ImGui::End();
    if (back) {
        ordering_ = false;
        drag_ = {};
    } else if (start) {
        choice.action = CardAction::OneByOne;
        choice.hands = HandFilter::Both;
        choice.bars = 0;
        for (int i = 0; i < order_.count; ++i) {
            choice.bars |= 1u << order_.slot[static_cast<std::size_t>(i)];
            choice.order[static_cast<std::size_t>(i)] = order_.slot[static_cast<std::size_t>(i)];
        }
        choice.order_count = order_.count;
        ordering_ = false;
        drag_ = {};
    }
    return choice;
}

} // namespace etude::app::ui
