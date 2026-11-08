// SPDX-License-Identifier: GPL-3.0-or-later
#include "device_check_card.hpp"

#include "bar.hpp"
#include "crash_layout.hpp" // wrap_report
#include "icon_audit.hpp"
#include "icons.hpp"
#include "item_probe.hpp"

#include "etude/device_check_text.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace etude::app::ui {

namespace {

const char* tone_icon(DeviceCheckTone tone) {
    switch (tone) {
    case DeviceCheckTone::Waiting:
        return icon::kPlug;
    case DeviceCheckTone::Found:
        return icon::kKeyboard;
    case DeviceCheckTone::Good:
        return icon::kCircleCheck;
    case DeviceCheckTone::Warn:
        return icon::kWarning;
    }
    return icon::kKeyboard;
}

ImVec4 tone_colour(DeviceCheckTone tone) {
    switch (tone) {
    case DeviceCheckTone::Waiting:
        return col::kText2;
    case DeviceCheckTone::Warn:
        return col::kWarning;
    default:
        return col::kAccent;
    }
}

// The step's icon on a tile of the step's colour, centred in the `hit` square at `pos`.
void badge(const Kit& k, ImDrawList* dl, ImVec2 pos, DeviceCheckTone tone, float grow) {
    const float hit = k.s(tok::kHit) * grow;
    const float face = k.s(tok::kFace) * grow;
    const ImVec2 a{pos.x + (hit - face) * 0.5f, pos.y + (hit - face) * 0.5f};
    const ImVec2 b{a.x + face, a.y + face};
    const ImVec4 c = tone_colour(tone);
    const float radius = k.s(tok::kRadiusControl);
    if (tone == DeviceCheckTone::Waiting) {
        dl->AddRectFilled(a, b, u32(col::kFill), radius);
    } else {
        dl->AddRectFilled(a, b, u32(with_alpha(c, tok::kSelectedFill)), radius);
        dl->AddRect(a, b, u32(with_alpha(c, tok::kSelectedLine)), radius, 0, k.s(tok::kHairline));
    }
    glyph(k, dl, tone_icon(tone), {pos.x + hit * 0.5f, pos.y + hit * 0.5f}, k.s(tok::kIcon) * grow, c);
}

// The card's words, wrapped to the room beside the badge and the buttons: a long keyboard name or
// detail goes on a second line, it is never cut. Sizes are the popup rule's for `step`.
struct DeviceLines {
    std::vector<std::string> title;
    std::vector<std::string> detail;
    float title_size = 0.0f;
    float detail_size = 0.0f;
    bool single() const { return title.size() <= 1 && detail.size() <= 1; }
    // The block's height: the card's row when it is one line over one, else the lines' own height.
    float height(float row) const {
        const float lines = static_cast<float>(title.size()) * title_size * kLineRatio +
                            static_cast<float>(detail.size()) * detail_size * kLineRatio;
        return single() ? row : std::max(row, lines);
    }
    static constexpr float kLineRatio = 1.2f;
};

DeviceLines device_lines(const Kit& k, const DeviceCheckWords& words, int step, float room) {
    DeviceLines l;
    l.title_size = k.s(tok::popup_type(tok::kBody, step, tok::PopupText::Title));
    l.detail_size = k.s(tok::popup_type(tok::kLabel, step, tok::PopupText::Text));
    const auto wrap = [&](ImFont* font, float size, const std::string& text) {
        return wrap_report(std::string(screen_text(std::string_view(text))), room,
                           [&](const std::string& line) { return text_size(font, size, line.c_str()).x; });
    };
    l.title = wrap(k.bold, l.title_size, words.title);
    l.detail = wrap(k.regular, l.detail_size, words.detail);
    return l;
}

// The lines in `slot` (`rest` tall): one over one as a stack, else top down, centred in the slot.
void draw_words(Kit& k, ImDrawList* dl, const DeviceLines& l, const Box& slot) {
    if (l.single()) {
        const StackLine top{k.bold, l.title_size, col::kText, l.title.empty() ? "" : l.title[0].c_str()};
        const StackLine bottom{k.regular, l.detail_size, col::kText2, l.detail.empty() ? "" : l.detail[0].c_str()};
        label_stack(k, dl, slot, top, &bottom);
        return;
    }
    float y = slot.y0 + (slot.h() - l.height(0.0f)) * 0.5f;
    const auto lines = [&](const std::vector<std::string>& text, ImFont* font, float size, ImVec4 colour) {
        for (const std::string& line : text) {
            const float lh = size * DeviceLines::kLineRatio;
            label_at(k, dl, font, size, {slot.x0, y, slot.x1, y + lh}, LabelAlign::Left, colour, line.c_str());
            y += lh;
        }
    };
    lines(l.title, k.bold, l.title_size, col::kText);
    lines(l.detail, k.regular, l.detail_size, col::kText2);
}

} // namespace

