// SPDX-License-Identifier: GPL-3.0-or-later
// Rows with two lines of words: the title over a detail line, with a switch or a chevron at the
// end, and a paragraph of help text. For the pages whose rows say what they are doing ("Phone /
// Ready").
#include "kit.hpp"

#include "kit_internal.hpp"

#include "tap.hpp"

#include <string>
#include <string_view>
#include <vector>

namespace etude::app::ui {

namespace {

// Two lines of words in `box`, the title over the detail, each cut to the box's width.
void two_lines(Kit& k, ImDrawList* dl, const Box& box, const char* label, const char* detail,
               ImVec4 label_ink, ImVec4 detail_ink, ImFont* title_font, float title_size,
               float detail_size) {
    const FitText top = fit(title_font, title_size, label, box.w());
    const FitText bottom = fit(k.regular, detail_size, detail, box.w());
    const StackLine title{title_font, title_size, label_ink, top.c_str()};
    const StackLine sub{k.regular, detail_size, detail_ink, bottom.c_str()};
    label_stack(k, dl, box, title, &sub);
}

Box box_of(ImVec2 origin, const Rect& r) {
    return {origin.x + r.x, origin.y + r.y, origin.x + r.right(), origin.y + r.bottom()};
}

} // namespace

bool switch_row_detail(Kit& k, const char* icon, const char* label, const char* detail,
                       ImVec4 detail_ink, bool* value, const ToggleRowGeometry& g) {
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const Box row = box_of(origin, g.row);
    ImGui::PushID(label);
    ImGui::SetNextItemAllowOverlap();
    ImGui::SetCursorScreenPos({row.x0, row.y0});
    const bool pressed = ImGui::InvisibleButton("##row", {row.w(), row.h()});
    const ImGuiID id = ImGui::GetItemID();
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, label, row, row, st);
    const Box icon_box = box_of(origin, g.icon);
    glyph(k, dl, icon, {icon_box.cx(), icon_box.cy()}, k.s(tok::kIconLg), col::kText2);
    two_lines(k, dl, box_of(origin, g.text), label, detail, col::kText, detail_ink, k.bold,
              k.s(tok::kTitle), k.s(tok::kBody));
    const Box track = box_of(origin, g.track);
    switch_knob(k, dl, {track.x0, track.y0}, *value, id, track.w(), track.h());
    ImGui::PopID();
    ImGui::SetCursorScreenPos(origin);
    ImGui::Dummy({row.w(), row.h()});
    if (tapped(pressed)) {
        *value = !*value;
        return true;
    }
    return false;
}

bool detail_tile(Kit& k, const char* id, const char* icon, const char* title, const char* detail,
                 ImVec2 origin, const Rect& area, bool muted) {
    const Box tile = box_of(origin, area);
    ImGui::SetCursorScreenPos({tile.x0, tile.y0});
    const bool pressed = ImGui::InvisibleButton(id, {tile.w(), tile.h()});
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, id, tile, tile, st, Idle::Fill, k.s(tok::kRadiusCard));
    const ImVec4 ink = muted ? col::kText3 : col::kText;
    const ImVec4 sub_ink = muted ? col::kText3 : col::kText2;
    const float pad = k.s(tok::kS3);
    const float icon_size = k.s(tok::kIconLg);
    glyph(k, dl, icon, {tile.x0 + pad + icon_size * 0.5f, tile.cy()}, icon_size, sub_ink);
    const float x = tile.x0 + pad + icon_size + pad;
    two_lines(k, dl, {x, tile.y0, tile.x1 - pad, tile.y1}, title, detail, ink, sub_ink, k.regular,
              k.s(tok::kBody), k.s(tok::kLabel));
    return tapped(pressed);
}

namespace {

// `text` broken at spaces into at most `max_lines` lines no wider than `room` (greedy).
std::vector<std::string> wrapped(Kit& k, std::string_view text, float size, float room,
                                 int max_lines) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size() && static_cast<int>(lines.size()) < max_lines) {
        std::size_t end = text.find(' ', at);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        const std::string word(text.substr(at, end - at));
        const std::string trial = line.empty() ? word : line + " " + word;
        if (!line.empty() && text_size(k.regular, size, trial.c_str()).x > room) {
            lines.push_back(line);
            line = word;
        } else {
            line = trial;
        }
        at = end + 1;
    }
    if (!line.empty() && static_cast<int>(lines.size()) < max_lines) {
        lines.push_back(line);
    }
    return lines;
}

// Under the words of the rows above it: past their icon.
float help_indent(const Kit& k) { return 2.0f * k.s(tok::kS3) + k.s(tok::kIcon); }

} // namespace

float help_text_height(Kit& k, const char* text, float width, int max_lines) {
    const float size = k.s(tok::kLabel);
    const float room = width - help_indent(k) - k.s(tok::kS3);
    return static_cast<float>(wrapped(k, text, size, room, max_lines).size()) *
           text_size(k.regular, size, "Ag").y;
}

void help_text(Kit& k, const char* text, ImVec4 ink, int max_lines) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float size = k.s(tok::kLabel);
    const float pitch = text_size(k.regular, size, "Ag").y;
    const float x = pos.x + help_indent(k);
    const float room = pos.x + w - k.s(tok::kS3) - x;
    float y = pos.y;
    for (const std::string& line : wrapped(k, text, size, room, max_lines)) {
        label_at(k, dl, k.regular, size, {x, y, x + room, y + pitch}, LabelAlign::Left, ink,
                 line.c_str());
        y += pitch;
    }
    end_row(pos, {w, y - pos.y});
}

} // namespace etude::app::ui
