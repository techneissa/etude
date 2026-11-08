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

namespace {

bool g_reduced_motion = false;
// The most a card's shadow reaches past it: `shadow`'s widest layer (2 + 16) and its drop (3 + 6).
constexpr float kShadowReach = 27.0f;
// A card bigger than this square (design points) gets ring shadows (see `shadow`).
constexpr float kRingShadowSide = 320.0f;
// A card this big gets ring shadows and an opaque fill (see `shadow`).
bool ring_shadow(const Kit& k, ImVec2 a, ImVec2 b) {
    return (b.x - a.x) * (b.y - a.y) > k.s(kRingShadowSide) * k.s(kRingShadowSide);
}
ImVec4 to_vec(RGBA c, float a = 1.0f) {
    return {static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f,
            static_cast<float>(c.b) / 255.0f, a};
}
// A tile's block is about half the scene's size, so its rim is half the recipe's width (1080p px)
// in hairlines.
constexpr float kTileRimScale = 0.5f;
// A small block painted from a style's recipe, the way the scene paints one: body gradient,
// inner glow, gloss and streak, rim, bevel. Every style goes through these same steps.
void one_block(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, const StyleRecipe& r, ImVec4 c) {
    const ImVec4 white{1, 1, 1, 1};
    const ImVec4 black{0, 0, 0, 1};
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float radius = std::min(k.s(tok::kS1) * r.corner_scale, 0.5f * w);
    const auto saturate = [](ImVec4 v, float s) {
        const float grey = 0.2126f * v.x + 0.7152f * v.y + 0.0722f * v.z;
        return ImVec4{std::clamp(grey + (v.x - grey) * s, 0.0f, 1.0f),
                      std::clamp(grey + (v.y - grey) * s, 0.0f, 1.0f),
                      std::clamp(grey + (v.z - grey) * s, 0.0f, 1.0f), v.w};
    };
    const ImVec4 sat = saturate(c, r.saturation);
    // The block glow: a few widening halos in the note's colour.
    for (int i = 3; i >= 1 && r.glow_alpha > 0.0f; --i) {
        const float g = k.s(tok::kInset) * static_cast<float>(i) * (0.5f + r.glow_width_share);
        dl->AddRectFilled({a.x - g, a.y - g}, {b.x + g, b.y + g},
                          u32(with_alpha(sat, 0.3f * r.glow_alpha)), radius + g);
    }
    const ImVec4 body = mix(mix(sat, black, r.core_darken), white, r.body_lighten);
    ImVec4 far = mix(body, white, r.top_lighten);
    far = {far.x * r.top_shade, far.y * r.top_shade, far.z * r.top_shade, 1.0f};
    dl->AddRectFilled(a, b, u32(with_alpha(body, r.body_alpha_bottom)), radius);
    // The far end's colour fading down to the body, inside the corners.
    const float top_alpha = std::max(r.body_alpha_top, r.top_lighten + (1.0f - r.top_shade));
    dl->AddRectFilledMultiColor({a.x, a.y + radius}, {b.x, a.y + 0.6f * h},
                                u32(with_alpha(far, top_alpha)), u32(with_alpha(far, top_alpha)),
                                u32(with_alpha(far, 0.0f)), u32(with_alpha(far, 0.0f)));
    if (r.inner_glow_alpha > 0.0f) {
        const float iw = std::max(1.0f, r.inner_width_share * w * 0.5f);
        dl->AddRect({a.x + iw * 0.5f, a.y + iw * 0.5f}, {b.x - iw * 0.5f, b.y - iw * 0.5f},
                    u32(with_alpha(mix(sat, white, 0.35f), r.inner_glow_alpha)),
                    std::max(0.0f, radius - iw * 0.5f), 0, iw);
    }
    if (r.gloss_alpha > 0.0f) {
        const float bw = 0.3f * w;
        const float inset = 0.14f * w;
        dl->AddRectFilledMultiColor({a.x + inset, a.y + inset}, {a.x + inset + bw, b.y - inset},
                                    u32(with_alpha(white, r.gloss_alpha)),
                                    u32(with_alpha(white, r.gloss_alpha)),
                                    u32(with_alpha(white, 0.2f * r.gloss_alpha)),
                                    u32(with_alpha(white, 0.2f * r.gloss_alpha)));
    }
    if (r.specular_alpha > 0.0f) {
        const float x = a.x + 0.22f * w;
        dl->AddLine({x, a.y + std::max(radius, 0.1f * w)},
                    {x, a.y + std::max(radius, 0.1f * w) + r.specular_length * 0.8f * h},
                    u32(with_alpha(white, r.specular_alpha)), std::max(1.0f, 0.08f * w));
    }
    if (r.bevel_alpha > 0.0f) {
        const float bv = std::max(1.0f, r.bevel_share * w);
        dl->AddRectFilled(a, {b.x, a.y + bv}, u32(with_alpha(white, r.bevel_alpha)));
        dl->AddRectFilled({a.x, a.y + bv}, {a.x + bv, b.y}, u32(with_alpha(white, r.bevel_alpha)));
        dl->AddRectFilled({a.x + bv, b.y - bv}, b, u32(with_alpha(black, r.bevel_alpha)));
        dl->AddRectFilled({b.x - bv, a.y + bv}, {b.x, b.y - bv},
                          u32(with_alpha(black, r.bevel_alpha)));
    }
    if (r.rim_alpha > 0.0f && r.rim_width_px > 0.0f) {
        const float rw = std::max(1.0f, k.s(tok::kHairline) * kTileRimScale * r.rim_width_px);
        const ImVec4 rim = mix(mix(sat, white, r.rim_lighten), black, r.rim_darken);
        dl->AddRect({a.x + 0.5f * rw, a.y + 0.5f * rw}, {b.x - 0.5f * rw, b.y - 0.5f * rw},
                    u32(with_alpha(rim, r.rim_alpha)), std::max(0.0f, radius - 0.5f * rw), 0, rw);
    }
}

} // namespace

