// SPDX-License-Identifier: GPL-3.0-or-later
// Morphing icons, after the technique of morphicons (MIT License, Copyright (c) 2026 Guillermo,
// https://github.com/guillermolg00/morphicons). The icons themselves are in morph_glyphs.cpp.
#include "morph.hpp"

#include "icon_audit.hpp"
#include "kit.hpp"

#include "etude/icon_motion.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <memory>

namespace etude::app::ui {

namespace {

// Lines and dots from here on are in output points.
std::array<ImVec2, kMaxCurvePoints> g_pts;

// The turn at point i of a polyline of m points: the cosine between the segments in and out.
float turn_cos(int i, int m) {
    const ImVec2 p = g_pts[static_cast<std::size_t>(i)];
    const ImVec2 a = g_pts[static_cast<std::size_t>((i + m - 1) % m)];
    const ImVec2 b = g_pts[static_cast<std::size_t>((i + 1) % m)];
    const float ux = p.x - a.x;
    const float uy = p.y - a.y;
    const float vx = b.x - p.x;
    const float vy = b.y - p.y;
    const float len = std::sqrt((ux * ux + uy * uy) * (vx * vx + vy * vy));
    return len > 0.0f ? (ux * vx + uy * vy) / len : 1.0f;
}

// One stroke the way Lucide draws it (round caps and joins): the polyline, then a dot at each end
// of an open path and at each sharp turn, where ImGui's mitred join would square the corner. A
// turn of more than 90 degrees (a path doubling back between two fingers) also splits the line
// there, since its mitre would shoot out as a spike. With `round` false (a faded stroke) the dots
// are left out: translucent dots over the line would show as darker blotches.
void stroke(ImDrawList* dl, int n, bool closed, ImU32 ink, float width, bool round) {
    // Points closer than a hundredth of a point make no segment; they would pinch the line.
    int m = 0;
    for (int i = 0; i < n; ++i) {
        const ImVec2 p = g_pts[static_cast<std::size_t>(i)];
        if (m > 0) {
            const ImVec2 q = g_pts[static_cast<std::size_t>(m - 1)];
            if (std::fabs(p.x - q.x) + std::fabs(p.y - q.y) < 0.01f) {
                continue;
            }
        }
        g_pts[static_cast<std::size_t>(m++)] = p;
    }
    if (closed && m > 2) {
        const ImVec2 a = g_pts[0];
        const ImVec2 b = g_pts[static_cast<std::size_t>(m - 1)];
        if (std::fabs(a.x - b.x) + std::fabs(a.y - b.y) < 0.01f) {
            --m;
        }
    }
    const float r = width * 0.5f;
    if (m < 2) {
        if (m == 1 && round) {
            dl->AddCircleFilled(g_pts[0], r, ink);
        }
        return;
    }
    constexpr float kSharp = 0.82f; // cos of a 35 degree turn: a dot for a round join
    constexpr float kFold = 0.0f;   // cos of a 90 degree turn: the line splits
    // Where a closed path splits, it is drawn open from its first fold round to the same point.
    int start = 0;
    if (closed && m > 2) {
        for (int i = 0; i < m; ++i) {
            if (turn_cos(i, m) < kFold) {
                start = i;
                closed = false;
                break;
            }
        }
        if (!closed && start > 0) {
            std::rotate(g_pts.begin(), g_pts.begin() + start, g_pts.begin() + m);
        }
        if (!closed && m < static_cast<int>(g_pts.size())) {
            g_pts[static_cast<std::size_t>(m++)] = g_pts[0]; // back round to the fold
        }
    }
    int piece = 0;
    for (int i = 0; i < m; ++i) {
        const bool cap = !closed && (i == 0 || i == m - 1);
        const float c = cap ? 1.0f : turn_cos(i, m);
        const bool fold = !closed && !cap && c < kFold;
        if (round && (cap || fold || c < kSharp)) {
            dl->AddCircleFilled(g_pts[static_cast<std::size_t>(i)], r, ink);
        }
        if (fold || i == m - 1) {
            dl->AddPolyline(g_pts.data() + piece, i - piece + 1, ink, width,
                            closed ? ImDrawFlags_Closed : ImDrawFlags_None);
            piece = i;
        }
    }
}

// The 24-unit grid, centred on `centre`.
struct Mapping {
    ImVec2 centre;
    float unit;      // points per grid unit
    float cs = 1.0f; // the glyph turned about its centre: cos and sin of the angle, clockwise
    float sn = 0.0f;
    ImVec2 at(MorphPoint p) const {
        const float dx = (p.x - 12.0f) * unit;
        const float dy = (p.y - 12.0f) * unit;
        return {centre.x + dx * cs - dy * sn, centre.y + dx * sn + dy * cs};
    }
};

float stroke_width(GlyphStyle g, float size) {
    return std::max(1.5f, g.stroke * size * g.scale / 24.0f);
}

// The exact curves, flattened finely enough for their size on screen.
void draw_curves(ImDrawList* dl, const CubicPath& path, const Mapping& map, ImU32 ink, float width,
                 bool round = true) {
    for (int s = 0; s < path.subpath_count; ++s) {
        const CubicSubpath& sp = path.subpaths[static_cast<std::size_t>(s)];
        int n = 0;
        const auto put = [&](ImVec2 p) {
            if (n < static_cast<int>(g_pts.size())) {
                g_pts[static_cast<std::size_t>(n++)] = p;
            }
        };
        put(map.at(path.cubics[static_cast<std::size_t>(sp.first)].p0));
        for (int j = sp.first; j < sp.first + sp.count; ++j) {
            const Cubic& c = path.cubics[static_cast<std::size_t>(j)];
            const ImVec2 p0 = map.at(c.p0);
            const ImVec2 p1 = map.at(c.c1);
            const ImVec2 p2 = map.at(c.c2);
            const ImVec2 p3 = map.at(c.p3);
            // A line (its controls on the chord) needs no steps between its ends.
            const auto off = [&](ImVec2 q) {
                const float dx = p3.x - p0.x;
                const float dy = p3.y - p0.y;
                const float l = std::sqrt(dx * dx + dy * dy);
                return l > 0.0f ? std::fabs((q.x - p0.x) * dy - (q.y - p0.y) * dx) / l
                                : std::hypot(q.x - p0.x, q.y - p0.y);
            };
            int steps = 1;
            if (off(p1) > 0.05f || off(p2) > 0.05f) {
                const float poly = std::hypot(p1.x - p0.x, p1.y - p0.y) +
                                   std::hypot(p2.x - p1.x, p2.y - p1.y) +
                                   std::hypot(p3.x - p2.x, p3.y - p2.y);
                steps = std::clamp(static_cast<int>(std::ceil(poly / 1.5f)), 2, kMaxCurveSteps);
            }
            for (int i = 1; i <= steps; ++i) {
                const float t = static_cast<float>(i) / static_cast<float>(steps);
                const float u = 1.0f - t;
                const float b0 = u * u * u;
                const float b1 = 3.0f * u * u * t;
                const float b2 = 3.0f * u * t * t;
                const float b3 = t * t * t;
                put({b0 * p0.x + b1 * p1.x + b2 * p2.x + b3 * p3.x,
                     b0 * p0.y + b1 * p1.y + b2 * p2.y + b3 * p3.y});
            }
        }
        stroke(dl, n, sp.closed, ink, width, round);
    }
}

void draw_frame(ImDrawList* dl, const MorphShape& shape, const Mapping& map, ImU32 ink,
                float width) {
    for (int s = 0; s < shape.count; ++s) {
        const MorphSubpath& sp = shape.subpaths[static_cast<std::size_t>(s)];
        for (std::size_t i = 0; i < sp.pts.size(); ++i) {
            g_pts[i] = map.at(sp.pts[i]);
        }
        stroke(dl, kMorphSamples, sp.closed, ink, width, true);
    }
}

// Each morphing control's state, by its ImGui id. The slot drawn longest ago is taken for a new
// control, but never one drawn this frame: with every slot in use this frame, the new control
// shows its icon without animating.
struct Slot {
    ImGuiID key = 0;
    int frame = -10; // the ImGui frame it was last drawn on
    MorphIcon icon;
    TurnIcon turn;
    RollIcon roll;
};
constexpr std::size_t kSlots = 32;
std::array<Slot, kSlots>& slots() {
    static const auto all = std::make_unique<std::array<Slot, kSlots>>();
    return *all;
}

Slot* slot_for(ImGuiID key, int frame) {
    Slot* spare = nullptr;
    for (Slot& s : slots()) {
        if (s.key == key) {
            return &s;
        }
        if (s.frame != frame && (spare == nullptr || s.frame < spare->frame)) {
            spare = &s;
        }
    }
    if (spare != nullptr) {
        spare->key = key;
        spare->frame = -10;
    }
    return spare;
}

} // namespace

void draw_glyph(ImDrawList* dl, MorphGlyph glyph, ImVec2 centre, float size, ImVec4 colour) {
    if (!glyph_ok(glyph) || audit::icons_hidden()) {
        return;
    }
    const GlyphStyle g = glyph_style(glyph);
    const Mapping map{centre, size * g.scale / 24.0f};
    draw_curves(dl, morph_data(glyph).path, map, u32(colour), stroke_width(g, size));
}

namespace {

// Draws `target` as the control `key`'s icon, morphing from what it showed last frame.
void morph_icon(const Kit& k, ImDrawList* dl, ImGuiID key, const MorphIconData& target,
                GlyphStyle g, ImVec2 centre, float size, ImVec4 colour, Spring spring) {
    const int frame = ImGui::GetFrameCount();
    Slot* s = slot_for(key, frame);
    if (s != nullptr && s->frame != frame) {
        // Shown last frame too: animate the change. A control coming back into view, or reduced
        // effects, shows its state as it is.
        s->icon.go(target, !k.reduced_motion && s->frame == frame - 1, spring);
        s->icon.step(k.dt);
        s->frame = frame;
    }
    if (audit::icons_hidden()) {
        return;
    }
    const Mapping map{centre, size * g.scale / 24.0f};
    const ImU32 ink = u32(colour);
    const float width = stroke_width(g, size);
    if (s != nullptr && s->icon.moving()) {
        draw_frame(dl, s->icon.frame(), map, ink, width);
    } else {
        draw_curves(dl, target.path, map, ink, width);
    }
}

} // namespace

void morph_glyph(const Kit& k, ImDrawList* dl, ImGuiID key, MorphGlyph glyph, ImVec2 centre,
                 float size, ImVec4 colour, Spring spring) {
    if (!glyph_ok(glyph)) {
        return;
    }
    morph_icon(k, dl, key, morph_data(glyph), glyph_style(glyph), centre, size, colour, spring);
}

void turn_glyph(const Kit& k, ImDrawList* dl, ImGuiID key, MorphGlyph glyph, float angle,
                ImVec2 centre, float size, ImVec4 colour, Spring spring) {
    if (!glyph_ok(glyph)) {
        return;
    }
    const int frame = ImGui::GetFrameCount();
    Slot* s = slot_for(key, frame);
    float shown = angle;
    if (s != nullptr) {
        if (s->frame != frame) {
            // Shown last frame too: turn. A control coming back into view, or reduced effects,
            // shows its state as it is.
            s->turn.go(angle, !k.reduced_motion && s->frame == frame - 1, spring);
            s->turn.step(k.dt);
            s->frame = frame;
        }
        shown = s->turn.angle();
    }
    if (audit::icons_hidden()) {
        return;
    }
    const GlyphStyle g = glyph_style(glyph);
    const Mapping map{centre, size * g.scale / 24.0f, std::cos(shown), std::sin(shown)};
    draw_curves(dl, morph_data(glyph).path, map, u32(colour), stroke_width(g, size));
}

void roll_number(const Kit& k, ImDrawList* dl, ImGuiID key, int seconds, ImVec2 centre, float size,
                 ImVec4 colour, Spring spring) {
    seconds = std::clamp(seconds, kSkipMin, kSkipMax);
    const int frame = ImGui::GetFrameCount();
    Slot* s = slot_for(key, frame);
    if (s != nullptr && s->frame != frame) {
        s->roll.go(seconds, !k.reduced_motion && s->frame == frame - 1, spring);
        s->roll.step(k.dt);
        s->frame = frame;
    }
    if (audit::icons_hidden()) {
        return;
    }
    const float unit = size / 24.0f;
    const GlyphStyle g{kNumberStroke, 1.0f};
    const float width = stroke_width(g, size);
    if (s == nullptr || !s->roll.moving()) {
        draw_curves(dl, number_data(seconds).path, Mapping{centre, unit}, u32(colour), width);
        return;
    }
    // Rolling: the numbers stay inside the ring, so what has moved out of the window is cut off.
    dl->PushClipRect({centre.x - kNumberWindow.x * unit, centre.y - kNumberWindow.y * unit},
                     {centre.x + kNumberWindow.x * unit, centre.y + kNumberWindow.y * unit}, true);
    for (int i = 0; i < s->roll.count(); ++i) {
        const float at = s->roll.offset(i) * kNumberTravel * unit;
        ImVec4 c = colour;
        c.w *= s->roll.alpha(i);
        draw_curves(dl, number_data(s->roll.layer_value(i)).path,
                    Mapping{{centre.x, centre.y + at}, unit}, u32(c), width, c.w >= 0.999f);
    }
    dl->PopClipRect();
}

} // namespace etude::app::ui
