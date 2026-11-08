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
#include "undo_chip.hpp"

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

// A compact control (a switch, a value pill, a chevron) is placed by its right edge: tells the
// column probe where it went.
void note_compact(const Kit& k, ImVec2 pos, float w, float natural_w) {
    if (column_probe::on()) {
        const RowFrame f = row_frame(pos.x, w, k.u);
        column_probe::note(f, RowKind::Compact, compact_span(f.plan, natural_w));
    }
}

void switch_knob(Kit& k, ImDrawList* dl, ImVec2 a, bool on, ImGuiID id, float width, float height) {
    const float w = width > 0.0f ? width : k.s(tok::kSwitchW);
    const float h = height > 0.0f ? height : k.s(tok::kSwitchH);
    const float t = tween(id, on ? 1.0f : 0.0f, tok::kFast);
    const ImVec4 track = mix({1, 1, 1, 0.14f}, col::kAccent, t);
    dl->AddRectFilled(a, {a.x + w, a.y + h}, u32(track), h * 0.5f);
    const float kr = h * 0.5f - k.s(tok::kInset);
    const float kx = a.x + h * 0.5f + (w - h) * t;
    dl->AddCircleFilled({kx, a.y + h * 0.5f + k.s(0.8f)}, kr + k.s(0.6f), u32({0, 0, 0, 0.25f}),
                        24);
    dl->AddCircleFilled({kx, a.y + h * 0.5f}, kr, u32(mix(col::kText2, {1, 1, 1, 1}, t)), 24);
}

namespace {

float words_width(const Kit& k, std::string_view text, float label_pt = tok::kLabel) {
    text = screen_text(text);
    return k.regular
        ->CalcTextSizeA(k.s(label_pt), FLT_MAX, 0.0f, text.data(), text.data() + text.size())
        .x;
}

// Two lines of words, one in each half of the face; true (so a caller can return its tap).
bool two_lines(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& face, float room,
               ImVec4 ink, std::string_view first, std::string_view second) {
    const FitText line1 = fit(font, size, first, room);
    const FitText line2 = fit(font, size, second, room);
    label_at(k, dl, font, size, {face.x0, face.y0, face.x1, face.cy()}, LabelAlign::Centre, ink,
             line1.c_str());
    label_at(k, dl, font, size, {face.x0, face.cy(), face.x1, face.y1}, LabelAlign::Centre, ink,
             line2.c_str());
    return true;
}

} // namespace

bool gear(Kit& k, ImVec2 centre, float hit, bool active, float alpha) {
    ImGui::SetCursorScreenPos({centre.x - hit * 0.5f, centre.y - hit * 0.5f});
    const bool pressed = ImGui::InvisibleButton("##gear", {hit, hit});
    const State st = item_state(k, false);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float half = std::min(k.s(tok::kFace), hit) * 0.5f;
    const Box face{centre.x - half, centre.y - half, centre.x + half, centre.y + half};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * alpha);
    draw_state(k, dl, "##gear", face,
               {centre.x - hit * 0.5f, centre.y - hit * 0.5f, centre.x + hit * 0.5f,
                centre.y + hit * 0.5f},
               st);
    ImGui::PopStyleVar();
    ImVec4 c = active ? col::kAccent
                      : state_ink(st, with_alpha(col::kText, st.hover ? 1.0f : 0.74f));
    c.w *= alpha;
    audit::note("##gear", {face.x0, face.y0}, {face.x1, face.y1});
    morph_glyph(k, dl, ImGui::GetItemID(), active ? MorphGlyph::Close : MorphGlyph::Settings,
                centre, k.s(tok::kIconLg), c, kSpringSmooth);
    return pressed;
}