ImVec4 with_alpha(ImVec4 c, float a) { return {c.x, c.y, c.z, c.w * a}; }

ImVec4 mix(ImVec4 a, ImVec4 b, float t) {
    return {a.x + (b.x - a.x) * t, a.y + (b.y - a.y) * t, a.z + (b.z - a.z) * t,
            a.w + (b.w - a.w) * t};
}

float ease_out(float t) {
    t = std::clamp(t, 0.0f, 1.0f);
    const float inv = 1.0f - t;
    return 1.0f - inv * inv * inv;
}


void set_reduced_motion(bool reduced) { g_reduced_motion = reduced; }

float approach(float p, float target, float dt, float duration) {
    const float step = duration > 0.0f && !g_reduced_motion ? dt / duration : 1.0f;
    return p < target ? std::min(target, p + step) : std::max(target, p - step);
}

float tween(ImGuiID id, float target, float duration) {
    ImGuiStorage* st = ImGui::GetStateStorage();
    // Stored as the start value, the target and the progress, so the ease has a fixed path. By
    // value: a pointer into the storage dies when a later insert grows it.
    const float to = st->GetFloat(id + 1, NAN);
    if (std::isnan(to)) {
        st->SetFloat(id, target);
        st->SetFloat(id + 1, target);
        st->SetFloat(id + 2, 1.0f);
        return target;
    }
    float from = st->GetFloat(id, target);
    float p = st->GetFloat(id + 2, 1.0f);
    if (to != target) {
        from = from + (to - from) * ease_out(p);
        p = 0.0f;
        st->SetFloat(id, from);
        st->SetFloat(id + 1, target);
    }
    p = approach(p, 1.0f, ImGui::GetIO().DeltaTime, duration);
    st->SetFloat(id + 2, p);
    return from + (target - from) * ease_out(p);
}

ImU32 u32(ImVec4 c) { return ImGui::GetColorU32(c); }

void shadow(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float strength) {
    // Layered rounded rects, wider and fainter, offset down: a soft shadow without a blur. Under a
    // big card (the settings sheet) each layer is a ring reaching just under the card's edge, and
    // the card is drawn opaque (`card`): filling all 7 under it would cover a large area 7 times.
    // A small card fills its layers: cheaper to build than rings.
    constexpr int kLayers = 7;
    const bool big = ring_shadow(k, a, b);
    for (int i = kLayers; i >= 1; --i) {
        const float t = static_cast<float>(i) / kLayers;
        const float grow = k.s(2.0f + 16.0f * t);
        const float drop = k.s(3.0f + 6.0f * t);
        const float alpha = 0.055f * strength * (1.0f - t * 0.75f);
        const ImVec2 lo{a.x - grow, a.y - grow + drop};
        const ImVec2 hi{b.x + grow, b.y + grow + drop};
        if (!big) {
            dl->AddRectFilled(lo, hi, u32({0.0f, 0.0f, 0.0f, alpha}), radius + grow);
            continue;
        }
        // From the outer edge in to past the card's edge on every side (the bottom is the widest).
        const float band = std::min(grow + drop + radius, std::min(hi.x - lo.x, hi.y - lo.y) * 0.5f);
        const float half = band * 0.5f;
        dl->AddRect({lo.x + half, lo.y + half}, {hi.x - half, hi.y - half},
                    u32({0.0f, 0.0f, 0.0f, alpha}), std::max(0.0f, radius + grow - half), 0, band);
    }
}

