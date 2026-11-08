// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "font_ink.hpp"
#include "icon_audit.hpp"
#include "icons.hpp"
#include "pressed_fade.hpp"
#include "row_layout.hpp"
#include "sections_port.hpp"
#include "etude/accidental_text.hpp"
#include "etude/note_style.hpp"
#include "transport.hpp"

#include <imgui_internal.h> // ClearActiveID

#include <algorithm>
#include <array>
#include <cassert>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>
#include <vector>

namespace etude::app::ui {

Disclosure disclosure_tap(Kit& k, const char* id, ImVec2 pos, ImVec2 size, bool default_open) {
    Disclosure d;
    d.id = ImGui::GetID(id);
    ImGuiStorage* store = ImGui::GetStateStorage();
    d.open = store->GetBool(d.id, default_open);
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, size);
    const State st = item_state(k, false);
    d.held = st.pressed > 0.0f;
    d.hover = st.hover;
    // The header's own face: hover and press show on it at once, like any row.
    const Box face{pos.x, pos.y, pos.x + size.x, pos.y + size.y};
    draw_state(k, ImGui::GetWindowDrawList(), id, face, face, st);
    if (tapped(pressed)) {
        d.open = !d.open;
        store->SetBool(d.id, d.open);
    }
    return d;
}

void disclosure_chevron(Kit& k, const Disclosure& d, ImVec2 pos, ImVec2 size) {
    // Shut it points right; open, a quarter turn clockwise, down.
    turn_glyph(k, ImGui::GetWindowDrawList(), d.id, MorphGlyph::Chevron,
               d.open ? kQuarterTurn : 0.0f, {chevron_x(k, pos, size), pos.y + size.y * 0.5f},
               k.s(tok::kIconSm), d.held || d.hover ? col::kText : col::kText2);
}

float disclosure_label_right(const Kit& k, ImVec2 pos, ImVec2 size) {
    return chevron_x(k, pos, size) - k.s(tok::kIconSm) * 0.5f - k.s(tok::kS2);
}

bool armed(const Kit& k, ImGuiID id) { return k.confirm_id == id && k.now < k.confirm_until; }

bool icon_action(Kit& k, const char* id, const char* icon, const char* label, float size,
                 bool enabled, bool danger, bool is_armed) {
    // The hit area is never under `kHit`, centred where the caller put a `size` square.
    const ImVec2 at = ImGui::GetCursorScreenPos();
    const float hit = std::max(size, k.s(tok::kHit));
    const ImVec2 pos{at.x - (hit - size) * 0.5f, at.y - (hit - size) * 0.5f};
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, {hit, hit});
    ImGui::EndDisabled();
    const State st = item_state(k, false, true, !enabled);
    if (enabled) {
        long_press_tip(k, label);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float d = std::min(k.s(tok::kFace), size);
    const ImVec2 a{pos.x + (hit - d) * 0.5f, pos.y + (hit - d) * 0.5f};
    const ImVec2 b{a.x + d, a.y + d};
    const Box face{a.x, a.y, b.x, b.y};
    const Box area{pos.x, pos.y, pos.x + hit, pos.y + hit};
    ImVec4 ink = danger ? col::kWarning : col::kText;
    if (is_armed) {
        // Armed: the warning colour on the face until the second tap.
        dl->AddRectFilled(a, b, u32(with_alpha(col::kWarning, tok::kSelectedFill)),
                          face_radius(k, face));
        draw_state(k, dl, id, face, area, st);
        ink = col::kWarning;
    } else {
        draw_state(k, dl, id, face, area, st, Idle::Fill);
    }
    audit::note(id, a, b);
    glyph(k, dl, icon, {(a.x + b.x) * 0.5f, (a.y + b.y) * 0.5f}, k.s(tok::kIconSm),
          state_ink(st, ink));
    return enabled && tapped(pressed);
}

