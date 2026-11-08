// SPDX-License-Identifier: GPL-3.0-or-later
#include "kit.hpp"

#include "kit_internal.hpp"

#include "clef_icon.hpp"
#include "face_layout.hpp"
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

namespace {

// Where an item's eased states live in ImGui's storage, apart from the item's own id.
constexpr ImGuiID kSelectedKey = 0x5E1EC7EDu;
constexpr ImGuiID kPressedKey = 0x9E55ED00u;
constexpr ImGuiID kPressedFrameKey = 0x9E55ED01u; // the frame the fade was last written on

} // namespace

Box face_area(const Kit& k, ImVec2 centre, ImVec2 hit) {
    const bool band = k.face_top < k.face_bottom;
    return {centre.x - hit.x * 0.5f, band ? k.face_top : centre.y - hit.y * 0.5f,
            centre.x + hit.x * 0.5f, band ? k.face_bottom : centre.y + hit.y * 0.5f};
}

FacePlan plan_face(const Kit& k, ImVec2 centre, ImVec2 hit, float face_w, float face_h,
                   const char* caption) {
    const Box area = face_area(k, centre, hit);
    FacePlan plan;
    float cw = 0.0f;
    float ch = 0.0f;
    if (caption != nullptr) {
        const float size = k.s(tok::kCaption);
        const float room =
            std::max(0.0f, std::min(centre.x - area.x0, area.x1 - centre.x) - k.s(kFaceInset)) *
            2.0f;
        const float cs = fit_caption_size(size, text_size(k.regular, size, caption).x, room, k.u);
        if (cs > 0.0f) {
            const ImVec2 ts = text_size(k.regular, cs, caption);
            cw = ts.x;
            ch = ts.y;
            plan.caption_size = cs;
        }
    }
    plan.lay = lay_face(area, centre.x, face_w, face_h, k.s(tok::kIcon), cw, ch, k.u);
    if (!plan.lay.caption) {
        plan.caption_size = 0.0f;
    }
    return plan;
}

State item_state(const Kit& k, bool on, bool live, bool disabled, const ImVec4* tint) {
    State s;
    s.disabled = disabled;
    s.tint = tint;
    const ImGuiID id = ImGui::GetItemID();
    const audit::Forced forced = audit::forced_state();
    const bool usable = live && !disabled;
    s.hover = usable && ((!k.touch && ImGui::IsItemHovered()) || forced == audit::Forced::Hover);
    const bool held = usable && (ImGui::IsItemActive() || forced == audit::Forced::Pressed);
    if (forced != audit::Forced::None) {
        // The audit's frame: the forced state alone, and nothing stored that would move after it.
        s.pressed = held ? 1.0f : 0.0f;
        s.selected = forced == audit::Forced::Selected || on ? 1.0f : 0.0f;
        s.focus = forced == audit::Forced::Focus;
        return s;
    }
    ImGuiStorage* st = ImGui::GetStateStorage();
    // A value from a frame this control was not drawn on (a row on a page that was left while it
    // faded) is stale: the control comes back not pressed.
    const int frame = ImGui::GetFrameCount();
    const float was = live_pressed_fade(st->GetFloat(id ^ kPressedKey, 0.0f),
                                        st->GetInt(id ^ kPressedFrameKey, frame - 2), frame);
    // In at once, out over kFast; stored while it moves or holds, with the frame it was written on.
    const float p = held ? 1.0f : (was > 0.0f ? approach(was, 0.0f, k.dt, tok::kFast) : 0.0f);
    if (p != was || p > 0.0f) {
        st->SetFloat(id ^ kPressedKey, p);
        st->SetInt(id ^ kPressedFrameKey, frame);
    }
    s.pressed = p;
    s.selected = tween(id ^ kSelectedKey, on ? 1.0f : 0.0f, tok::kFast);
    s.focus = ImGui::IsItemFocused() && GImGui->NavCursorVisible;
    return s;
}

