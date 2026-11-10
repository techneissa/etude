// SPDX-License-Identifier: GPL-3.0-or-later
// The practice list popover and a section's menu of the section band
// (section_band.hpp).
#include "section_band.hpp"

#include "bar.hpp"
#include "icons.hpp"
#include "plan_list_layout.hpp"
#include "sections_ui.hpp"

#include "etude/editor_layout.hpp"
#include "etude/level_stars.hpp"
#include "etude/roll_view.hpp"
#include "etude/section_overview.hpp"
#include "etude/transport.hpp"

#include <algorithm>
#include <cfloat>
#include <cmath>
#include <cstdio>

namespace etude::app::ui {
namespace {

std::size_t index_of(const SectionsView& v, SectionId id) {
    for (std::size_t i = 0; i < v.sections.size(); ++i) {
        if (v.sections[i].id == id) {
            return i;
        }
    }
    return v.sections.size();
}

// A wrapped paragraph in `size`, `colour`, `max_w` wide; returns its height.
float paragraph(Kit& k, ImDrawList* dl, ImVec2 pos, float max_w, float size, ImVec4 colour,
                std::string_view text) {
    text = screen_text(text);
    const char* end = text.data() + text.size();
    const ImVec2 ts = k.regular->CalcTextSizeA(size, FLT_MAX, max_w, text.data(), end);
    dl->AddText(k.regular, size, {std::round(pos.x), std::round(pos.y)}, u32(colour), text.data(),
                end, max_w);
    return ts.y;
}

// Two lines of text in a row `h` tall from `x`: `top` in the label size, `bottom` in the caption
// size under it.
void two_lines(Kit& k, ImDrawList* dl, float x, float y, float h, float max_w, std::string_view top,
               bool bold_top, std::string_view bottom) {
    const float s1 = k.s(tok::kLabel);
    const float s2 = k.s(tok::kCaption);
    ImFont* top_font = bold_top ? k.bold : k.regular;
    const FitText t = fit(top_font, s1, top, max_w);
    const FitText b = fit(k.regular, s2, bottom, max_w);
    const StackLine top_line{top_font, s1, col::kText, t.c_str()};
    const StackLine bottom_line{k.regular, s2, col::kText3, b.c_str()};
    label_stack(k, dl, {x, y, x + max_w, y + h}, top_line, &bottom_line);
}

// A list row: the whole row is its hit and its face; `on` draws it selected. Returns true on a tap.
bool list_row(Kit& k, const char* id, ImVec2 pos, float w, float h, bool on) {
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, {std::max(1.0f, w), h});
    const State st = item_state(k, on);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const Box face{pos.x, pos.y + k.s(tok::kInset), pos.x + w, pos.y + h - k.s(tok::kInset)};
    draw_state(k, ImGui::GetWindowDrawList(), id, face, row, st);
    return pressed;
}

// The taps on a row of two buttons side by side.
struct Pair {
    bool a = false;
    bool b = false;
};

// A popover card's window at `pos`, `size`: the card drawn behind its body; the toast keeps off it.
bool begin_card(Kit& k, const char* id, ImVec2 pos, ImVec2 size) {
    k.toast_avoid({pos.x, pos.y, size.x, size.y});
    if (!begin_layer(id, pos, size)) {
        return false;
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    overlay_card(k, dl, pos, {pos.x + size.x, pos.y + size.y}, k.s(tok::kRadiusCard),
                 col::kSurface);
    return true;
}

// A tap that began outside the card and outside the band, released without movement: the popover
// closes.
bool tapped_away(ImVec2 min, ImVec2 max, const RectF& band) {
    if (!tapped_away_from(min, max)) {
        return false;
    }
    const ImVec2 m = tap_start();
    return !(m.x >= band.x && m.x <= band.right() && m.y >= band.y && m.y <= band.bottom());
}


} // namespace

namespace {

// The title and how many items there are.
void plan_title(Kit& k, const SectionsView& v, float inner) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 p = ImGui::GetCursorScreenPos();
    const float size = k.s(tok::kLabel);
    text_at(k, dl, k.bold, size, {p.x, p.y}, col::kText, "Practice list");
    FitText count;
    count.format(v.plan.size() == 1 ? "%zu section" : "%zu sections", v.plan.size());
    const ImVec2 cs = text_size(k.regular, k.s(tok::kCaption), count.c_str());
    text_at(k, dl, k.regular, k.s(tok::kCaption), {p.x + inner - cs.x, p.y}, col::kText3,
            count.c_str());
    ImGui::Dummy({inner, text_size(k.bold, size, "P").y + k.s(tok::kS2)});
}

// The items, a row each, a hit tall (the item spacing keeps the dead gap). `focus` is the one
// being edited.
void plan_rows(Kit& k, const SectionsView& v, float inner, std::size_t& focus) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hit = k.s(tok::kHit);
    if (v.plan.empty()) {
        const ImVec2 p = ImGui::GetCursorScreenPos();
        const float ph = paragraph(k, dl, p, inner, k.s(tok::kLabel), col::kText2,
                                   "The list is empty. Hold a section on the band and add it, "
                                   "or add them all below.");
        ImGui::Dummy({inner, ph + k.s(tok::kS2)});
    }
    for (std::size_t i = 0; i < v.plan.size(); ++i) {
        const PlanRow& r = v.plan[i];
        const ImVec2 pos = ImGui::GetCursorScreenPos();
        ImGui::PushID(static_cast<int>(i));
        const bool item_running = v.plan_running && v.plan_item == static_cast<int>(i);
        if (list_row(k, "##plan-row", pos, inner, hit, i == focus)) {
            focus = i;
        }
        FitText title;
        title.format("%zu  %s", i + 1, r.name.c_str());
        const float text_x = pos.x + k.s(tok::kS3);
        two_lines(k, dl, text_x, pos.y, hit, inner - k.s(tok::kS3) - (item_running ? hit : 0.0f),
                  title, item_running, r.detail);
        if (item_running) {
            glyph(k, dl, icon::kPlay, {pos.x + inner - hit * 0.5f, pos.y + hit * 0.5f},
                  k.s(tok::kIconSm), col::kAccent);
        }
        ImGui::PopID();
        ImGui::SetCursorScreenPos({pos.x, pos.y + hit});
        ImGui::Dummy({inner, 0.0f});
    }
}