bool icon_toggle(Kit& k, const char* id, Icon icon, const char* label, ImVec2 centre, ImVec2 hit,
                 float drawn, bool on, bool live, const char* caption, const ImVec4* tint,
                 float shake_x, float hit_dy, float glyph_dy) {
    // On whole pixels, so the icon sits centred in its button.
    centre = {std::round(centre.x), std::round(centre.y)};
    // The face first, from where the hit area is before it moves by `hit_dy`.
    const FacePlan plan = plan_face(k, centre, hit, drawn, drawn, caption);
    if (plan.lay.hidden) {
        return false; // its pill is shorter than its icon
    }
    const Box& face = plan.lay.face;
    ImGui::SetCursorScreenPos({centre.x - hit.x * 0.5f, centre.y + hit_dy - hit.y * 0.5f});
    const bool pressed = ImGui::InvisibleButton(id, hit);
    if (live) {
        // A control whose caption did not fit still names itself on a long press.
        long_press_tip(k, label != nullptr ? label : (plan.lay.caption ? nullptr : caption));
    }
    // Every state is drawn on the face, never past it.
    const State st = item_state(k, on, live, false, tint);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 ic{face.cx(), face.cy()};
    if (audit::on()) {
        const bool play = icon.morph == MorphGlyph::Play ||
                          (icon.glyph != nullptr && std::strcmp(icon.glyph, icon::kPlay) == 0);
        audit::note(id, {face.x0, face.y0}, {face.x1, face.y1},
                    play ? InkKind::PlayTriangle : InkKind::Centred);
    }
    draw_state(k, dl, id, face, face_area(k, centre, hit), st, Idle::None,
               plan.lay.band ? face.h() * 0.5f : 0.0f);
    const ImVec4 shown = state_ink(st, tint != nullptr ? col::kText3 : col::kText2);
    const float sel = st.selected;
    const ImVec4 lit = tint != nullptr ? *tint : col::kAccent;
    if (icon.glyph != nullptr) {
        glyph(k, dl, icon.glyph, {ic.x + shake_x, ic.y + glyph_dy}, k.s(tok::kIcon), shown);
    } else {
        morph_glyph(k, dl, ImGui::GetItemID(), icon.morph, {ic.x + shake_x, ic.y + glyph_dy},
                    k.s(tok::kIcon), shown);
    }
    if (plan.lay.caption) {
        label_at(k, dl, k.regular, plan.caption_size, plan.lay.caption_box, LabelAlign::Centre,
                 mix(col::kText2, lit, sel), caption);
        audit::ink({plan.lay.caption_box.x0, plan.lay.caption_box.y0},
                   {plan.lay.caption_box.x1, plan.lay.caption_box.y1});
    }
    return live && tapped(pressed);
}

float chip_width(const Kit& k, const char* icon, std::string_view text, const char* trail) {
    if (text.empty()) {
        return k.s(tok::kHit); // an icon alone: round
    }
    float w = 2.0f * k.s(tok::kS3) + words_width(k, text);
    if (icon != nullptr) {
        w += k.s(tok::kIconSm) + k.s(tok::kS2);
    }
    if (trail != nullptr) {
        w += k.s(tok::kIconSm) + k.s(tok::kS1);
    }
    // A pixel spare, so the words are never cut, and never narrower than a short word's chip.
    return std::max(k.s(tok::kChipMinW), std::ceil(w + k.s(tok::kHairline)));
}

float chip_width_at(const Kit& k, const char* icon, std::string_view text, float label_pt) {
    if (text.empty()) {
        return k.s(tok::kHit);
    }
    float w = 2.0f * k.s(tok::kS3) + words_width(k, text, label_pt > 0.0f ? label_pt : tok::kLabel);
    if (icon != nullptr) {
        w += k.s(tok::kIconSm) + k.s(tok::kS2);
    }
    return std::max(k.s(tok::kChipMinW), std::ceil(w + k.s(tok::kHairline)));
}

ChipOpts popup_button(const Kit& k, int step, const char* icon, std::string_view text) {
    ChipOpts o;
    o.icon = icon;
    if (step > 0) {
        const float grow = tok::popup_grow(step);
        o.label_pt = tok::popup_type(tok::kLabel, step, tok::PopupText::Button);
        o.hit_h = k.s(tok::kHit) * grow;
        o.face_h = k.s(tok::kFace) * grow;
        o.width = chip_width_at(k, icon, text, o.label_pt);
    }
    return o;
}

bool press_area(Kit& k, const char* id, const Box& area) {
    (void)k;
    ImGui::SetCursorScreenPos({area.x0, area.y0});
    return ImGui::InvisibleButton(id, {area.w(), area.h()});
}

