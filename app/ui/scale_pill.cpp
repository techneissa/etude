// SPDX-License-Identifier: GPL-3.0-or-later
#include "scale_pill.hpp"

#include "icon_audit.hpp"
#include "icons.hpp"
#include "key_ring.hpp"
#include "note_name.hpp"
#include "scale_card_layout.hpp"

#include "etude/scale_overlay.hpp"

#include <imgui_internal.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace etude::app::ui {

namespace {

constexpr const char* kTypeIds[scale_card_tok::kTypes] = {
    "##scale-type-00", "##scale-type-01", "##scale-type-02", "##scale-type-03", "##scale-type-04",
    "##scale-type-05", "##scale-type-06", "##scale-type-07", "##scale-type-08", "##scale-type-09",
    "##scale-type-10", "##scale-type-11", "##scale-type-12", "##scale-type-13"};
constexpr float kPillMaxW = 232.0f; // the pill's width at most, in design points
constexpr float kChevronW = 10.0f;  // the pill's chevron, across and half as tall

struct PillText {
    char name[8] = {};
    const char* mode = "";
    bool off = true;
};

PillText pill_text(const AppSettings& app) {
    PillText t;
    t.off = !app.scale_overlay;
    if (!t.off) {
        key_ring_key_name(app.scale_root, app.scale_type, t.name, sizeof t.name);
        t.mode = scale_type_name(app.scale_type);
    }
    return t;
}

// The widest of the types' words in a grid cell: the chip's own width for it.
float cell_width(const Kit& k) {
    float w = k.s(tok::kChipMinW);
    for (const ScaleType t : kAllScaleTypes) {
        w = std::max(w, chip_width(k, nullptr, scale_type_name(t)) + k.s(tok::kS1));
    }
    return w;
}

// Fingers, in the tightest card, where the chip has no room for the word: the digits it shows.
void draw_digits(const Kit& k, ImDrawList* dl, const char* digits, ImVec2 centre, float size,
                 ImVec4 colour, bool on) {
    (void)on;
    if (audit::icons_hidden()) {
        return; // the audit's frame without icons
    }
    const float h = size * 1.2f;
    label_at(
        k, dl, k.bold, size,
        {centre.x - size * 1.5f, centre.y - h * 0.5f, centre.x + size * 1.5f, centre.y + h * 0.5f},
        LabelAlign::Centre, colour, digits);
}

// A small chevron pointing down, drawn as two strokes.
void chevron_down(const Kit& k, ImDrawList* dl, ImVec2 centre, ImVec4 colour) {
    const float w = k.s(kChevronW) * 0.5f;
    const float h = w * 0.5f;
    const ImVec2 pts[3] = {
        {centre.x - w, centre.y - h}, {centre.x, centre.y + h}, {centre.x + w, centre.y - h}};
    dl->AddPolyline(pts, 3, u32(colour), ImDrawFlags_None, k.s(1.6f));
}

} // namespace

float ScalePill::width(const Kit& k, const AppSettings& app) const {
    const PillText t = pill_text(app);
    const float size = k.s(tok::kBody);
    const float pad = k.s(tok::kS3);
    const float gap = k.s(tok::kS2);
    float words = 0.0f;
    if (t.off) {
        words = text_size(k.bold, size, "Scale").x;
    } else {
        words = note_name_width(k.bold, size, t.name) + k.s(tok::kS1) +
                text_size(k.bold, size, t.mode).x;
    }
    const float w = pad + k.s(tok::kIconSm) + gap + words + gap + k.s(kChevronW) + pad;
    return std::min(std::max(w, k.s(tok::kChipMinW)), k.s(kPillMaxW));
}