bool text_button(Kit& k, const char* id, const char* icon, const char* label, bool confirm,
                 const char* confirm_label) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    const bool pressed = ImGui::InvisibleButton(id, {w, h});
    const ImGuiID item = ImGui::GetItemID();
    const State st = item_state(k, false);
    const bool is_armed = confirm && armed(k, item);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ch = k.s(tok::kFace);
    const ImVec2 a{pos.x, pos.y + (h - ch) * 0.5f};
    const ImVec2 b{pos.x + w, a.y + ch};
    const Box face{a.x, a.y, b.x, b.y};
    ImVec4 ink = col::kText;
    if (is_armed) {
        dl->AddRectFilled(a, b, u32(with_alpha(col::kWarning, tok::kSelectedFill)),
                          face_radius(k, face));
        ink = col::kWarning;
    }
    draw_state(k, dl, id, face, {pos.x, pos.y, pos.x + w, pos.y + h}, st,
               is_armed ? Idle::None : Idle::Fill);
    ink = state_ink(st, ink);
    const char* text = is_armed && confirm_label ? confirm_label : label;
    const float size = k.s(tok::kLabel);
    const ImVec2 ts = text_size(k.regular, size, text);
    const float total = ts.x + (icon ? k.s(tok::kIconSm) + k.s(tok::kS2) : 0.0f);
    float x = pos.x + (w - total) * 0.5f;
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIconSm) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIconSm),
              ink);
        x += k.s(tok::kIconSm) + k.s(tok::kS2);
    }
    label_at(k, dl, k.regular, size, {x, face.y0, x + ts.x, face.y1}, LabelAlign::Centre, ink,
             text);
    if (!tapped(pressed)) {
        return false;
    }
    if (!confirm) {
        return true;
    }
    if (is_armed) {
        k.confirm_id = 0;
        return true;
    }
    k.confirm_id = item;
    k.confirm_until = k.now + 3.0;
    return false;
}

bool action_button(Kit& k, const char* id, const char* icon, const char* label, ImVec2 pos,
                   ImVec2 size, bool enabled, bool primary, bool danger, bool confirm,
                   const char* confirm_label, float face_h, float label_pt, const ImVec4* icon_ink,
                   bool icon_ink_always) {
    ImGui::SetCursorScreenPos(pos);
    ImGui::BeginDisabled(!enabled);
    const bool pressed = ImGui::InvisibleButton(id, size);
    ImGui::EndDisabled();
    const ImGuiID item = ImGui::GetItemID();
    const State st = item_state(k, false, true, !enabled);
    const bool is_armed = confirm && armed(k, item);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float ch = std::min(size.y, face_h > 0.0f ? k.s(face_h) : k.s(tok::kFace));
    const ImVec2 a{pos.x, pos.y + (size.y - ch) * 0.5f};
    const ImVec2 b{pos.x + size.x, a.y + ch};
    const Box face{a.x, a.y, b.x, b.y};
    ImVec4 ink = col::kText;
    Idle idle = Idle::Fill;
    if (primary && !is_armed) {
        idle = Idle::Accent;
        ink = col::kAccentInk;
    } else if (danger) {
        ink = col::kWarning;
    }
    if (is_armed) {
        dl->AddRectFilled(a, b, u32(with_alpha(col::kWarning, tok::kSelectedFill)),
                          face_radius(k, face));
        idle = Idle::None;
        ink = col::kWarning;
    }
    draw_state(k, dl, id, face, {pos.x, pos.y, pos.x + size.x, pos.y + size.y}, st, idle);
    audit::note(id, pos, {pos.x + size.x, pos.y + size.y}, InkKind::Target);
    ink = state_ink(st, ink, idle == Idle::Accent);
    const char* text = is_armed && confirm_label != nullptr ? confirm_label : label;
    const float grow = label_pt > 0.0f ? label_pt / tok::kLabel : 1.0f;
    const float tsize = k.s(tok::kLabel) * grow;
    const float isize = k.s(tok::kIconSm) * grow;
    const float gap = k.s(tok::kS2);
    const float avail = size.x - k.s(tok::kS3);
    const bool has_icon = icon != nullptr;
    const FitText shown =
        fit(primary ? k.bold : k.regular, tsize, text, avail - (has_icon ? isize + gap : 0.0f));
    const ImVec2 ts = text_size(primary ? k.bold : k.regular, tsize, shown.c_str());
    const float total = ts.x + (has_icon ? isize + gap : 0.0f);
    float x = pos.x + (size.x - total) * 0.5f;
    const float cy = (a.y + b.y) * 0.5f;
    if (has_icon) {
        ImVec4 icon_colour = ink;
        if (icon_ink != nullptr) {
            icon_colour = icon_ink_always ? with_alpha(*icon_ink, ink.w)
                                          : mix(ink, with_alpha(*icon_ink, ink.w), st.pressed);
        }
        glyph(k, dl, icon, {x + isize * 0.5f, cy}, isize * 1.05f, icon_colour);
        x += isize + gap;
    }
    label_at(k, dl, primary ? k.bold : k.regular, tsize, {x, face.y0, x + ts.x, face.y1},
             LabelAlign::Centre, ink, shown.c_str());
    if (!enabled || !tapped(pressed)) {
        return false;
    }
    if (!confirm) {
        return true;
    }
    if (is_armed) {
        k.confirm_id = 0;
        return true;
    }
    k.confirm_id = item;
    k.confirm_until = k.now + 3.0;
    return false;
}