bool chip(Kit& k, const char* id, std::string_view text, ImVec2 pos, const ChipOpts& o) {
    const float w = o.fixed ? o.width : drawn_chip_w(chip_width(k, o.icon, text, o.trail), o.width);
    const float hit_h = o.hit_h > 0.0f ? o.hit_h : k.s(tok::kHit);
    const float fh = o.face_h > 0.0f ? o.face_h : k.s(tok::kFace);
    ImGui::SetCursorScreenPos(pos);
    const bool pressed = ImGui::InvisibleButton(id, {w, hit_h});
    const State st = item_state(k, o.on, o.live, false, o.tint);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float top = pos.y + (hit_h - fh) * 0.5f;
    const Box face{pos.x, top, pos.x + w, top + fh};
    // A solid chip is selected in white (not in a tint of its colour): the state paints it as an
    // unselected one, then the white goes over it by how selected it is.
    const bool solid = o.solid && o.tint != nullptr;
    State paint = st;
    if (solid) {
        paint.selected = 0.0f;
    }
    draw_state(k, dl, id, face, {pos.x, pos.y, pos.x + w, pos.y + hit_h}, paint, Idle::Fill,
               fh * 0.5f);
    if (solid && st.selected > 0.0f) {
        dl->AddRectFilled({face.x0, face.y0}, {face.x1, face.y1},
                          u32(with_alpha(ImVec4{1.0f, 1.0f, 1.0f, 1.0f}, st.selected)),
                          face_radius(k, face, fh * 0.5f));
    }
    audit::chip(id, {face.x0, face.y0}, {face.x1, face.y1}, !text.empty(), o.face_h > 0.0f);
    // A tinted chip is in its colour (a little softened until it is selected).
    const ImVec4 base = o.tint != nullptr ? mix(col::kText2, *o.tint, 0.8f) : col::kText;
    ImVec4 ink = state_ink(paint, base);
    ImVec4 icon_ink = o.tint != nullptr ? ink : state_ink(st, col::kText2);
    if (solid) {
        // The words go dark on the white; the icon keeps the tint, a little darker to read on it.
        const ImVec4 dark = col::kScreenBg;
        ink = mix(ink, dark, st.selected);
        icon_ink = mix(icon_ink, mix(*o.tint, {0.0f, 0.0f, 0.0f, 1.0f}, 0.25f), st.selected);
    }
    const float cy = face.cy();
    const float icon_size = k.s(tok::kIconSm);
    const auto draw_icon = [&](const char* glyph_text, ImVec2 at, ImVec4 colour) {
        if (o.draw_icon != nullptr) {
            o.draw_icon(k, dl, glyph_text, at, icon_size, colour, o.on);
        } else {
            glyph(k, dl, glyph_text, at, icon_size, colour);
        }
    };
    if (text.empty()) {
        audit::note(id, {face.x0, face.y0}, {face.x1, face.y1});
        if (o.icon != nullptr) {
            draw_icon(o.icon, {face.cx(), cy}, solid ? icon_ink : ink);
        }
        return o.live && tapped(pressed);
    }
    audit::note(id, {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    // The icon, words and trailing icon are one group, centred in the face (`kChipMinW` wide).
    const float pad = k.s(o.fixed ? tok::kS1 : tok::kS3);
    const float lead = o.icon != nullptr ? icon_size + k.s(tok::kS2) : 0.0f;
    const float tail = o.trail != nullptr ? icon_size + k.s(tok::kS1) : 0.0f;
    const float size = k.s(o.label_pt > 0.0f ? o.label_pt : (o.large ? tok::kBody : tok::kLabel));
    ImFont* const font = o.large ? k.bold : k.regular;
    if (!o.second.empty()) {
        return two_lines(k, dl, font, size, face, w - 2.0f * pad, ink, text, o.second) &&
               o.live && tapped(pressed);
    }
    const FitText shown = fit(font, size, text, w - 2.0f * pad - lead - tail);
    const float shown_w = text_size(font, size, shown.c_str()).x;
    float x = face.cx() - (lead + shown_w + tail) * 0.5f;
    if (o.icon != nullptr) {
        draw_icon(o.icon, {x + icon_size * 0.5f, cy}, solid ? icon_ink : state_ink(st, o.tint != nullptr ? base : col::kText2));
        x += lead;
    }
    // Words alone are centred in the face itself, so the audit holds the chip to it; with an icon
    // the group is what is centred, and the words keep their own slot.
    const Box words{x, face.y0, x + shown_w, face.y1};
    label_at(k, dl, font, size, o.icon == nullptr && o.trail == nullptr ? face : words,
             LabelAlign::Centre, ink, shown.c_str());
    if (o.trail != nullptr) {
        glyph(k, dl, o.trail, {x + shown_w + k.s(tok::kS1) + icon_size * 0.5f, cy}, icon_size,
              state_ink(st, col::kText2));
    }
    return o.live && tapped(pressed);
}

void section(Kit& k, const char* text) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(26.0f);
    const float size = k.s(tok::kCaption);
    const ImVec2 ts = text_size(k.bold, size, text);
    text_at(k, dl, k.bold, size, {pos.x + k.s(tok::kS1), pos.y + h - ts.y - k.s(3.0f)}, col::kText3,
            text);
    ImGui::Dummy({1.0f, h});
}

bool cycle_row(Kit& k, const char* icon, const char* label, const char* value, const char* info) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    ImGui::PushID(label);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const float size = k.s(tok::kBody);
    const float pill_w = k.s(68.0f);
    const float pill_h = k.s(tok::kFace);
    float x = pos.x + k.s(tok::kS3) + k.s(tok::kIcon) + k.s(tok::kS3);
    const float room = pos.x + w - k.s(tok::kS3) - pill_w - x - (info ? k.s(kInfoReserve) : 0.0f);
    const RowLabel shown = row_label(k.regular, size, label, room);
    const float label_w = shown.width;
    // The row's tap area leaves the info button's hit square (and a gap) out.
    const float ix = x + label_w + k.s(kInfoOffset);
    const Box info_hit{ix - k.s(tok::kHit) * 0.5f, pos.y, ix + k.s(tok::kHit) * 0.5f, pos.y + h};
    const RowTap tap = row_tap(k, "##row", row, info != nullptr ? &info_hit : nullptr);
    const bool pressed = tap.pressed;
    const State st = tap.state;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, label, row, row, st);
    x = pos.x + k.s(tok::kS3);
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon),
              col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    draw_row_label(k, dl, k.regular, size, {x, pos.y, pos.x + w, pos.y + h}, col::kText, shown);
    note_compact(k, pos, w, pill_w);
    const ImVec2 pa{pos.x + w - k.s(tok::kS3) - pill_w, pos.y + (h - pill_h) * 0.5f};
    // The current choice on a plain face: a tap moves it on, so it is not drawn as selected.
    dl->AddRectFilled(pa, {pa.x + pill_w, pa.y + pill_h}, u32(col::kFill),
                      k.s(tok::kRadiusControl));
    const float csize = k.s(tok::kLabel);
    label_at(k, dl, k.regular, csize, {pa.x, pa.y, pa.x + pill_w, pa.y + pill_h},
             LabelAlign::Centre, col::kText, value);
    if (info != nullptr) {
        info_button(k, info, {ix, pos.y + h * 0.5f});
    }
    ImGui::PopID();
    end_row(pos, {w, h});
    return tapped(pressed);
}