// Start or Stop, then skip back and forward: the card's footer, outside the scrolling list.
void plan_footer(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float skip_w = hit;
    const float main_w = inner - 2.0f * skip_w - 2.0f * gap;
    const ImVec2 at{pos.x, pos.y + gap};
    if (v.plan_running) {
        if (action_button(k, "##plan-stop", icon::kStop, v.run_active ? "End" : "Stop", at,
                          {main_w, hit}, true, false, true)) {
            sp.plan_stop(k.now_ns);
        }
    } else if (action_button(k, "##plan-start", icon::kPlay, "Start", at, {main_w, hit},
                             !v.plan.empty(), true)) {
        sp.plan_start(k.now_ns);
        k.say(sp.take_message());
    }
    ImGui::BeginDisabled(!v.plan_running);
    if (icon_toggle(k, "##plan-prev", icon::kPrevious, "Previous section",
                    {at.x + main_w + gap + skip_w * 0.5f, at.y + hit * 0.5f}, {skip_w, hit},
                    k.s(tok::kFace), false, true) &&
        v.plan_running) {
        sp.plan_skip(-1, k.now_ns);
    }
    ImGui::EndDisabled();
    ImGui::BeginDisabled(!v.plan_running || (v.run_active && !v.run_can_skip));
    if (icon_toggle(k, "##plan-next", icon::kSkipNext, "Next section",
                    {at.x + main_w + 2.0f * gap + skip_w * 1.5f, at.y + hit * 0.5f}, {skip_w, hit},
                    k.s(tok::kFace), false, true) &&
        v.plan_running) {
        sp.plan_skip(1, k.now_ns);
    }
    ImGui::EndDisabled();
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// Add every section, or clear the list.
void plan_list_buttons(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float half = (inner - gap) * 0.5f;
    const ImVec2 at{pos.x, pos.y + gap};
    if (action_button(k, "##plan-all", icon::kListPlus, "Add all", at, {half, hit},
                      !v.sections.empty())) {
        sp.plan_add_all();
    }
    if (action_button(k, "##plan-clear", icon::kTrash, "Clear", {at.x + half + gap, at.y},
                      {half, hit}, !v.plan.empty(), false, true, true, "Tap to clear")) {
        sp.plan_clear();
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// The item being edited: its passes and speed-up, and its place in the list.
void plan_item_editor(Kit& k, SectionsPort& sp, const SectionsView& v, float inner,
                      std::size_t& focus) {
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const PlanRow& row = v.plan[focus];
    PlanItem item = row.item;
    bool changed = false;
    FitText item_title;
    item_title.format("ITEM %zu \xC2\xB7 %s", focus + 1, row.name.c_str());
    section(k, item_title.c_str());
    changed |= stepper_row(k, icon::kRepeat, "Passes", &item.repeats, 0, kMaxPlanRepeats,
                           Unit::Times);
    changed |= switch_row(k, icon::kCheck, "Clean passes only", &item.clean_passes_only,
                          "A pass with a wrong or missed note does not count. With 0 "
                          "passes the section stays until you skip it.");
    changed |= switch_row(k, icon::kGauge, "Speed-up", &item.speed_up.enabled);
    if (item.speed_up.enabled) {
        changed |= stepper_row(k, nullptr, "Start", &item.speed_up.start_percent, 25,
                               kMaxSpeedPercent, Unit::Percent);
        changed |= stepper_row(k, nullptr, "Step", &item.speed_up.step_percent, 5, 25,
                               Unit::Percent);
        changed |= stepper_row(k, nullptr, "Up to", &item.speed_up.max_percent, 25,
                               kMaxSpeedPercent, Unit::Percent);
    }
    if (changed) {
        // The grid the transport takes: steps of 5, the top not under the start.
        item.speed_up.start_percent = (item.speed_up.start_percent / 5) * 5;
        item.speed_up.max_percent =
            std::max(item.speed_up.start_percent, (item.speed_up.max_percent / 5) * 5);
        sp.plan_set(focus, item);
    }
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float third = (inner - 2.0f * gap) / 3.0f;
    if (action_button(k, "##item-up", icon::kArrowUp, "Up", pos, {third, hit}, focus > 0)) {
        sp.plan_move(focus, focus - 1);
        --focus;
    }
    if (action_button(k, "##item-down", icon::kArrowDown, "Down", {pos.x + third + gap, pos.y},
                      {third, hit}, focus + 1 < v.plan.size())) {
        sp.plan_move(focus, focus + 1);
        ++focus;
    }
    if (action_button(k, "##item-remove", icon::kTrash, "Remove",
                      {pos.x + 2.0f * (third + gap), pos.y}, {third, hit}, true, false, true)) {
        sp.plan_remove(focus);
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, hit + gap});
}

// The hand path: right hand, then left, then both, for the looped section or the song.
void plan_hand_path(Kit& k, SectionsPort& sp, const SectionsView& v, float inner) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float cpad = k.s(tok::kS3);
    const float text_w = inner - 2.0f * cpad;
    const float body_size = k.s(tok::kLabel);
    const float body_h =
        k.regular->CalcTextSizeA(body_size, FLT_MAX, text_w, screen_text(v.hand_hint.c_str())).y;
    const float btn_h = v.hand_can_apply ? hit : 0.0f;
    const Disclosure fold = disclosure_tap(k, "##hand-path-fold", pos, {inner, hit}, false);
    const float card_h = fold.open ? hit + body_h + (btn_h > 0.0f ? gap + btn_h : 0.0f) + cpad : hit;
    // A plain face, not `card`: the list scrolls, and a card draws past the window's clip.
    dl->AddRectFilled(pos, {pos.x + inner, pos.y + card_h}, u32(col::kRaised),
                      k.s(tok::kRadiusControl));
    const float hand = k.s(tok::kIconSm);
    const float pair_w = hand_pair_width(hand);
    hand_pair(k, dl, {pos.x + cpad + pair_w * 0.5f, pos.y + hit * 0.5f}, hand, col::kText2);
    const float label_x = pos.x + cpad + pair_w + k.s(tok::kS2);
    FitText hand_path;
    hand_path.format("HAND PATH \xC2\xB7 %s", v.hand_scope.c_str());
    const float cap = k.s(tok::kCaption);
    const FitText label =
        fit(k.bold, cap, hand_path, disclosure_label_right(k, pos, {inner, hit}) - label_x);
    label_at(k, dl, k.bold, cap, {label_x, pos.y, pos.x + inner, pos.y + hit}, LabelAlign::Left,
             col::kText3, label.c_str());
    disclosure_chevron(k, fold, pos, {inner, hit});
    if (fold.open) {
        const float ty = pos.y + hit;
        paragraph(k, dl, {pos.x + cpad, ty}, text_w, body_size, col::kText, v.hand_hint);
        if (v.hand_can_apply) {
            FitText use;
            use.format("Use %s", v.hand_apply.c_str());
            if (action_button(k, "##hand-apply", icon::kCheck, use.c_str(),
                              {pos.x + cpad, ty + body_h + gap}, {text_w, btn_h}, true, true)) {
                sp.apply_hand_path(k.now_ns);
                k.say(sp.take_message());
            }
        }
    }
    ImGui::SetCursorScreenPos(pos);
    ImGui::Dummy({inner, card_h});
}

} // namespace