DeviceCheckChoice DeviceCheckCard::draw(Kit& k, const etude::DeviceCheck* check,
                                        const RectF& area, int octave_offset, bool allowed) {
    const bool want = device_check_wanted(check != nullptr && !check->finished(), !allowed, false);
    wanted_ = want;
    shown_p_ = approach(shown_p_, want ? 1.0f : 0.0f, k.dt, tok::kMove);
    rect_ = {};
    if (shown_p_ <= 0.0f || check == nullptr) {
        return DeviceCheckChoice::None;
    }
    const DeviceCheckWords words = device_check_words(*check, octave_offset);
    const bool waiting = check->step() == DeviceCheckStep::WaitingForKeyboard;
    const bool can_finish = check->step() == DeviceCheckStep::NoteSeen;
    // The popup rule: a wider window gets a larger card, title, detail and buttons.
    const int step = tok::wide_step(ImGui::GetIO().DisplaySize.x);
    const float grow = tok::popup_grow(step);
    const float hit = k.s(tok::kHit) * grow;
    const float pad = k.s(tok::kS3);
    const float gap = k.s(tok::kS2);
    ChipOpts done_o = popup_button(k, step, icon::kCheck, "Done");
    done_o.on = true;
    const float done_w = std::max(chip_width(k, done_o.icon, "Done"), done_o.width);
    const float card_w = tok::popup_width(tok::kDeviceCheckW, step, ImGui::GetIO().DisplaySize.x / k.u);
    const float w = std::min(k.s(card_w), std::max(area.w - k.s(tok::kS5), 0.0f));
    const float room = w - 2.0f * pad - 2.0f * (hit + gap) - (can_finish ? done_w + gap : 0.0f);
    const DeviceLines lines = device_lines(k, words, step, std::max(room, 0.0f));
    const float row_h = lines.height(hit);
    const float h = row_h + 2.0f * pad;
    const float t = ease_out(shown_p_);
    const float x = area.x + (area.w - w) * 0.5f;
    const float rest_y = area.y + std::max(0.0f, (area.h - h) * 0.5f);
    const float y = rest_y + (1.0f - t) * -k.s(tok::kS2);
    rect_ = {x, rest_y, w, h};
    DeviceCheckChoice choice = DeviceCheckChoice::None;
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    if (begin_layer("##device-check", {x, y}, {w, h})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        const float left = x + pad;
        const float right = x + w - pad;
        const float top = y + pad;
        const float row = top + (row_h - hit) * 0.5f; // the badge and the buttons, centred on the words
        badge(k, dl, {left, row}, words.tone, grow);

        // The leaving button at the right: the keyboard that stays (no keyboard found) or a cross.
        ChipOpts leave_o;
        leave_o.icon = waiting ? icon::kKeyboard : icon::kClose;
        if (step > 0) {
            leave_o.width = hit;
            leave_o.hit_h = hit;
            leave_o.face_h = k.s(tok::kFace) * grow;
        }
        if (chip(k, "##check-skip", "", {right - hit, row}, leave_o)) {
            choice = DeviceCheckChoice::Skip;
        }
        if (can_finish) {
            const float dx = right - hit - gap - done_w;
            if (chip(k, "##check-done", "Done", {dx, row}, done_o)) {
                choice = DeviceCheckChoice::Done;
            }
        }
        const float text_x = left + hit + gap;
        draw_words(k, dl, lines, {text_x, top, text_x + std::max(room, 0.0f), top + row_h});

        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        k.toast_avoid({wp.x, wp.y, w, h});
        overlay_card(k, dl, wp, {wp.x + w, wp.y + h}, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        probe::add_rect("##device-check-rect", wp, {wp.x + w, wp.y + h});
    }
    ImGui::End();
    if (!want) {
        block_input("##device-check");
    }
    ImGui::PopStyleVar();
    return choice;
}

} // namespace etude::app::ui