// One choice of a list: its words, a check on the chosen one (`dim`: the words and the check faint,
// for a choice that is not there, even when it is the chosen one), a full-width hit. True when the
// row was tapped: a release over the row the press began on, by the kit's one `tapped` rule (a
// drag along the row or a second finger picks nothing).
bool radio_choice(Kit& k, const char* label, const char* detail, bool on, bool dim) {
    const float w = ImGui::GetContentRegionAvail().x;
    const ImVec2 pos = ImGui::GetCursorScreenPos();
    const float h = k.s(tok::kHit);
    const bool pressed = ImGui::InvisibleButton(label, {w, h});
    // Hover and pressed come from the kit's one state helper, drawn inside the row; the chosen row
    // is told by its accent text and check, as before.
    const State st = item_state(k, false);
    if (column_probe::on()) {
        const RowFrame frame = row_frame(pos.x, w, k.u);
        column_probe::note(frame, RowKind::Block, block_span(frame.plan, k.u), false, true);
    }
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    draw_state(k, dl, label, row, row, st);
    const float size = k.s(tok::kBody);
    const ImVec2 ts = text_size(k.regular, size, label);
    const float x = pos.x + k.s(tok::kS3);
    label_at(k, dl, k.regular, size, {x, row.y0, row.x1, row.y1}, LabelAlign::Left,
             dim ? col::kText3 : (on ? col::kAccent : col::kText), label);
    if (detail != nullptr) {
        const float ds = k.s(tok::kLabel);
        label_at(k, dl, k.regular, ds, {x + ts.x + k.s(tok::kS2), row.y0, row.x1, row.y1},
                 LabelAlign::Left, col::kText3, detail);
    }
    if (on) {
        glyph(k, dl, icon::kCheck, {pos.x + w - k.s(tok::kS3) - k.s(9.0f), pos.y + h * 0.5f},
              k.s(tok::kIcon) * 0.9f, dim ? col::kText3 : col::kAccent);
    }
    return tapped(pressed);
}

// The same for a list where the chosen row does nothing when tapped again: true when a choice
// other than the chosen one was tapped.
bool radio_row(Kit& k, const char* label, const char* detail, bool on, bool dim) {
    return radio_choice(k, label, detail, on, dim) && !on;
}

