// SPDX-License-Identifier: GPL-3.0-or-later
#include "pages_configs_confirm.hpp"

#include "bar.hpp" // begin_layer
#include "configs_layout.hpp"
#include "crash_layout.hpp" // wrap_report
#include "icons.hpp"
#include "profile_ui.hpp"
#include "tap.hpp"

#include <imgui_internal.h> // BringWindowToDisplayFront

#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

bool g_was_open = false; // it was open on that frame
OutsideTap g_outside;
ImVec2 g_min{};
ImVec2 g_max{};

std::vector<std::string> wrapped(ImFont* font, float size, const std::string& text, float max_w) {
    return wrap_report(text, max_w,
                       [&](const std::string& s) { return text_size(font, size, s.c_str()).x; });
}

// One line of `lines` per slot of height `lh` from the top of `block`, centred.
void centred_lines(Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& block, float lh,
                   const std::vector<std::string>& lines, ImVec4 colour) {
    for (std::size_t i = 0; i < lines.size(); ++i) {
        const float y = block.y0 + static_cast<float>(i) * lh;
        label_at(k, dl, font, size, {block.x0, y, block.x1, y + lh}, LabelAlign::Centre, colour,
                 lines[i].c_str());
    }
}

} // namespace

bool copy_confirm_covers(ImVec2* min, ImVec2* max) {
    if (g_max.x <= g_min.x) {
        return false;
    }
    *min = g_min;
    *max = g_max;
    return true;
}

std::optional<std::int64_t> draw_copy_confirm(Kit& k, CopyAsk& ask) {
    ask.page_frame(ImGui::GetFrameCount()); // a page that was away closes it
    const Profile* who = k.profiles != nullptr && k.profiles->set != nullptr
                             ? k.profiles->set->find(ask.profile())
                             : nullptr;
    if (ask.open() && (who == nullptr || who->id == k.profiles->active)) {
        ask.cancel(); // the profile went, or became the one in use
    }
    if (!ask.open()) {
        g_was_open = false;
        g_min = g_max = {};
        return std::nullopt;
    }
    if (!g_was_open) {
        g_outside = OutsideTap{}; // the tap that opened it never closes it
        g_was_open = true;
    }
    const ImVec2 display = ImGui::GetIO().DisplaySize;
    const float text_w = configs_confirm_text_w(display.x, k.u);
    const int step = tok::wide_step(display.x);
    const float title_pt = tok::popup_type(tok::kTitle, step, tok::PopupText::Title);
    const float detail_pt = tok::popup_type(tok::kBody, step, tok::PopupText::Detail);
    const std::vector<std::string> title =
        wrapped(k.bold, k.s(title_pt), config_copy_question(who->name), text_w);
    const std::vector<std::string> detail =
        wrapped(k.regular, k.s(detail_pt), kConfigCopyDetail, text_w);
    const ConfigsConfirmPlan plan = plan_configs_confirm(
        display.x, display.y, k.u, static_cast<int>(title.size()), static_cast<int>(detail.size()));
    std::optional<std::int64_t> copy;
    bool close = false;
    if (begin_layer("##configs-confirm", {0.0f, 0.0f}, display)) {
        ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        profile_avatar(k, dl, {plan.avatar.cx(), plan.avatar.cy()}, plan.avatar.w(), *who);
        centred_lines(k, dl, k.bold, k.s(plan.title_pt), plan.title, plan.title_lh, title,
                      col::kText);
        centred_lines(k, dl, k.regular, k.s(plan.detail_pt), plan.detail, plan.detail_lh, detail,
                      col::kText2);
        const float label_pt = plan.step > 0 ? plan.button_pt : 0.0f;
        if (action_button(k, "##copy-cancel", icon::kClose, "Cancel",
                          {plan.cancel.x0, plan.cancel.y0}, {plan.cancel.w(), plan.cancel.h()},
                          true, false, false, false, nullptr, plan.face_h, label_pt)) {
            close = true;
        }
        if (action_button(k, "##copy-confirm", icon::kCheck, "Copy", {plan.copy.x0, plan.copy.y0},
                          {plan.copy.w(), plan.copy.h()}, true, true, false, false, nullptr,
                          plan.face_h, label_pt)) {
            copy = ask.copy();
        }
        dl->ChannelsSetCurrent(0);
        dl->AddRectFilled({0.0f, 0.0f}, display, u32(col::kScrim));
        const Box c = plan.card;
        // Opaque: the live previews under the dim must not show through the words.
        const ImVec4 fill{col::kSurface.x, col::kSurface.y, col::kSurface.z, 1.0f};
        sheet_card(k, dl, {c.x0, c.y0}, {c.x1, c.y1}, k.s(tok::kRadiusSheet), fill);
        k.toast_avoid({c.x0, c.y0, c.w(), c.h()});
        dl->ChannelsMerge();
        g_min = {0.0f, 0.0f};
        g_max = display;
        ask.outside_tap(g_outside.away(true, {c.x0, c.y0}, {c.x1, c.y1}));
    }
    ImGui::End();
    if (close) {
        ask.cancel();
    }
    return copy;
}

} // namespace etude::app::ui