void SectionBand::list_popover(Kit& k, SectionsPort& sp, const BandPlace& place, float anchor_x) {
    const SectionsView& v = sp.view();
    const float margin = k.s(tok::kS3);
    const float w = std::min(place.display_w - 2.0f * margin, k.s(kBandListW));
    const float x = std::clamp(anchor_x - w * 0.5f, margin, place.display_w - w - margin);
    const float y = place.below + k.s(tok::kS2);
    const float max_h = place.keyboard_top - y - k.s(tok::kS2);
    const float pad = k.s(tok::kS3);
    // Closed where the card could not keep the footer and one whole row.
    if (max_h < plan_list_min_h(pad, k.s(tok::kS2), k.s(tok::kHit))) {
        list_open_ = false;
        return;
    }
    // The title, the list that scrolls, and the footer with Start: the footer is never inside the
    // scrolling part, so it stays whole whatever the list's length.
    const PlanListLayout lay =
        plan_list_layout({max_h, pad, text_size(k.bold, k.s(tok::kLabel), "P").y, k.s(tok::kS2),
                          k.s(tok::kHit), list_h_});
    if (plan_focus_ >= v.plan.size()) {
        plan_focus_ = v.plan.empty() ? 0 : v.plan.size() - 1;
    }
    if (v.plan_running && v.plan_item >= 0) {
        plan_focus_ = static_cast<std::size_t>(v.plan_item); // the editor follows the list
    }
    if (begin_card(k, "##band-list-card", {x, y}, {w, lay.h})) {
        ImGui::SetCursorScreenPos({x + pad, y + pad});
        ImGui::BeginChild("##band-list-body", {w - 2.0f * pad, lay.h - 2.0f * pad},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        const float inner = ImGui::GetContentRegionAvail().x;
        const float body_top = ImGui::GetCursorScreenPos().y;
        if (lay.title) {
            plan_title(k, v, inner);
        }
        // Placed by the layout's numbers, not by the cursor, which adds the item spacing.
        ImGui::SetCursorScreenPos({x + pad, body_top + lay.title_h});
        ImGui::BeginChild("##band-list-scroll", {inner, std::max(1.0f, lay.scroll_h)},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        const float top = ImGui::GetCursorScreenPos().y;
        plan_rows(k, v, inner, plan_focus_);
        plan_list_buttons(k, sp, v, inner);
        if (!v.plan.empty()) {
            plan_item_editor(k, sp, v, inner, plan_focus_);
        }
        plan_hand_path(k, sp, v, inner);
        list_h_ = ImGui::GetCursorScreenPos().y - top;
        drag_scroll(k);
        ImGui::EndChild();
        ImGui::SetCursorScreenPos({x + pad, body_top + lay.title_h + lay.scroll_h});
        plan_footer(k, sp, v, inner);
        ImGui::EndChild();
    }
    ImGui::End();
    if (opened_frame_ != ImGui::GetFrameCount() &&
        tapped_away({x, y}, {x + w, y + lay.h},
                    {0.0f, place.band.y, place.display_w, place.below - place.band.y})) {
        list_open_ = false;
    }
}

void SectionBand::menu_popover(Kit& k, SectionsPort& sp, const BandPlace& place) {
    const SectionsView& v = sp.view();
    const std::size_t index = index_of(v, menu_section_);
    if (index >= v.sections.size()) {
        menu_open_ = false; // the section went away
        return;
    }
    const SectionRow& r = v.sections[index];
    const float margin = k.s(tok::kS3);
    const float w = std::min(place.display_w - 2.0f * margin, k.s(kBandMenuW));
    const float x = std::clamp(menu_anchor_x_ - w * 0.5f, margin, place.display_w - w - margin);
    const float y = place.below + k.s(tok::kS2);
    const float max_h = place.keyboard_top - y - k.s(tok::kS2);
    const float pad = k.s(tok::kS3);
    const float h = std::min(max_h, menu_h_ > 0.0f ? menu_h_ + 2.0f * pad : max_h);
    const float hit = k.s(tok::kHit);
    const float gap = k.s(tok::kS2);
    bool done = false;
    if (begin_card(k, "##band-menu-card", {x, y}, {w, std::max(h, hit)})) {
        ImGui::SetCursorScreenPos({x + pad, y + pad});
        ImGui::BeginChild("##band-menu-body", {w - 2.0f * pad, std::max(h - 2.0f * pad, hit)},
                          ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar);
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float inner = ImGui::GetContentRegionAvail().x;
        const float top = ImGui::GetCursorScreenPos().y;
        // The section: its colour, name and where it is. Delete is the icon at the end of this
        // row, kDestructiveGap from every other action (the rows below start that far down), and
        // takes a second tap; the toast it leaves offers Undo.
        {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const float dot = k.s(tok::kS2);
            dl->AddCircleFilled({p.x + dot * 0.5f, p.y + hit * 0.5f}, dot * 0.5f,
                                u32(band_tint(index)), 12);
            two_lines(k, dl, p.x + dot + k.s(tok::kS2), p.y, hit, inner - dot - k.s(tok::kS2) - hit,
                      r.label, true, r.detail);
            ImGui::SetCursorScreenPos({p.x + inner - hit, p.y});
            const ImGuiID delete_id = ImGui::GetID("##menu-delete");
            const bool delete_armed = armed(k, delete_id);
            if (icon_action(k, "##menu-delete", icon::kTrash,
                            delete_armed ? "Tap again to delete" : "Delete", hit, true, true,
                            delete_armed)) {
                if (delete_armed) {
                    const std::uint32_t before = sp.edit_serial();
                    sp.erase_section(r.id, k.now_ns);
                    k.say_edit(before);
                    k.confirm_id = 0;
                    done = true;
                } else {
                    k.confirm_id = delete_id;
                    k.confirm_until = k.now + 3.0;
                    k.say("Tap again to delete");
                }
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, hit + std::max(k.s(kDestructiveGap) - gap, 0.0f)});
        }
        const float half = (inner - gap) * 0.5f;
        // A row of two actions, or one action across the card when `id_b` is null.
        const auto row = [&](const char* id_a, const char* icon_a, const char* label_a, bool en_a,
                             const char* id_b, const char* icon_b, const char* label_b, bool en_b) {
            const ImVec2 p = ImGui::GetCursorScreenPos();
            const ImVec2 at{p.x, p.y + gap};
            Pair out;
            const float a_w = id_b != nullptr ? half : inner;
            out.a = action_button(k, id_a, icon_a, label_a, at, {a_w, hit}, en_a);
            if (id_b != nullptr) {
                out.b = action_button(k, id_b, icon_b, label_b, {at.x + half + gap, at.y},
                                      {half, hit}, en_b);
            }
            ImGui::SetCursorScreenPos(p);
            ImGui::Dummy({inner, hit + gap});
            return out;
        };
        const bool has_next = index + 1 < v.sections.size();
        Pair p = row("##menu-rename", icon::kRename, "Rename", true, "##menu-join", icon::kMerge,
                     "Join next", has_next);
        if (p.a) {
            open_section_rename(r.id, r.custom_name ? r.label : "");
            done = true;
        }
        if (p.b) {
            const std::uint32_t before = sp.edit_serial();
            sp.merge_with_next(r.id, k.now_ns);
            k.say_edit(before);
            done = true;
        }
        p = row("##menu-list", icon::kListPlus, "Add to list", true, "##menu-lane", icon::kUnfold,
                "Edit cuts", true);
        if (p.a) {
            sp.plan_add(r.id);
            k.say("Added to the practice list");
            done = true;
        }
        if (p.b) {
            lane_ = true;
            done = true;
        }
        // Undo, with the loop's save beside it when there is a loop to save.
        if (v.loop_can_be_saved) {
            p = row("##menu-undo", icon::kUndo, "Undo", v.can_undo, "##menu-save-loop", icon::kSave,
                    "Save loop", true);
        } else {
            p = row("##menu-undo", icon::kUndo, "Undo", v.can_undo, nullptr, nullptr, nullptr,
                    false);
        }
        if (p.a) {
            sp.undo(k.now_ns);
            k.say(sp.take_message());
            done = true;
        }
        if (p.b) {
            sp.save_loop_as_section(k.now_ns);
            k.say(sp.take_message());
            done = true;
        }
        menu_h_ = ImGui::GetCursorScreenPos().y - top;
        drag_scroll(k);
        ImGui::EndChild();
    }
    ImGui::End();
    if (done || (opened_frame_ != ImGui::GetFrameCount() &&
                 tapped_away({x, y}, {x + w, y + h},
                             {0.0f, place.band.y, place.display_w, place.below - place.band.y}))) {
        menu_open_ = false;
    }
}

} // namespace etude::app::ui
