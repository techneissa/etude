// SPDX-License-Identifier: GPL-3.0-or-later
// A replay's marks over the falling notes (#78): what the player played as a narrow bar on the edge
// of its key's block in the grade's colour (early below the note, late above, with a small arrow),
// the notes nobody played as a hatched outline on the expected block, and the wrong keys as a
// magenta bar with a cross. The block itself and its name stay as they are.
#include "scene_internal.hpp"

#include <algorithm>
#include <cmath>

namespace etude::app {

using namespace scene_detail;

namespace {

// Design points (times the scene's unit).
constexpr float kBorderW = 2.0f;     // a played note's mark: one side of the block's border
constexpr float kArrow = 4.5f;       // the lean arrow's size, tied to the end of that border
constexpr float kWrongStripeW = 5.0f; // a wrong key's bar on its column's right edge
constexpr float kWrongStripeInset = 1.0f;
constexpr float kHatchGap = 7.0f;    // between the hatch lines of a missed note
constexpr float kHatchLine = 1.5f;
constexpr float kRingW = 2.0f;
constexpr float kWrongFill = 0.32f;  // the wrong key's bar, as a share of its colour
constexpr float kCross = 0.5f;       // the cross, as a share of the column's width, at most
constexpr float kCrossMax = 14.0f;
constexpr float kCrossLine = 2.0f;

// A line of width `w` from `a` to `b` as a quad.
void add_line(Mesh& mesh, SDL_FPoint a, SDL_FPoint b, float w, SDL_FColor colour) {
    const float dx = b.x - a.x;
    const float dy = b.y - a.y;
    const float len = std::sqrt(dx * dx + dy * dy);
    if (len <= 0.0f) {
        return;
    }
    const float nx = -dy / len * w * 0.5f;
    const float ny = dx / len * w * 0.5f;
    const SDL_FPoint quad[4] = {{a.x + nx, a.y + ny}, {b.x + nx, b.y + ny}, {b.x - nx, b.y - ny},
                                {a.x - nx, a.y - ny}};
    mesh.quad(quad, colour);
}

// Diagonal lines (rising to the right) across the block `r`, `gap` apart, drawn only where the
// block shows: between `y0` and `y1` (the note area), so a long block costs no more than its
// visible part and the lines keep their place on the block as it scrolls.
void add_hatch(Mesh& mesh, const SDL_FRect& r, float y0, float y1, float gap, float width,
               SDL_FColor colour) {
    const float bottom = r.y + r.h;
    const float t_lo = std::max(0.0f, bottom - y1); // how far up the block the visible part starts
    const float t_hi = std::min(r.h, bottom - y0);  // and ends
    if (r.w <= 0.0f || t_hi <= t_lo || gap <= 0.0f) {
        return;
    }
    // The line `d` to the right of the block's left edge, where it meets the block's bottom: it
    // climbs one pixel for every pixel it goes right. Only the lines that cross the visible part.
    for (float d = std::ceil(-t_hi / gap) * gap; d < r.w - t_lo; d += gap) {
        const float t0 = std::max(t_lo, -d);
        const float t1 = std::min(t_hi, r.w - d);
        if (t1 <= t0) {
            continue;
        }
        add_line(mesh, {r.x + d + t0, bottom - t0}, {r.x + d + t1, bottom - t1}, width, colour);
    }
}

// How far a border `w` wide at a rounded block's edge stays in from the block's top and bottom: the
// corner's curve there.
float corner_inset(float radius, float w) {
    if (radius <= w) {
        return 0.0f;
    }
    const float run = radius - w;
    return radius - std::sqrt(radius * radius - run * run);
}

} // namespace

void Scene::draw_replay_marks(const FrameInput& in, float base_y, float min_height) {
    const RectF& area = layout_.note_area;
    const bool high = settings_.high_contrast;
    for (const ReplayBarMark& m : in.replay_marks) {
        const BlockColumn& c = cols_[m.key & 127];
        if (!c.visible) {
            continue;
        }
        float top = base_y - m.top * area.h;
        const float bottom = base_y - m.bottom * area.h;
        top = std::min(top, bottom - min_height);
        const GradeGradient g = grade_gradient(replay_grade(m.mark), high);
        const SDL_FColor head = to_fcolor(g.top);
        const SDL_FColor foot = to_fcolor(g.bottom);
        if (m.mark == ReplayMark::Missed) {
            // Nobody played it: the expected block, hatched in red where it shows, and outlined.
            const SDL_FRect block{c.x, top, c.w, bottom - top};
            add_hatch(mesh_, block, area.y, base_y, kHatchGap * unit_, kHatchLine * unit_,
                      with_alpha(head, 0.55f));
            mesh_.ring(block, block_radius(c.w), kRingW * unit_, head, with_alpha(foot, 0.85f));
            continue;
        }
        if (m.mark == ReplayMark::Wrong) {
            // A key with no note for it: a bar across its column with a cross on it.
            const SDL_FRect bar{c.x, top, c.w, bottom - top};
            mesh_.rounded_rect(bar, block_radius(c.w), with_alpha(head, kWrongFill),
                               with_alpha(foot, kWrongFill), 0.0f);
            const float stripe = kWrongStripeW * unit_;
            mesh_.rounded_rect(
                {c.x + c.w - stripe - kWrongStripeInset * unit_, top, stripe, bottom - top},
                0.5f * stripe, head, foot, 0.0f);
            const float half = std::min(kCross * c.w, kCrossMax * unit_) * 0.5f;
            const float cx = c.x + 0.5f * c.w;
            const float cy = 0.5f * (top + bottom);
            const float line = kCrossLine * unit_;
            add_line(mesh_, {cx - half, cy - half}, {cx + half, cy + half}, line, kWhite);
            add_line(mesh_, {cx - half, cy + half}, {cx + half, cy - half}, line, kWhite);
            continue;
        }
        // Played: one side of the block's border, on its right edge inside the outline, in the
        // grade's colour; where the block's corners curve it stays clear of them. A small arrow at
        // the end it leans toward.
        const float border = kBorderW * unit_;
        const float inset = std::min(corner_inset(block_radius(c.w), border), 0.5f * (bottom - top));
        const float x = c.x + c.w - border;
        const float y0 = top + inset;
        const float y1 = bottom - inset;
        mesh_.rect({x, y0, border, y1 - y0}, head, foot);
        if (m.lean != 0) {
            const float a = kArrow * unit_;
            const float cx = c.x + c.w - 0.5f * a; // inside the block's edge, over the border
            if (m.lean > 0) { // late: higher on screen, the arrow points up
                mesh_.triangle({cx, y0 - a}, {cx - 0.5f * a, y0}, {cx + 0.5f * a, y0}, head, head,
                               head);
            } else { // early: lower, the arrow points down
                mesh_.triangle({cx, y1 + a}, {cx - 0.5f * a, y1}, {cx + 0.5f * a, y1}, foot, foot,
                               foot);
            }
        }
    }
}

} // namespace etude::app