float face_radius(const Kit& k, const Box& face, float radius) {
    const float r = radius > 0.0f ? radius : k.s(tok::kRadiusControl);
    return std::max(0.0f, std::min(r, std::min(face.w(), face.h()) * 0.5f));
}

void draw_state(const Kit& k, ImDrawList* dl, const char* name, const Box& face, const Box& area,
                const State& s, Idle idle, float radius) {
    audit::face(name, {face.x0, face.y0}, {face.x1, face.y1}, {area.x0, area.y0},
                {area.x1, area.y1}, dl);
    if (!(face.w() > 0.0f) || !(face.h() > 0.0f)) {
        return;
    }
    // On whole pixels, inward, so the edges stay sharp at a fractional scale and never leave the
    // face.
    const float fb = k.fb_scale > 0.0f ? k.fb_scale : 1.0f;
    const ImVec2 a{std::ceil(face.x0 * fb - 0.01f) / fb, std::ceil(face.y0 * fb - 0.01f) / fb};
    const ImVec2 b{std::floor(face.x1 * fb + 0.01f) / fb, std::floor(face.y1 * fb + 0.01f) / fb};
    if (!(b.x > a.x) || !(b.y > a.y)) {
        return;
    }
    const float r = face_radius(k, {a.x, a.y, b.x, b.y}, radius);
    const float dim = s.disabled ? tok::kDisabledAlpha : 1.0f;
    if (idle == Idle::Fill) {
        dl->AddRectFilled(a, b, u32(with_alpha(col::kFill, dim)), r);
    } else if (idle == Idle::Accent) {
        dl->AddRectFilled(a, b, u32(with_alpha(col::kAccent, dim)), r);
    }
    if (s.selected > 0.0f && idle != Idle::Accent) {
        if (s.tint != nullptr) {
            dl->AddRectFilled(a, b, u32(with_alpha(*s.tint, tok::kHandFill * s.selected * dim)), r);
        } else {
            dl->AddRectFilled(a, b, u32(with_alpha(col::kSelected, s.selected * dim)), r);
            // The line inside the face: half its width in from each edge.
            const float line = k.s(tok::kHairline);
            const float in = line * 0.5f;
            dl->AddRect({a.x + in, a.y + in}, {b.x - in, b.y - in},
                        u32(with_alpha(col::kSelectedLine, s.selected * dim)),
                        std::max(0.0f, r - in), 0, line);
        }
    }
    if (s.disabled) {
        return;
    }
    if (s.hover) {
        dl->AddRectFilled(a, b, u32(col::kHover), r);
    }
    if (s.pressed > 0.0f) {
        dl->AddRectFilled(a, b, u32(with_alpha(col::kPressed, s.pressed)), r);
    }
    if (s.focus) {
        const float ring = k.s(tok::kFocusRing);
        const FocusRing plan = focus_ring_plan(b.x - a.x, b.y - a.y, ring, k.s(tok::kHairline));
        if (plan.fill) {
            dl->AddRectFilled(a, b, u32(col::kAccent), r);
        } else {
            const float in = plan.inset;
            dl->AddRect({a.x + in, a.y + in}, {b.x - in, b.y - in}, u32(col::kAccent),
                        std::max(0.0f, r - in), 0, ring);
        }
    }
}

ImVec4 state_ink(const State& s, ImVec4 base, bool on_accent) {
    ImVec4 ink = base;
    if (!on_accent && s.selected > 0.0f) {
        ink = mix(base, s.tint != nullptr ? *s.tint : col::kAccent, s.selected);
    }
    if (s.pressed > 0.0f && !s.disabled) {
        ink = mix(ink, {1.0f, 1.0f, 1.0f, ink.w}, tok::kPressedInk * s.pressed);
    }
    if (s.disabled) {
        ink = with_alpha(ink, tok::kDisabledAlpha);
    }
    return ink;
}

} // namespace etude::app::ui