// The clip a `CardClip::Current` card draws in: the list's clip, each edge moved out by the most
// the card's shadow reaches (nothing without one), but never past the clip of the window around the
// current one (a child page's parent) or, for a top-level window, the screen.
static void push_card_clip(const Kit& k, ImDrawList* dl, float shadow_strength) {
    ImVec2 lo = dl->GetClipRectMin();
    ImVec2 hi = dl->GetClipRectMax();
    if (shadow_strength > 0.0f) {
        const float reach = k.s(kShadowReach);
        ImVec2 bound_lo{0.0f, 0.0f};
        ImVec2 bound_hi = ImGui::GetMainViewport()->Size;
        const ImGuiWindow* w = ImGui::GetCurrentWindowRead();
        if (w != nullptr && w->DrawList == dl && w->ParentWindow != nullptr &&
            (w->Flags & ImGuiWindowFlags_ChildWindow) != 0) {
            bound_lo = w->ParentWindow->ClipRect.Min;
            bound_hi = w->ParentWindow->ClipRect.Max;
        }
        lo = {std::min(lo.x, std::max(lo.x - reach, bound_lo.x)),
              std::min(lo.y, std::max(lo.y - reach, bound_lo.y))};
        hi = {std::max(hi.x, std::min(hi.x + reach, bound_hi.x)),
              std::max(hi.y, std::min(hi.y + reach, bound_hi.y))};
    }
    dl->PushClipRect(lo, hi, false);
}

void card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, ImVec4 fill,
          ImDrawFlags corners, float shadow_strength, CardClip clip) {
    if (clip == CardClip::Screen) {
        dl->PushClipRectFullScreen();
    } else {
        push_card_clip(k, dl, shadow_strength);
    }
    if (shadow_strength > 0.0f) {
        shadow(k, dl, a, b, radius, shadow_strength);
        if (ring_shadow(k, a, b)) {
            fill.w = 1.0f; // the shadow is only a ring: nothing under the card to show through
        }
    }
    dl->AddRectFilled(a, b, u32(fill), radius, corners);
    // A faint lighter band at the top edge reads as a lit surface.
    // Not on a disc or a pill: the band is square and would show past the curve.
    if (radius * 2.0f < b.y - a.y - 1.0f) {
        dl->AddRectFilledMultiColor({a.x + radius * 0.5f, a.y + 1.0f},
                                    {b.x - radius * 0.5f, a.y + k.s(tok::kS4)}, u32({1, 1, 1, 0.025f}),
                                    u32({1, 1, 1, 0.025f}), u32({1, 1, 1, 0.0f}),
                                    u32({1, 1, 1, 0.0f}));
    }
    dl->AddRect(a, b, u32(col::kHairline), radius, corners, 1.0f);
    dl->PopClipRect();
}

void note_preview(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, NoteStyle style, RGBA colour,
                  float intensity, int glow) {
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const ImVec4 c = to_vec(colour);
    const ImVec4 dark = mix(c, {0, 0, 0, 1}, 0.32f);
    const StyleRecipe recipe = style_recipe(style, intensity, glow);
    // White-key note: tall, left; black-key note: narrower, shorter, darker and fuller, right.
    one_block(k, dl, {a.x + w * 0.10f, a.y + h * 0.04f}, {a.x + w * 0.50f, b.y - h * 0.04f}, recipe,
              c);
    one_block(k, dl, {a.x + w * 0.60f, a.y + h * 0.28f}, {a.x + w * 0.86f, b.y - h * 0.14f},
              fuller_recipe(recipe), dark);
}

void long_press_tip(Kit& k, const char* label) {
    if (label != nullptr && ImGui::IsItemActive() &&
        ImGui::GetIO().MouseDownDuration[0] > tok::kLongPress) {
        k.tip = label;
        k.tip_min = ImGui::GetItemRectMin();
        k.tip_max = ImGui::GetItemRectMax();
    }
}

} // namespace etude::app::ui
