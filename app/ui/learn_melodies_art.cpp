// SPDX-License-Identifier: GPL-3.0-or-later
#include "learn_melodies_art.hpp"

#include "icons.hpp"
#include "music_glyph.hpp"

#include "etude/learn_colours.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app::ui {

namespace {

constexpr float kPi = 3.14159265f;
constexpr int kRingSegments = 48;
constexpr int kMaxBorderPoints = 400;

// The point `s` along a rounded rectangle's outline from the middle of its top, clockwise.
ImVec2 outline_point(ImVec2 a, ImVec2 b, float r, float s) {
    const float w = b.x - a.x;
    const float h = b.y - a.y;
    const float cx = (a.x + b.x) * 0.5f;
    const float arc = kPi * 0.5f * r;
    const float side_h = h - 2.0f * r;
    const float side_w = w - 2.0f * r;
    // The nine pieces in order: half the top, a corner, the right side, a corner, the bottom, a corner,
    // the left side, a corner, the other half of the top.
    const float len[9] = {side_w * 0.5f, arc, side_h, arc, side_w, arc, side_h, arc, side_w * 0.5f};
    int piece = 0;
    while (piece < 8 && s > len[piece]) {
        s -= len[piece++];
    }
    const float t = len[piece] > 0.0f ? std::clamp(s / len[piece], 0.0f, 1.0f) : 0.0f;
    const auto corner = [&](float centre_x, float centre_y, float from) {
        const float angle = from + t * kPi * 0.5f;
        return ImVec2{centre_x + std::cos(angle) * r, centre_y + std::sin(angle) * r};
    };
    switch (piece) {
    case 0: return {cx + t * side_w * 0.5f, a.y};
    case 1: return corner(b.x - r, a.y + r, -kPi * 0.5f);
    case 2: return {b.x, a.y + r + t * side_h};
    case 3: return corner(b.x - r, b.y - r, 0.0f);
    case 4: return {b.x - r - t * side_w, b.y};
    case 5: return corner(a.x + r, b.y - r, kPi * 0.5f);
    case 6: return {a.x, b.y - r - t * side_h};
    case 7: return corner(a.x + r, a.y + r, kPi);
    default: return {a.x + r + t * side_w * 0.5f, a.y};
    }
}

} // namespace

void melody_draw_border(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float thick, float share,
                        ImU32 fill, ImU32 track) {
    const float half = thick * 0.5f;
    const ImVec2 in_a{a.x + half, a.y + half};
    const ImVec2 in_b{b.x - half, b.y - half};
    const float r = std::max(0.0f, std::min(radius - half, std::min(in_b.x - in_a.x, in_b.y - in_a.y) * 0.5f));
    dl->AddRect(in_a, in_b, track, r, 0, thick);
    if (share <= 0.0f) {
        return;
    }
    if (share >= 0.999f) {
        dl->AddRect(in_a, in_b, fill, r, 0, thick);
        return;
    }
    const float w = in_b.x - in_a.x;
    const float h = in_b.y - in_a.y;
    const float total = 2.0f * (w - 2.0f * r) + 2.0f * (h - 2.0f * r) + 2.0f * kPi * r;
    const float length = total * share;
    const int steps = std::clamp(static_cast<int>(length / std::max(thick, 2.0f)), 2, kMaxBorderPoints);
    for (int i = 0; i <= steps; ++i) {
        dl->PathLineTo(outline_point(in_a, in_b, r, length * static_cast<float>(i) / static_cast<float>(steps)));
    }
    dl->PathStroke(fill, 0, thick);
}

void melody_draw_ring(ImDrawList* dl, ImVec2 c, float r, float thick, float share, ImU32 fill,
                      ImU32 track) {
    dl->AddCircle(c, r, track, kRingSegments, thick);
    if (share <= 0.0f) {
        return;
    }
    if (share >= 0.999f) {
        dl->AddCircle(c, r, fill, kRingSegments, thick);
        return;
    }
    const float start = -kPi * 0.5f;
    dl->PathArcTo(c, r, start, start + 2.0f * kPi * share, kRingSegments);
    dl->PathStroke(fill, 0, thick);
}

void melody_draw_fades(const Kit& k, const Box& view, bool more_left, bool more_right) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fade = k.s(learn_tok::kStripFade);
    const ImU32 solid = u32(col::kScreenBg);
    const ImU32 clear = u32(with_alpha(col::kScreenBg, 0.0f));
    if (more_left) {
        dl->AddRectFilledMultiColor({view.x0, view.y0}, {view.x0 + fade, view.y1}, solid, clear, clear, solid);
    }
    if (more_right) {
        dl->AddRectFilledMultiColor({view.x1 - fade, view.y0}, {view.x1, view.y1}, clear, solid, solid, clear);
    }
}

