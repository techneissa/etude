// SPDX-License-Identifier: GPL-3.0-or-later
#include "card_kit.hpp"

#include "etude/unit_format.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "replay_port.hpp"

#include <imgui_internal.h> // ShadeVertsLinearColorGradientKeepAlpha

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace etude::app::ui {

ImVec2 to_vec(float x, float y) { return {x, y}; }

bool pin_chip(Kit& k, const char* id, const PinInfo& pin, ImVec2 centre, ImVec2 hit) {
    const bool dim = !pin.available || pin.pending;
    const char* label = !pin.available ? "Nothing to pin: no notes were kept"
                        : pin.pinned   ? "Unpin this run"
                                       : "Pin this run: keep its replay";
    if (dim) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * tok::kDisabledAlpha);
    }
    const bool tapped = icon_toggle(k, id, icon::kPin, label, centre, hit, k.s(tok::kFace),
                                    pin.pinned, true);
    if (dim) {
        ImGui::PopStyleVar();
    }
    return tapped;
}

float button_icon_width(const Kit& k, const ButtonIcon& i) {
    return i.font != nullptr ? k.s(tok::kIconSm) : 0.0f;
}

void draw_button_icon(const Kit& k, ImDrawList* dl, const ButtonIcon& i, float x, float cy,
                      ImVec4 ink) {
    const float size = k.s(tok::kIconSm);
    if (i.font != nullptr) {
        glyph(k, dl, i.font, to_vec(x + size * 0.5f, cy), size, ink);
    }
}

// The line under the figure: how this run compares with the last one like it.
void card_comparison_line(const ResultsCard& c, FitText* out) {
    switch (c.comparison) {
    case CardComparison::Unknown:
        out->clear();
        break;
    case CardComparison::FirstTry:
        out->assign("First try like this");
        break;
    case CardComparison::Better:
        out->format("Up %s on last time", format_unit(c.delta_pct, Unit::Percent).c_str());
        break;
    case CardComparison::Worse:
        out->format("Down %s on last time", format_unit(c.delta_pct, Unit::Percent).c_str());
        break;
    case CardComparison::Same:
        out->assign("Same as last time");
        break;
    }
    if (c.new_best) {
        out->append(out->empty() ? "New best" : " \xC2\xB7 new best");
    }
}

// A button of the card: its face in the kit's states (`draw_state`), the icon and the label
// centred.
bool card_button(Kit& k, const char* id, const ButtonIcon& icon, const char* label,
                 const RectF& cell, bool primary, bool enabled) {
    ImGui::SetCursorScreenPos(to_vec(cell.x, cell.y));
    const bool pressed = ImGui::InvisibleButton(id, to_vec(cell.w, cell.h));
    const State st = item_state(k, false, true, !enabled);
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float face_h = std::min(cell.h, k.s(tok::kFace));
    const Box face{cell.x, cell.y + (cell.h - face_h) * 0.5f, cell.right(),
                   cell.y + (cell.h + face_h) * 0.5f};
    const Idle idle = primary ? Idle::Accent : Idle::Fill;
    draw_state(k, dl, id, face, {cell.x, cell.y, cell.right(), cell.bottom()}, st, idle);
    const ImVec4 ink = state_ink(st, primary ? col::kAccentInk : col::kText, primary);
    ImFont* font = primary ? k.bold : k.regular;
    const float tsize = k.s(tok::kLabel);
    const float gap = k.s(tok::kS2);
    const float iw = button_icon_width(k, icon);
    const float avail = cell.w - k.s(tok::kS3) - (iw > 0.0f ? iw + gap : 0.0f);
    const FitText shown = fit(font, tsize, label, avail);
    const ImVec2 ts = text_size(font, tsize, shown.c_str());
    float x = cell.x + (cell.w - (ts.x + (iw > 0.0f ? iw + gap : 0.0f))) * 0.5f;
    float cy = (face.y0 + face.y1) * 0.5f;
    if (iw > 0.0f) {
        // On whole pixels, so the icon's ink sits where its square says whatever the card's scale.
        x = std::round(x);
        cy = std::round(cy);
        draw_button_icon(k, dl, icon, x, cy, ink);
        // The icon's own square: its ink must sit centred in it.
        char note[48];
        std::snprintf(note, sizeof(note), "%s-icon", id);
        const bool play = std::strcmp(icon.font, icon::kPlay) == 0;
        audit::note(note, to_vec(x, cy - iw * 0.5f), to_vec(x + iw, cy + iw * 0.5f),
                    play ? InkKind::PlayTriangle : InkKind::Centred);
        x += iw + gap;
    }
    label_at(k, dl, font, tsize, {x, face.y0, x + ts.x, face.y1}, LabelAlign::Centre, ink,
             shown.c_str());
    return enabled && tapped(pressed);
}

