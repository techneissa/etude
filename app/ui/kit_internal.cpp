// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit_internal.hpp"

#include "info_toggle.hpp"
#include "touch_rules.hpp"

#include <string>

namespace etude::app::ui {

RowTap row_tap(Kit& k, const char* id, const Box& row, const Box* info) {
    RowTap out;
    const auto part = [&](const char* name, const Box& b) {
        if (!(b.w() > 0.0f)) {
            return;
        }
        ImGui::SetCursorScreenPos({b.x0, b.y0});
        out.pressed = ImGui::InvisibleButton(name, {b.w(), b.h()}) || out.pressed;
        const State s = item_state(k, false);
        out.state.hover = out.state.hover || s.hover;
        out.state.pressed = std::max(out.state.pressed, s.pressed);
        out.state.focus = out.state.focus || s.focus;
    };
    const RowTapParts parts = row_tap_parts(row, info, k.s(tok::kHitGap));
    part(id, parts.part[0]);
    if (parts.count > 1) {
        std::string second = id;
        second += "-r";
        part(second.c_str(), parts.part[1]);
    }
    return out;
}

RowLabel row_label(ImFont* font, float size, std::string_view label, float room) {
    RowLabel out;
    const std::string_view text = screen_text(label);
    const auto width = [&](std::string_view part) {
        const std::string copy(part);
        return text_size(font, size, copy.c_str()).x;
    };
    if (width(text) <= room) {
        out.first.assign(text);
        out.width = width(text);
        return out;
    }
    // The longest first line that fits, broken at a space; the rest is the second line.
    std::size_t at = std::string_view::npos;
    for (std::size_t i = text.find(' '); i != std::string_view::npos; i = text.find(' ', i + 1)) {
        if (width(text.substr(0, i)) > room) {
            break;
        }
        at = i;
    }
    if (at == std::string_view::npos || at == 0) {
        out.first = fit(font, size, text, room);
        out.width = width(out.first.view());
        return out;
    }
    out.first.assign(text.substr(0, at));
    out.second = fit_unreported(font, size, text.substr(at + 1), room);
    // A second line that is cut is a cut label: the audit is told the whole label, not its half.
    if (out.second.view() != text.substr(at + 1)) {
        audit::cut(text, std::string(out.first.view()) + " " + std::string(out.second.view()));
    }
    out.width = std::max(width(out.first.view()), width(out.second.view()));
    return out;
}

void draw_row_label(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                    ImVec4 colour, const RowLabel& label) {
    if (!label.two()) {
        label_at(k, dl, font, size, slot, LabelAlign::Left, colour, label.first.c_str());
        return;
    }
    const StackLine top{font, size, colour, label.first.c_str()};
    const StackLine bottom{font, size, colour, label.second.c_str()};
    label_stack(k, dl, slot, top, &bottom);
}

bool info_button(Kit& k, const char* text, ImVec2 centre, const char* icon_glyph,
                 const ImVec4* ink, float glyph_size, float hit_up) {
    // The label's width and the row's height are fractional, so the button's centre is too; the
    // face and the glyph (whose pen is a whole pixel) are placed from a whole-pixel centre, so
    // the face is not half a pixel off the ink.
    centre = {std::round(centre.x), std::round(centre.y)};
    const float hit = k.s(tok::kHit);
    const float hit_cy = centre.y - hit_up;
    ImGui::SetCursorScreenPos({centre.x - hit * 0.5f, hit_cy - hit * 0.5f});
    ImGui::PushID(text);
    const bool pressed = ImGui::InvisibleButton("##info", {hit, hit});
    const ImGuiID id = ImGui::GetItemID();
    // A press that lands on an (i) is that (i)'s tap in the making: the frame's close-on-press
    // (`finish_frame`) leaves the bubble to the release, which opens, closes or switches it.
    if (pressed || ImGui::IsItemActive()) {
        k.info_seen = true;
    }
    const bool open = k.info_id == id;
    // An ink given is the button's own colour in every state: open, it lights its face in a faint
    // tint of that colour (the tint's fill, no accent line) and the glyph stays in it.
    const State st = item_state(k, open, true, false, ink);
    ImGui::PopID();
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float half = k.s(tok::kChip) * 0.5f;
    const Box face{centre.x - half, centre.y - half, centre.x + half, centre.y + half};
    audit::note("##info", {face.x0, face.y0}, {face.x1, face.y1});
    draw_state(k, dl, "##info", face,
               {centre.x - hit * 0.5f, hit_cy - hit * 0.5f, centre.x + hit * 0.5f,
                hit_cy + hit * 0.5f},
               st);
    glyph(k, dl, icon_glyph != nullptr ? icon_glyph : icon::kInfo, centre,
          glyph_size > 0.0f ? glyph_size : k.s(tok::kIconSm), state_ink(st, ink != nullptr ? *ink : col::kText3));
    if (pressed) {
        k.info_id = info_after_tap(k.info_id, id);
        if (k.info_id == id) {
            k.info_text = text;
            k.info_in_settings = k.in_settings;
        }
    }
    if (k.info_id == id) {
        k.info_anchor = {centre.x, centre.y + k.s(tok::kS3)};
    }
    return pressed;
}

} // namespace etude::app::ui