bool ScalePill::pill(Kit& k, UiModel& model, const Frame& f, float x, float max_w, bool live) {
    const AppSettings& app = model.app;
    // Whole pixels, so the layer's clip never cuts the hit area; never wider than the room the
    // status area leaves it, and not drawn at all where it cannot be a hit wide.
    const float w = std::min(std::ceil(width(k, app)), std::floor(max_w));
    x = std::round(x);
    const float hit = std::ceil(k.s(tok::kHit));
    if (w < hit) {
        area_ = {};
        return false;
    }
    const float face_h = k.s(tok::kFace);
    const float row_h = f.row_max.y - f.row_min.y;
    const float y = std::round(f.row_min.y + std::max(0.0f, (row_h - hit) * 0.5f));
    if (!begin_layer("##scale-pill-layer", {x, y}, {w, hit})) {
        ImGui::End();
        return false;
    }
    ImGui::BringWindowToDisplayFront(ImGui::GetCurrentWindow());
    pill_x_ = x;
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const Box area{x, y, x + w, y + hit};
    area_ = area;
    const bool pressed = press_area(k, "##scale-pill", area);
    const State st = item_state(k, app.scale_overlay || open_, live);
    const Box face{x, y + (hit - face_h) * 0.5f, x + w, y + (hit + face_h) * 0.5f};
    draw_state(k, dl, "##scale-pill", face, area, st, Idle::Fill, face_h * 0.5f);
    audit::note("##scale-pill", {face.x0, face.y0}, {face.x1, face.y1}, InkKind::Target);
    audit::chip("##scale-pill", {face.x0, face.y0}, {face.x1, face.y1}, true, false);
    const ImVec4 ink = state_ink(st, col::kText);
    const float pad = k.s(tok::kS3);
    const float gap = k.s(tok::kS2);
    const float size = k.s(tok::kBody);
    const float isz = k.s(tok::kIconSm);
    float cx = face.x0 + pad;
    glyph(k, dl, icon::kPiano, {cx + isz * 0.5f, face.cy()}, isz,
          app.scale_overlay ? col::kAccent : col::kText2);
    cx += isz + gap;
    const float right = face.x1 - pad - k.s(kChevronW) - gap;
    const PillText t = pill_text(app);
    if (t.off) {
        const FitText shown = fit(k.bold, size, "Scale", right - cx);
        label_at(k, dl, k.bold, size, {cx, face.y0, right, face.y1}, LabelAlign::Left, ink,
                 shown.c_str());
    } else {
        const float nw = note_name_width(k.bold, size, t.name);
        note_name_at(k, dl, k.bold, size, {cx, face.y0, cx + nw, face.y1}, ink, t.name);
        cx += nw + k.s(tok::kS1);
        const FitText shown = fit(k.bold, size, t.mode, right - cx);
        label_at(k, dl, k.bold, size, {cx, face.y0, right, face.y1}, LabelAlign::Left, ink,
                 shown.c_str());
    }
    chevron_down(k, dl, {face.x1 - pad - k.s(kChevronW) * 0.5f, face.cy()}, ink);
    ImGui::End();
    if (live && tapped(pressed)) {
        open_ = !open_;
    }
    return true;
}

namespace {

// Picking a key or a type turns the overlay on; Off turns it off and Fingers flips its numbers.
void pick_root(AppSettings& app, int pc) {
    app.scale_root = pc;
    app.scale_overlay = true;
}

void pick_type(AppSettings& app, ScaleType type) {
    app.scale_type = type;
    app.scale_overlay = true;
}

// The fourteen type chips: their words wrap at the space where a cell has no room for one line.
template <class Place>
void type_chips(Kit& k, AppSettings& app, const ScaleCardLayout& lay, Place place) {
    const float hit = k.s(tok::kHit);
    const float pad = k.s(tok::kS1); // a fixed chip's side pad: the words' room is the rest
    std::array<std::string_view, scale_card_tok::kTypes> names;
    for (std::size_t i = 0; i < names.size(); ++i) {
        names[i] = scale_type_name(kAllScaleTypes[i]);
    }
    const ScaleLabels words = scale_labels(
        names, [&](std::size_t i) { return std::max(hit, lay.type[i].w()) - 2.0f * pad; },
        [&](std::string_view text, float pt) {
            return text_size(k.regular, k.s(pt), std::string(text).c_str()).x;
        });
    for (std::size_t i = 0; i < names.size(); ++i) {
        const ScaleType type = kAllScaleTypes[i];
        ChipOpts o;
        o.on = app.scale_overlay && app.scale_type == type;
        o.width = std::max(hit, lay.type[i].w());
        o.fixed = true;
        o.label_pt = words.pt;
        o.second = words.second[i];
        if (chip(k, kTypeIds[i], words.first[i], place(lay.type[i]), o)) {
            pick_type(app, type);
        }
    }
}

} // namespace