namespace {

ImVec4 vec_of(RGBA c) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, 1.0f};
}

} // namespace

void figure_row_face(const Kit& k, ImDrawList* dl, const RectF& r) {
    const ImVec2 a = to_vec(r.x, r.y);
    const ImVec2 b = to_vec(r.right(), r.bottom());
    if (k.high_contrast) {
        dl->AddRect(a, b, u32(col::kText3), k.s(tok::kRadiusMd), 0, tok::kHairline);
    } else {
        dl->AddRectFilled(a, b, u32(col::kFill), k.s(tok::kRadiusMd));
    }
}

void grade_counts_row(Kit& k, ImDrawList* dl, const RectF& r, const FigureCell* cells, int count) {
    figure_row_face(k, dl, r);
    if (count <= 0) {
        return;
    }
    const float vs = k.s(tok::kBody);
    const float cs = k.s(tok::kCaption);
    const float cell_w = r.w / static_cast<float>(count);
    for (int i = 0; i < count; ++i) {
        FitText value;
        value.format("%d", cells[i].value);
        const ImVec2 vz = text_size(k.bold, vs, value.c_str());
        const FitText caption = fit(k.regular, cs, cells[i].caption, cell_w);
        const ImVec2 cz = text_size(k.regular, cs, caption.c_str());
        const float block = vz.y + cz.y;
        const float x0 = r.x + static_cast<float>(i) * cell_w;
        const float y = r.y + (r.h - block) * 0.5f;
        // The number over its word, each centred in its line's box by its ink.
        const Box value_slot{x0, y, x0 + cell_w, y + vz.y};
        const Box word_slot{x0, y + vz.y, x0 + cell_w, y + vz.y + cz.y};
        // A grade that happened shows in its own colour, the word under it stays; a zero is dim.
        if (cells[i].value > 0) {
            text_gradient(k, dl, k.bold, vs,
                          label_pen(k.bold, vs, value_slot, LabelAlign::Centre, value.c_str()),
                          grade_ink(k, cells[i].grade), value.c_str());
        } else {
            label_at(k, dl, k.bold, vs, value_slot, LabelAlign::Centre, col::kText2, value.c_str());
        }
        label_at(k, dl, k.regular, cs, word_slot, LabelAlign::Centre, col::kText2, caption.c_str());
    }
}

GradeInk grade_ink(const Kit& k, Grade grade) {
    const GradeGradient g = grade_gradient(grade, k.high_contrast);
    return {vec_of(g.top), vec_of(g.bottom)};
}

void text_gradient(const Kit& /*k*/, ImDrawList* dl, ImFont* font, float size, ImVec2 pos,
                   const GradeInk& ink, const char* text) {
    pos = {std::round(pos.x), std::round(pos.y)};
    const int first = dl->VtxBuffer.Size;
    dl->AddText(font, size, pos, u32(ink.top), text);
    // The capitals stand between about a fifth and nine tenths of the size below the line's top.
    ImGui::ShadeVertsLinearColorGradientKeepAlpha(
        dl, first, dl->VtxBuffer.Size, {pos.x, pos.y + size * 0.2f}, {pos.x, pos.y + size * 0.9f},
        u32(ink.top), u32(ink.bottom));
}

} // namespace etude::app::ui