int header(Kit& k, const char* title, bool back, bool close) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit) + k.s(tok::kS1);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    int result = 0;
    float x = pos.x;
    const float hit = k.s(tok::kHit);
    if (back) {
        ImGui::SetCursorScreenPos({pos.x - k.s(tok::kS1), pos.y + (h - hit) * 0.5f});
        const bool pressed = ImGui::InvisibleButton("##back", {hit, hit});
        const State st = item_state(k, false);
        const ImVec2 c{pos.x - k.s(tok::kS1) + hit * 0.5f, pos.y + h * 0.5f};
        const float half = k.s(tok::kFace) * 0.5f;
        const Box face{c.x - half, c.y - half, c.x + half, c.y + half};
        draw_state(k, dl, "##back", face,
                   {c.x - hit * 0.5f, c.y - hit * 0.5f, c.x + hit * 0.5f, c.y + hit * 0.5f}, st);
        audit::note("##back", {face.x0, face.y0}, {face.x1, face.y1});
        glyph(k, dl, icon::kBack, c, k.s(tok::kIconLg), state_ink(st, col::kAccent));
        if (tapped(pressed)) {
            result = 1;
        }
        x += hit - k.s(tok::kS2);
    } else {
        x += k.s(tok::kS3);
    }
    const float size = k.s(tok::kTitle);
    label_at(k, dl, k.bold, size, {x, pos.y, pos.x + w, pos.y + h}, LabelAlign::Left, col::kText,
             title);
    if (close) {
        ImGui::SetCursorScreenPos({pos.x + w - hit + k.s(tok::kS1), pos.y + (h - hit) * 0.5f});
        const bool pressed = ImGui::InvisibleButton("##close", {hit, hit});
        const State st = item_state(k, false);
        const ImVec2 c{pos.x + w - hit * 0.5f + k.s(tok::kS1), pos.y + h * 0.5f};
        const float half = k.s(tok::kFace) * 0.5f;
        const Box face{c.x - half, c.y - half, c.x + half, c.y + half};
        draw_state(k, dl, "##close", face,
                   {c.x - hit * 0.5f, c.y - hit * 0.5f, c.x + hit * 0.5f, c.y + hit * 0.5f}, st,
                   Idle::Fill);
        audit::note("##close", {face.x0, face.y0}, {face.x1, face.y1});
        glyph(k, dl, icon::kClose, c, k.s(tok::kIconSm), state_ink(st, col::kText2));
        if (tapped(pressed)) {
            result = 2;
        }
    }
    end_row(pos, {w, h});
    return result;
}

void drag_scroll(Kit& k, bool lock_axis) {
    const ImGuiIO& io = ImGui::GetIO();
    const ImGuiID window = ImGui::GetID("##drag-scroll");
    const int frame = ImGui::GetFrameCount();
    if (k.scroll_window != 0 && frame - k.scroll_frame > 1) {
        k.scroll.cancel(); // the window it followed was not drawn: no coasting on return
        k.scroll_window = 0;
    }
    const bool clicked = ImGui::IsMouseClicked(0);
    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);
    const float x = io.MousePos.x / k.u;
    const float y = io.MousePos.y / k.u;
    if (clicked && k.scroll_window == window && !hovered) {
        k.scroll.cancel(); // a touch elsewhere stops the coasting
        k.scroll_window = 0;
    }
    if (clicked && hovered) {
        k.scroll_window = window;
        k.scroll.set_axis(ScrollDrag::Axis::Vertical, lock_axis);
        k.scroll.press(x, y, k.now, io.MouseSource != ImGuiMouseSource_Mouse);
    }
    if (k.scroll_window != window) {
        return;
    }
    k.scroll_frame = frame;
    if (ImGui::IsMouseDown(0)) {
        if (!clicked) {
            k.scroll.move(x, y, k.now);
        }
    } else {
        k.scroll.release(x, y, k.now);
    }
    const ScrollDrag::Step step = k.scroll.update(k.now, k.slider_active, ImGui::GetScrollY() / k.u,
                                                  ImGui::GetScrollMaxY() / k.u);
    if (step.took_touch) {
        ImGui::ClearActiveID(); // the controls under the finger let go
    }
    if (step.scroll_by != 0.0f) {
        ImGui::SetScrollY(ImGui::GetScrollY() + step.scroll_by * k.u);
    }
    if (!k.scroll.busy()) {
        k.scroll_window = 0;
    }
}

} // namespace etude::app::ui