void ScalePill::popover(Kit& k, UiModel& model, const Frame& f) {
    AppSettings& app = model.app;
    open_p_ = approach(open_p_, open_ ? 1.0f : 0.0f, k.dt, tok::kMove);
    if (open_p_ <= 0.0f) {
        return;
    }
    const float t = ease_out(open_p_);
    const float u = k.u;
    const float top = f.row_max.y + k.s(tok::kS1);
    const ScaleCardLayout lay = scale_card_layout(
        f.display.x, f.display.y, top, f.keyboard_top - k.s(tok::kS2), pill_x_, cell_width(k), u);
    const float slide = (1.0f - t) * -k.s(tok::kS2);
    const ImVec2 at{lay.card.x0, lay.card.y0 + slide};
    ImGui::PushStyleVar(ImGuiStyleVar_Alpha, t);
    if (begin_layer("##scale-card", at, {lay.card.w(), lay.card.h()})) {
        ImDrawList* dl = ImGui::GetWindowDrawList();
        dl->ChannelsSplit(2);
        dl->ChannelsSetCurrent(1);
        KeyRingOpts ring;
        ring.max_key_pt = scale_card_tok::kRingKeyMax;
        const Box ring_area{lay.ring.x0, lay.ring.y0 + slide, lay.ring.x1, lay.ring.y1 + slide};
        const int key =
            key_ring(k, "##scale-ring", ring_area, app.scale_root, app.scale_type, ring);
        if (key >= 0) {
            pick_root(app, key);
        }
        const float hit = k.s(tok::kHit);
        const auto place = [&](const Box& c) { return ImVec2{c.x0, c.cy() + slide - hit * 0.5f}; };
        type_chips(k, app, lay, place);
        ChipOpts fingers;
        fingers.live = scale_has_fingering(app.scale_type);
        fingers.on = app.scale_fingers && fingers.live; // a toggle that does nothing is not "on"
        fingers.width = std::max(hit, lay.fingers.w());
        fingers.fixed = true;
        if (lay.tight) {
            fingers.icon = "123";
            fingers.draw_icon = draw_digits;
        }
        if (chip(k, "##scale-fingers", lay.tight ? "" : "Fingers", place(lay.fingers), fingers) &&
            fingers.live) {
            app.scale_fingers = !app.scale_fingers;
        }
        ChipOpts off;
        off.on = !app.scale_overlay;
        off.width = std::max(hit, lay.off.w());
        off.fixed = true;
        if (lay.tight) {
            off.icon = icon::kClose;
        }
        if (chip(k, "##scale-off", lay.tight ? "" : "Off", place(lay.off), off)) {
            app.scale_overlay = false;
        }
        dl->ChannelsSetCurrent(0);
        const ImVec2 wp = ImGui::GetWindowPos();
        const ImVec2 lo{wp.x, wp.y};
        const ImVec2 hi{wp.x + lay.card.w(), wp.y + lay.card.h()};
        k.toast_avoid({lo.x, lo.y, lay.card.w(), lay.card.h()});
        overlay_card(k, dl, lo, hi, k.s(tok::kRadiusCard));
        dl->ChannelsMerge();
        if (tapped_outside(outside_, open_, f, lo, hi)) {
            open_ = false;
        }
    }
    ImGui::End();
    if (!open_) {
        block_input("##scale-card");
    }
    ImGui::PopStyleVar();
}

} // namespace etude::app::ui