bool switch_row(Kit& k, const char* icon, const char* label, bool* value, const char* info) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    ImGui::PushID(label);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const float size = k.s(tok::kBody);
    const float sw = k.s(tok::kSwitchW);
    float x = pos.x + k.s(tok::kS3) + k.s(tok::kIcon) + k.s(tok::kS3);
    const float room = pos.x + w - k.s(tok::kS3) - sw - x - (info ? k.s(kInfoReserve) : 0.0f);
    const RowLabel shown = row_label(k.regular, size, label, room);
    const float label_w = shown.width;
    const float ix = x + label_w + k.s(kInfoOffset);
    const Box info_hit{ix - k.s(tok::kHit) * 0.5f, pos.y, ix + k.s(tok::kHit) * 0.5f, pos.y + h};
    const RowTap tap = row_tap(k, "##row", row, info != nullptr ? &info_hit : nullptr);
    const bool pressed = tap.pressed;
    const ImGuiID id = ImGui::GetItemID(); // the switch eases by the row's last part, as before
    const State st = tap.state;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, label, row, row, st);
    x = pos.x + k.s(tok::kS3);
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon),
              col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    draw_row_label(k, dl, k.regular, size, {x, pos.y, pos.x + w, pos.y + h}, col::kText, shown);
    note_compact(k, pos, w, sw);
    switch_knob(k, dl, {pos.x + w - k.s(tok::kS3) - sw, pos.y + (h - k.s(tok::kSwitchH)) * 0.5f},
                *value, id);
    if (info != nullptr) {
        info_button(k, info, {ix, pos.y + h * 0.5f});
    }
    ImGui::PopID();
    end_row(pos, {w, h});
    if (tapped(pressed)) {
        *value = !*value;
        return true;
    }
    return false;
}

bool nav_row(Kit& k, const char* icon, const char* label, std::string_view value,
             const char* info) {
    float w = 0;
    const ImVec2 pos = row_start(&w);
    const float h = k.s(tok::kHit);
    const float size = k.s(tok::kBody);
    const float x0 = pos.x + k.s(tok::kS3) + k.s(tok::kIcon) + k.s(tok::kS3);
    const ImVec2 ls = text_size(k.regular, size, label);
    // An (i) after the label: the row's tap area leaves its hit square out.
    const float ix = x0 + ls.x + k.s(kInfoOffset);
    const Box info_hit{ix - k.s(tok::kHit) * 0.5f, pos.y, ix + k.s(tok::kHit) * 0.5f, pos.y + h};
    ImGui::PushID(label);
    const Box row{pos.x, pos.y, pos.x + w, pos.y + h};
    const RowTap tap = row_tap(k, "##nav", row, info != nullptr ? &info_hit : nullptr);
    const bool pressed = tap.pressed;
    const State st = tap.state;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    draw_state(k, dl, label, row, row, st);
    float x = pos.x + k.s(tok::kS3);
    if (icon != nullptr) {
        glyph(k, dl, icon, {x + k.s(tok::kIcon) * 0.5f, pos.y + h * 0.5f}, k.s(tok::kIcon),
              col::kText2);
    }
    x += k.s(tok::kIcon) + k.s(tok::kS3);
    label_at(k, dl, k.regular, size, {x, pos.y, pos.x + w, pos.y + h}, LabelAlign::Left, col::kText,
             label);
    const float chevron_x = pos.x + w - k.s(tok::kS3) - k.s(tok::kIconSm) * 0.5f;
    note_compact(k, pos, w, k.s(tok::kIconSm));
    glyph(k, dl, icon::kChevron, {chevron_x, pos.y + h * 0.5f}, k.s(tok::kIconSm), col::kText3);
    const float vs = k.s(tok::kLabel);
    const float info_w = info != nullptr ? k.s(kInfoReserve) : 0.0f;
    const float room = chevron_x - k.s(tok::kIconSm) - (x + ls.x + info_w + k.s(tok::kS3));
    const FitText shown = fit(k.regular, vs, value, room);
    label_at(k, dl, k.regular, vs, {x, pos.y, chevron_x - k.s(tok::kIconSm), pos.y + h},
             LabelAlign::Right, col::kText2, shown.c_str());
    if (info != nullptr) {
        info_button(k, info, {ix, pos.y + h * 0.5f});
    }
    ImGui::PopID();
    end_row(pos, {w, h});
    return tapped(pressed);
}

} // namespace etude::app::ui
