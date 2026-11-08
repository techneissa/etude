// SPDX-License-Identifier: GPL-3.0-or-later
#include "summary_page.hpp"

#include "bar.hpp" // begin_layer
#include "card_kit.hpp"
#include "fit_text.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "summary_board.hpp"

#include "etude/accidental_text.hpp"
#include "etude/pitch.hpp"
#include "etude/unit_format.hpp"

#include <imgui_internal.h> // the window order

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <span>

namespace etude::app::ui {

namespace {

constexpr const char* kDot = "\xC2\xB7";

// The ruler spans this many milliseconds either side of the beat: a press further off is drawn at
// the end.
constexpr double kRulerRangeMs = 160.0;

float ruler_x(const RectF& r, double ms) {
    const double clamped = std::clamp(ms, -kRulerRangeMs, kRulerRangeMs);
    return r.x + r.w * static_cast<float>((clamped + kRulerRangeMs) / (2.0 * kRulerRangeMs));
}

const char* lean_word(Tendency t) {
    switch (t) {
    case Tendency::Early:
        return "Leans early";
    case Tendency::Late:
        return "Leans late";
    case Tendency::Steady:
        break;
    }
    return "Steady";
}

// "12ms early" / "9ms late" / "on the beat": a signed error as words.
void error_words(int ms, FitText* out) {
    if (ms < 0) {
        out->format("%s early", format_unit(-ms, Unit::Milliseconds).c_str());
    } else if (ms > 0) {
        out->format("%s late", format_unit(ms, Unit::Milliseconds).c_str());
    } else {
        out->assign("on the beat");
    }
}

// The timing: a ruler of the beat in the middle, the middle half of the presses as a band round the
// median, a mark at the median, the early and late counts under its two ends.
void timing_part(Kit& k, ImDrawList* dl, const RunSummary& s, const SummaryLayout& l) {
    const float label_size = k.s(tok::kLabel);
    const float caption = k.s(tok::kCaption);
    const RectF& lr = l.timing_label;
    const Box slot{lr.x, lr.y, lr.right(), lr.bottom()};
    label_at(k, dl, k.bold, label_size, slot, LabelAlign::Left, col::kText, "Timing");
    if (!s.timing.valid()) {
        label_at(k, dl, k.regular, caption, slot, LabelAlign::Right, col::kText2,
                 "Wait Mode has no timing");
        return;
    }
    FitText words;
    if (s.timing.tendency == Tendency::Steady) {
        words.assign("Steady");
    } else {
        FitText median;
        error_words(s.timing.median_ms, &median);
        words.format("%s %s %s", lean_word(s.timing.tendency), kDot, median.c_str());
    }
    const FitText shown = fit(k.regular, caption, words, lr.w * 0.7f);
    label_at(k, dl, k.regular, caption, slot, LabelAlign::Right, col::kText2, shown.c_str());
    if (l.ruler.empty()) {
        return;
    }
    const RectF& r = l.ruler;
    const float track_h = k.s(tok::kS2);
    const float cy = r.y + (r.h - 2.0f * caption) * 0.5f;
    const float track_y0 = cy - track_h * 0.5f;
    const float track_y1 = cy + track_h * 0.5f;
    const float radius = k.s(tok::kS1);
    dl->AddRectFilled({r.x, track_y0}, {r.right(), track_y1}, u32(col::kFill), radius);
    // The middle half of the presses, centred on the median.
    const double half = static_cast<double>(s.timing.spread_ms) * 0.5;
    const float band0 = ruler_x(r, s.timing.median_ms - half);
    const float band1 = std::max(ruler_x(r, s.timing.median_ms + half), band0 + k.s(tok::kS1));
    const GradeInk lean = grade_ink(k, s.timing.tendency == Tendency::Early  ? Grade::Early
                                       : s.timing.tendency == Tendency::Late ? Grade::Late
                                                                             : Grade::Perfect);
    dl->AddRectFilled({band0, track_y0}, {band1, track_y1}, u32(lean.top), radius);
    // The beat, and the median on it.
    const float beat = ruler_x(r, 0.0);
    dl->AddLine({beat, track_y0 - k.s(tok::kS1)}, {beat, track_y1 + k.s(tok::kS1)}, u32(col::kText),
                k.s(tok::kHairline));
    const float med = ruler_x(r, s.timing.median_ms);
    dl->AddCircleFilled({med, cy}, k.s(tok::kS1) * 1.5f, u32(col::kText));
    // Early at the left end, Late at the right, each with its count.
    const float words_y = track_y1 + k.s(tok::kS1);
    FitText early;
    early.format("Early %d", s.timing.early);
    FitText late;
    late.format("Late %d", s.timing.late);
    const float line_h = std::max(0.0f, r.bottom() - words_y);
    label_at(k, dl, k.regular, caption, {r.x, words_y, r.x + r.w * 0.5f, words_y + line_h},
             LabelAlign::Left, grade_ink(k, Grade::Early).top, early.c_str());
    label_at(k, dl, k.regular, caption, {r.x + r.w * 0.5f, words_y, r.right(), words_y + line_h},
             LabelAlign::Right, grade_ink(k, Grade::Late).top, late.c_str());
}

// One hand's line: the hand's mark in its colour, how it went, how its timing leaned.
void hand_row(Kit& k, ImDrawList* dl, const RunSummary& s, Hand hand, const RectF& row) {
    const SummaryHand& h = s.hand(hand);
    const bool left = hand == Hand::Left;
    const ImVec4 ink = left ? k.hand_left : k.hand_right;
    const float badge = k.s(summary_tok::kBadge);
    glyph(k, dl, left ? icon::kLeftHand : icon::kRightHand, {row.x + badge * 0.5f, row.y + row.h * 0.5f},
          badge, ink);
    const float tx = row.x + badge + k.s(tok::kS2);
    const float size = k.s(tok::kLabel);
    const Box slot{tx, row.y, row.right(), row.bottom()};
    if (!h.practised || h.accuracy_pct < 0) {
        label_at(k, dl, k.regular, size, slot, LabelAlign::Left, col::kText3, "Not played");
        return;
    }
    FitText line;
    line.format("%s", format_unit(h.accuracy_pct, Unit::Percent).c_str());
    const float aw = text_size(k.bold, size, line.c_str()).x;
    Grade tier = Grade::Perfect;
    if (accuracy_grade(h.accuracy_pct, &tier)) {
        text_gradient(k, dl, k.bold, size, label_pen(k.bold, size, slot, LabelAlign::Left, line.c_str()),
                      grade_ink(k, tier), line.c_str());
    } else {
        label_at(k, dl, k.bold, size, slot, LabelAlign::Left, col::kText, line.c_str());
    }
    FitText rest;
    rest.format("%d missed", h.missed);
    if (h.timing_samples > 0) {
        FitText err;
        error_words(h.median_ms, &err);
        char tail[48];
        std::snprintf(tail, sizeof(tail), " %s %s", kDot, err.c_str());
        rest.append(tail);
    }
    if (s.weaker_hand == hand) {
        rest.append(" \xC2\xB7 weaker");
    }
    const float rx = tx + aw + k.s(tok::kS2);
    const FitText shown = fit(k.regular, k.s(tok::kCaption), rest, std::max(0.0f, row.right() - rx));
    label_at(k, dl, k.regular, k.s(tok::kCaption), {rx, row.y, row.right(), row.bottom()},
             LabelAlign::Left, col::kText2, shown.c_str());
}

// What went wrong in a bar: "4 missed", "2 wrong: C4 for D4".
void bar_detail(const RunSummary& s, const CardBar& bar, FitText* out) {
    if (bar.suggested) {
        out->assign("Hard, and played clean");
        return;
    }
    out->format("%d missed", bar.missed);
    if (bar.wrong > 0) {
        const auto spot = std::find_if(s.wrong_spots.begin(), s.wrong_spots.end(),
                                       [&](const WrongSpot& w) { return w.measure == bar.measure; });
        char tail[64];
        if (spot != s.wrong_spots.end() && spot->expected_key >= 0) {
            const std::string key = accidental_display(note_name(spot->key));
            const std::string meant = accidental_display(note_name(spot->expected_key));
            std::snprintf(tail, sizeof(tail), " %s %d wrong: %s for %s", kDot, bar.wrong,
                          key.c_str(), meant.c_str());
        } else {
            std::snprintf(tail, sizeof(tail), " %s %d wrong", kDot, bar.wrong);
        }
        out->append(tail);
    }
}

// A trouble bar's slim row: its name over what went wrong, then a watch and a practise chip.
// Returns the tapped chip: 1 watch, 2 practise.
int bar_row(Kit& k, ImDrawList* dl, const RunSummary& s, int index, const SummaryBarCell& cell,
            bool watchable) {
    const CardBar& bar = s.bars[static_cast<std::size_t>(index)];
    figure_row_face(k, dl, cell.face);
    const float s1 = k.s(tok::kLabel);
    const float s2 = k.s(tok::kCaption);
    FitText name;
    name.format("Bar %d", bar.written);
    FitText detail;
    bar_detail(s, bar, &detail);
    const float avail = cell.text.w;
    const FitText n = fit(k.bold, s1, name, avail);
    const FitText d = fit(k.regular, s2, detail, avail);
    const StackLine top{k.bold, s1, col::kText, n.c_str()};
    const StackLine bottom{k.regular, s2, col::kText2, d.c_str()};
    label_stack(k, dl, {cell.text.x, cell.face.y, cell.text.right(), cell.face.bottom()}, top,
                &bottom);
    int tapped = 0;
    const float drawn = k.s(tok::kFace);
    char id[32];
    std::snprintf(id, sizeof(id), "##review-watch%d", index);
    ImGui::BeginDisabled(!watchable);
    if (icon_toggle(k, id, icon::kPlay, "Watch this bar",
                    to_vec(cell.watch.x + cell.watch.w * 0.5f, cell.watch.y + cell.watch.h * 0.5f),
                    to_vec(cell.watch.w, cell.watch.h), drawn, false, true)) {
        tapped = 1;
    }
    ImGui::EndDisabled();
    std::snprintf(id, sizeof(id), "##review-practise%d", index);
    if (icon_toggle(k, id, icon::kPractice, "Practise this bar",
                    to_vec(cell.practise.x + cell.practise.w * 0.5f,
                           cell.practise.y + cell.practise.h * 0.5f),
                    to_vec(cell.practise.w, cell.practise.h), drawn, false, true)) {
        tapped = 2;
    }
    return tapped;
}

// The header: Back, what this is and what the run was, the pin, Done. Returns true when Done was
// tapped; `back` is set when Back was and `pinned_tap` when the pin was.
bool header(Kit& k, ImDrawList* dl, const RunSummary* s, const SummaryLayout& l, const PinInfo& pin,
            bool* back, bool* pinned_tap, int* tab, SegmentFit* tab_fit) {
    const float drawn = k.s(tok::kFace);
    if (icon_toggle(k, "##review-back", icon::kBack, "Back",
                    to_vec(l.back.x + l.back.w * 0.5f, l.back.y + l.back.h * 0.5f),
                    to_vec(l.back.w, l.back.h), drawn, false, true)) {
        *back = true;
    }
    if (pin_chip(k, "##review-pin", pin, to_vec(l.pin.x + l.pin.w * 0.5f, l.pin.y + l.pin.h * 0.5f),
                 to_vec(l.pin.w, l.pin.h))) {
        *pinned_tap = true;
    }
    static constexpr std::array<const char*, 3> kTabs{"Timing", "Board", "Runs"};
    const auto count = static_cast<std::size_t>(l.tab_count);
    segment_cells(k, "##review-tab", std::span<const RectF>(l.tabs.data(), count),
                  std::span<const char* const>(kTabs.data(), count), tab, tok::kHitGap * l.scale,
                  tab_fit);
    const bool done =
        icon_toggle(k, "##review-done", icon::kCheck, "Done",
                    to_vec(l.done.x + l.done.w * 0.5f, l.done.y + l.done.h * 0.5f),
                    to_vec(l.done.w, l.done.h), drawn, false, true);
    const float s1 = k.s(tok::kBody);
    const float s2 = k.s(tok::kLabel);
    FitText second;
    if (s == nullptr) {
        second.assign("Working it out");
    } else if (s->detail == SummaryDetail::Nothing) {
        second.assign("Nothing was graded");
    } else {
        second.format("%d hit %s %d missed %s %d wrong", s->hit, kDot, s->missed, kDot, s->wrong);
    }
    const FitText a = fit(k.bold, s1, "How it went", l.title.w);
    const FitText b = fit(k.regular, s2, second, l.title.w);
    const StackLine top{k.bold, s1, col::kText, a.c_str()};
    const StackLine bottom{k.regular, s2, col::kText2, b.c_str()};
    label_stack(k, dl, {l.title.x, l.title.y, l.title.right(), l.title.bottom()}, top, &bottom);
    return done;
}

// The timing, the hands and the worst bars. A bar's chip, when tapped, is the choice.
ReviewChoice body(Kit& k, ImDrawList* dl, const RunSummary& s, const SummaryLayout& l,
                  bool watchable) {
    ReviewChoice choice;
    timing_part(k, dl, s, l);
    if (l.hands) {
        hand_row(k, dl, s, Hand::Left, l.hand_rows[0]);
        hand_row(k, dl, s, Hand::Right, l.hand_rows[1]);
    }
    if (l.bar_count == 0) {
        return choice;
    }
    if (!l.bars_label.empty()) {
        label_at(k, dl, k.bold, k.s(tok::kLabel),
                 {l.bars_label.x, l.bars_label.y, l.bars_label.right(), l.bars_label.bottom()},
                 LabelAlign::Left, col::kText, s.bars[0].suggested ? "Hardest bars" : "Trouble bars");
    }
    for (int i = 0; i < l.bar_count; ++i) {
        const int tapped = bar_row(k, dl, s, i, l.bars[static_cast<std::size_t>(i)], watchable);
        const int measure = s.bars[static_cast<std::size_t>(i)].measure;
        if (tapped == 1) {
            choice = {ReviewAction::Watch, measure};
        } else if (tapped == 2) {
            choice = {ReviewAction::Practise, measure};
        }
    }
    return choice;
}

// The two actions at the foot, as chips: Watch replay and Practise these bars.
ReviewChoice actions(Kit& k, const SummaryLayout& l, bool watch_live, bool practise_live) {
    ReviewChoice choice;
    ChipOpts watch_opts;
    watch_opts.icon = icon::kPlay;
    watch_opts.live = watch_live;
    const float watch_w = chip_width(k, icon::kPlay, "Watch replay");
    if (chip(k, "##review-watch", "Watch replay", to_vec(l.actions.x, l.actions.y), watch_opts)) {
        choice = {ReviewAction::Watch, 0};
    }
    ChipOpts practise_opts;
    practise_opts.icon = icon::kPractice;
    practise_opts.live = practise_live;
    if (chip(k, "##review-practise", "Practise these bars",
             to_vec(l.actions.x + watch_w + k.s(tok::kS2), l.actions.y), practise_opts)) {
        choice = {ReviewAction::Practise, 0};
    }
    return choice;
}

// What the Board tab's layout needs: a line for every profile (the boards say how many), the chart
// when the player has runs of the song to chart.
BoardContent board_content(const BoardInfo& info, int tab) {
    BoardContent b;
    b.on = tab == 1;
    b.runs = tab == 2;
    if (info.profiles != nullptr) {
        b.rows = static_cast<int>(info.profiles->size());
    }
    b.chart = false;
    for (const SongBoard* board : info.periods) {
        if (board != nullptr) {
            b.rows = static_cast<int>(board->rows.size());
            b.chart = !board->chart.runs.empty();
            break;
        }
    }
    return b;
}

} // namespace

// The body under the header: the Runs tab, the Board tab or the timing, as the layout placed it.
ReviewChoice SummaryPageView::draw_body(Kit& k, ImDrawList* dl, const ReviewInfo& review,
                                        const SummaryLayout& l) {
    const RunSummary* s = review.summary;
    if (l.runs.shown) {
        if (draw_runs_tab(k, dl, review.board, l, period_, &chart_)) {
            return {ReviewAction::ChartView, 0};
        }
    } else if (l.board.shown) {
        if (draw_board_tab(k, dl, review.board, l, &period_, &chart_, &period_fit_)) {
            return {ReviewAction::ChartView, 0};
        }
    } else if (s != nullptr && s->detail != SummaryDetail::Nothing) {
        return body(k, dl, *s, l, review.watchable);
    }
    return {};
}

ReviewChoice SummaryPageView::draw(Kit& k, const ReviewInfo& review, const RectF& area,
                                   float card_scale) {
    ReviewChoice choice;
    shown_ = false;
    back_ = false;
    const RunSummary* s = review.summary;
    SummaryContent content;
    content.timing = false;
    content.hands = false;
    if (s != nullptr) {
        content.bars = s->bar_count;
        content.timing = s->timing.valid() || s->detail == SummaryDetail::Played;
        content.hands = s->hand(Hand::Left).practised && s->hand(Hand::Right).practised;
    }
    const SummaryLayout l =
        summary_layout(area, k.u, content, board_content(review.board, tab_), card_scale);
    if (l.card.empty()) {
        return choice;
    }
    if (!l.runs_tab && tab_ == 2) {
        tab_ = 1; // the Runs tab is gone (the window or the runs changed): the layout drew the Board
    }
    scale_ = l.scale;
    const ScaledKit scaled(k, l.scale);
    progress_ = approach(progress_, 1.0f, k.dt, tok::kMove);
    const float shown_p = ease_out(progress_);
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, shown_p);
    if (begin_layer("##review", to_vec(l.card.x, l.card.y), to_vec(l.card.w, l.card.h))) {
        // The band hanging from the revealed bar draws over the page and takes the touches in its
        // own area, whatever was touched last: the page stays behind it.
        ImGuiWindow* page = ImGui::GetCurrentWindow();
        page->Flags |= ImGuiWindowFlags_NoBringToFrontOnFocus;
        ImGuiWindow* band = ImGui::FindWindowByName("##band");
        const bool band_live = band != nullptr && (band->Active || band->WasActive);
        if (band_live && lower_behind_band(ImGui::FindWindowDisplayIndex(page),
                                           ImGui::FindWindowDisplayIndex(band))) {
            ImGui::BringWindowToDisplayBehind(page, band);
        }
        // What the band covers takes no touch of the page's and is not the audit's; the page does not
        // move for the band, so the frames without it audit those parts.
        if (band_live) {
            audit::set_overlay(band->Pos, {band->Pos.x + band->Size.x, band->Pos.y + band->Size.y});
        }
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float grow = card_tok::kEnterScale + (1.0f - card_tok::kEnterScale) * shown_p;
        const ImVec2 centre = to_vec(l.card.x + l.card.w * 0.5f, l.card.y + l.card.h * 0.5f);
        const ImVec2 half = to_vec(l.card.w * 0.5f * grow, l.card.h * 0.5f * grow);
        ui::overlay_card(k, dl, to_vec(centre.x - half.x, centre.y - half.y),
                         to_vec(centre.x + half.x, centre.y + half.y), k.s(tok::kRadiusLg));
        bool pin_tapped = false;
        if (header(k, dl, s, l, review.pin, &back_, &pin_tapped, &tab_, &tab_fit_)) {
            choice = {ReviewAction::Done, 0};
        } else if (pin_tapped) {
            choice = {ReviewAction::Pin, 0};
        }
        const ReviewChoice tapped = draw_body(k, dl, review, l);
        if (tapped.action != ReviewAction::None) {
            choice = tapped;
        }
        const ReviewChoice act =
            actions(k, l, review.watchable && s != nullptr, s != nullptr && s->bar_count > 0);
        if (act.action != ReviewAction::None) {
            choice = act;
        }
        shown_ = true;
    }
    ImGui::End();
    audit::clear_overlay();
    ImGui::PopStyleVar();
    return choice;
}

} // namespace etude::app::ui