void melody_draw_page_fades(const Kit& k, const Box& body, bool more_up, bool more_down) {
    ImDrawList* dl = ImGui::GetWindowDrawList();
    const float fade = k.s(learn_tok::kStripFade);
    const ImU32 solid = u32(col::kScreenBg);
    const ImU32 clear = u32(with_alpha(col::kScreenBg, 0.0f));
    if (more_up) {
        dl->AddRectFilledMultiColor({body.x0, body.y0}, {body.x1, body.y0 + fade}, solid, solid, clear, clear);
    }
    if (more_down) {
        dl->AddRectFilledMultiColor({body.x0, body.y1 - fade}, {body.x1, body.y1}, clear, clear, solid, solid);
    }
}

// ---- The gallery tile's picture ------------------------------------------------------------------

void melody_draw_picture(const Kit& k, ImDrawList* dl, const Box& box, RGBA own) {
    const ImVec4 own_ink{static_cast<float>(own.r) / 255.0f, static_cast<float>(own.g) / 255.0f,
                        static_cast<float>(own.b) / 255.0f, 1.0f};
    const float w = box.w();
    const float h = box.h();
    // The staff: five lines across the top third, the clef at its left, five notes (E D C D E) after it.
    const float margin = std::min(w, h) * 0.08f;
    const float staff_x0 = box.x0 + margin;
    const float staff_x1 = box.x1 - margin;
    const float staff_h = h * 0.21f;
    const float staff_y0 = box.y0 + h * 0.10f;
    const float space = staff_h / 4.0f;
    const float line = std::max(1.0f, k.s(tok::kHairline));
    for (int i = 0; i < 5; ++i) {
        const float y = staff_y0 + static_cast<float>(i) * space;
        dl->AddLine({staff_x0, y}, {staff_x1, y}, u32(with_alpha(own_ink, 0.45f)), line);
    }
    draw_music_glyph(k, dl, {staff_x0 + space * 1.4f, staff_y0 + staff_h * 0.5f}, 0x1D11E, staff_h * 1.7f,
                     u32(own_ink));
    // E on the bottom line, D under it, C on the ledger line, D, E.
    const float steps[5] = {0.0f, 0.5f, 1.0f, 0.5f, 0.0f};
    const float notes_x0 = staff_x0 + space * 3.6f;
    const float notes_pitch = (staff_x1 - notes_x0) / 5.0f;
    const float head_w = space * 1.25f;
    const float head_h = space * 0.9f;
    const float bottom = staff_y0 + staff_h;
    for (int i = 0; i < 5; ++i) {
        const float cx = notes_x0 + notes_pitch * (static_cast<float>(i) + 0.5f);
        const float cy = bottom + steps[i] * space;
        if (steps[i] >= 1.0f) {
            dl->AddLine({cx - head_w * 0.85f, cy}, {cx + head_w * 0.85f, cy}, u32(with_alpha(own_ink, 0.45f)),
                        line);
        }
        dl->AddEllipseFilled({cx, cy}, {head_w * 0.5f, head_h * 0.5f}, u32(own_ink), -0.35f, 16);
        dl->AddLine({cx + head_w * 0.45f, cy - head_h * 0.1f}, {cx + head_w * 0.45f, cy - space * 3.0f},
                    u32(own_ink), line * 1.5f);
    }
    // The arrow down, then the blocks falling over their keys: the first note's the lowest and lightest.
    const float arrow_y = bottom + space * 2.4f;
    glyph(k, dl, icon::kArrowDown, {box.cx(), arrow_y}, std::min(w, h) * 0.12f, col::kText2);
    const float blocks_y0 = arrow_y + std::min(w, h) * 0.12f;
    const float blocks_y1 = box.y1 - h * 0.04f;
    const float keys_w = (staff_x1 - staff_x0) * 0.45f;
    const float keys_x0 = box.cx() - keys_w * 0.5f;
    const float block_w = keys_w / 3.4f;
    const float block_h = std::min(block_w * 0.7f, (blocks_y1 - blocks_y0) / 5.0f);
    const float key[5] = {2.0f, 1.0f, 0.0f, 1.0f, 2.0f}; // E right, D middle, C left
    for (int i = 0; i < 5; ++i) {
        const float t = static_cast<float>(i) / 4.0f;
        const float x0 = keys_x0 + key[i] * (block_w * 1.2f);
        const float y1 = blocks_y1 - t * (blocks_y1 - blocks_y0 - block_h);
        const ImVec4 shade = mix(own_ink, ImVec4{own_ink.x * 0.5f, own_ink.y * 0.5f, own_ink.z * 0.6f, 1.0f}, t);
        dl->AddRectFilled({x0, y1 - block_h}, {x0 + block_w, y1}, u32(shade), block_h * 0.28f);
    }
}

} // namespace etude::app::ui
