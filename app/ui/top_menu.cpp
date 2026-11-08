// SPDX-License-Identifier: GPL-3.0-or-later
#include "top_menu.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "profile_ui.hpp"
#include "tap.hpp"
#include "top_menu_layout.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace {

struct Row {
    TopMenuEntry entry;
    const char* id;
    const char* words;
    const char* glyph; // null: the profile's avatar
};

constexpr Row kRows[] = {
    {TopMenuEntry::Library, "##menu-library", "Library", icon::kLibrary},
    {TopMenuEntry::Learn, "##menu-learn", "Learn", icon::kLearn},
    {TopMenuEntry::Profile, "##menu-profile", "Profile", nullptr},
};

const Row& row_of(TopMenuEntry e) { return kRows[static_cast<std::size_t>(e)]; }

// One entry: its hit area is the whole row, its face the row less the inset.
bool draw_row(Kit& k, ImDrawList* dl, const Row& r, const Box& area, const Profile* who) {
    const bool pressed = press_area(k, r.id, area);
    const State st = item_state(k, false, true);
    const float inset = k.s(tok::kInset) * 0.5f;
    const Box face{area.x0, area.y0 + inset, area.x1, area.y1 - inset};
    draw_state(k, dl, r.id, face, area, st);
    audit::note(r.id, {area.x0, area.y0}, {area.x1, area.y1}, InkKind::Target);
    const float icon = k.s(tok::kIcon);
    const ImVec2 at{area.x0 + k.s(tok::kS3) + icon * 0.5f, area.cy()};
    if (r.glyph != nullptr) {
        glyph(k, dl, r.glyph, at, icon, state_ink(st, col::kText2));
    } else if (who != nullptr) {
        profile_avatar(k, dl, at, k.s(profile_tok::kChipAvatar), *who);
    }
    const float x = area.x0 + k.s(tok::kS3) + icon + k.s(tok::kS3);
    const FitText shown = fit(k.regular, k.s(tok::kBody), r.words, area.x1 - k.s(tok::kS3) - x);
    label_at(k, dl, k.regular, k.s(tok::kBody), {x, face.y0, area.x1 - k.s(tok::kS3), face.y1},
             LabelAlign::Left, state_ink(st, col::kText), shown.c_str());
    return tapped(pressed);
}

} // namespace

std::optional<TopMenuEntry> TopMenu::popover(Kit& k, const Frame& f, const TopMenuEntries& entries,
                                             const Box& button) {
    if (entries.count() == 0) {
        open_ = false;
    }
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (open_p_ <= 0.0f) {
        return std::nullopt;
    }
    const float t = ease_out(open_p_);
    const TopMenuPlan plan = entries.plan();
    float word_w = 0.0f;
    for (int i = 0; i < plan.count; ++i) {
        const Row& r = row_of(plan.entry[static_cast<std::size_t>(i)]);
        word_w = std::max(word_w, text_size(k.regular, k.s(tok::kBody), r.words).x);
    }
    const float top = f.row_max.y + k.s(tok::kS1);
    const TopMenuLayout lay =
        top_menu_layout(f.display.x, f.display.y, top, button.x0, plan.count, word_w, k.u);
    const float slide = (1.0f - t) * -k.s(tok::kS2);
    std::optional<TopMenuEntry> picked;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    if (begin_layer("##top-menu-card", {lay.card.x0, lay.card.y0 + slide},
                    {lay.card.w(), lay.card.h()})) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        for (int i = 0; i < plan.count; ++i) {
            const Row& r = row_of(plan.entry[static_cast<std::size_t>(i)]);
            Box area = lay.row[static_cast<std::size_t>(i)];
            area.y0 += slide;
            area.y1 += slide;
            if (draw_row(k, dl, r, area, entries.profile)) {
                picked = r.entry;
            }
        }
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 lo{wp.x, wp.y};
        const ImVec2 hi{wp.x + lay.card.w(), wp.y + lay.card.h()};
        k.toast_avoid({lo.x, lo.y, lay.card.w(), lay.card.h()});
        overlay_card(k, dl, lo, hi, k.s(tok::kRadiusCard), col::kRaised);
        dl->ChannelsMerge();
        // A tap anywhere else closes it, the top row included; the button's own tap toggles it.
        const ImVec2 start = tap_start();
        const bool on_button = start.x >= button.x0 && start.x < button.x1 &&
                               start.y >= button.y0 && start.y < button.y1;
        if (open_ && outside_.away(true, lo, hi) && !on_button) {
            open_ = false;
        }
    }
    ImGui::End();
    if (picked) {
        open_ = false;
    }
    if (!open_) {
        block_input("##top-menu-card");
    }
    ImGui::PopStyleVar();
    return picked;
}

} // namespace etude::app::ui
