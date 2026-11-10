// SPDX-License-Identifier: GPL-3.0-or-later
// Where an icon control's face (the shape its selected, hover and pressed states fill) and its
// caption go. No ImGui in it.
//
// Rules:
// - The face, and so every state drawn on it, stays inside the area the control may draw in (its
//   group's card or the pill it sits on, else its own hit area), `kFaceInset` clear of every edge.
// - A caption sits under the face, never over it, inside the same area and between its cell's
//   sides. The face gives up height first, down to the icon plus `kFaceMinOverIcon`; where the
//   face and the caption still do not fit, the caption is dropped and the face is centred alone.
// - A caption too wide for its cell is drawn smaller, down to `kCaptionMinSize`; below that it is
//   dropped, never cut or run over the cell's sides.
// - A face without a caption is never smaller than its icon plus `kFaceInset` on each side. Where
//   the area is too short for that (a button on a short pill), the face is the area's
//   own band across the cell, drawn in the pill's shape (`band`); where even the area is shorter
//   than the icon, the control is not shown (`hidden`).
#pragma once

#include "tokens.hpp"

#include <cmath>

#include <algorithm>

namespace etude::app::ui {

// A rectangle in ImGui points (any one unit), y down.
struct Box {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;

    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    // `inner` lies inside this box shrunk by `margin` on every side.
    bool holds(const Box& inner, float margin = 0.0f) const {
        return inner.x0 >= x0 + margin && inner.x1 <= x1 - margin && inner.y0 >= y0 + margin &&
               inner.y1 <= y1 - margin;
    }
};

// Design points, scaled by the kit's unit.
inline constexpr float kFaceInset = tok::kS1;   // between a face or caption and the area's edge
inline constexpr float kCaptionGap = 1.0f;      // between a face and its caption
inline constexpr float kFaceMinOverIcon = 4.0f; // the smallest face with a caption: icon + this
inline constexpr float kCaptionMinSize = 10.0f; // a caption shrinks to this before it is dropped

// The focus ring of a face `w` by `h`: a stroke `ring` wide whose middle is `inset` in from the
// edge (`line` clear of it), always inside the face. A face too narrow to hold the stroke is
// filled with the ring's colour instead (`fill`): a thin section of the band is still seen
// focused, and nothing is drawn past it.
struct FocusRing {
    float inset = 0.0f;
    bool fill = false;
};
inline FocusRing focus_ring_plan(float w, float h, float ring, float line) {
    FocusRing r;
    const float half = std::min(w, h) * 0.5f;
    if (half < ring * 0.5f) {
        r.fill = true;
        return r;
    }
    r.inset = std::min(line + ring * 0.5f, half);
    return r;
}

struct FaceLayout {
    Box face;
    bool caption = false; // false: no caption asked for, or it did not fit
    Box caption_box;      // the caption's line box, centred under the face
    bool band = false;    // the face is the area's band: its corners are the area's (h/2)
    bool hidden = false;  // the area is shorter than the icon: nothing is shown
};

// Lays out a face `face_w` by `face_h` (a square icon face passes the same for both; it then stays
// square as it shrinks) centred on `cx` in `area`, with a caption `caption_w` by `caption_h` under
// it (0 for none). `icon` is the icon's size and `u` the kit's unit (points per design point).
inline FaceLayout lay_face(const Box& area, float cx, float face_w, float face_h, float icon,
                           float caption_w, float caption_h, float u) {
    const float inset = kFaceInset * u;
    const bool square = face_w == face_h;
    const float room_w = std::max(0.0f, std::min(cx - area.x0, area.x1 - cx) - inset) * 2.0f;
    const float room_h = std::max(0.0f, area.h() - 2.0f * inset);
    FaceLayout out;
    float w = std::min(face_w, room_w);
    float h = std::min(face_h, room_h);
    bool caption = caption_w > 0.0f && caption_h > 0.0f && caption_w <= room_w;
    if (caption) {
        const float below = kCaptionGap * u + caption_h;
        const float fit_h = std::min(h, room_h - below);
        if (fit_h >= std::min(face_h, icon + kFaceMinOverIcon * u)) {
            h = fit_h;
        } else {
            caption = false;
        }
    }
    if (square) {
        w = h = std::min(w, h);
    }
    if (!caption && std::min(w, h) < icon + 2.0f * inset) {
        // Too short for the icon with room round it: the area's own band, or nothing.
        out.band = true;
        out.hidden = area.h() < icon;
        const float half = std::min({face_w * 0.5f, cx - area.x0, area.x1 - cx});
        out.face = {cx - std::max(half, 0.0f), area.y0, cx + std::max(half, 0.0f), area.y1};
        return out;
    }
    const float stack = h + (caption ? kCaptionGap * u + caption_h : 0.0f);
    // The face's centre is a whole pixel: the area's middle and the stack's height are fractional,
    // and an icon's glyph (its pen is a whole pixel) or arc drawn from a fractional centre sits up
    // to half a pixel off the face, so a skip arrow could be 0.63 px off at x1.37 (#224). The
    // nearest whole pixel that keeps the face and its caption inside the area's inset (the next
    // one in, else the centre as it was).
    const auto snap = [](float centre, float least, float most) {
        for (const float c : {std::round(centre), std::floor(centre), std::ceil(centre)}) {
            if (c >= least && c <= most) {
                return c;
            }
        }
        return centre;
    };
    const float stack_cy = area.cy() - stack * 0.5f + h * 0.5f;
    const float half_x = std::max(w, caption ? caption_w : 0.0f) * 0.5f;
    const float face_cx = snap(cx, area.x0 + inset + half_x, area.x1 - inset - half_x);
    const float face_cy = snap(stack_cy, area.y0 + inset + h * 0.5f,
                               area.y1 - inset - (stack - h * 0.5f));
    const float top = face_cy - h * 0.5f;
    out.face = {face_cx - w * 0.5f, top, face_cx + w * 0.5f, top + h};
    out.caption = caption;
    if (caption) {
        const float cy0 = top + h + kCaptionGap * u;
        out.caption_box = {face_cx - caption_w * 0.5f, cy0, face_cx + caption_w * 0.5f,
                           cy0 + caption_h};
    }
    return out;
}

// The size a caption is drawn at so it fits `room` wide: `size` when its width there (`width`)
// fits, else smaller in proportion, and 0 (drop it) under `kCaptionMinSize`.
inline float fit_caption_size(float size, float width, float room, float u) {
    if (width <= room) {
        return size;
    }
    const float smaller = width > 0.0f ? size * room / width : 0.0f;
    return smaller >= kCaptionMinSize * u ? smaller : 0.0f;
}

} // namespace etude::app::ui
