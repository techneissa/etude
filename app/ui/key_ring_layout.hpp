// SPDX-License-Identifier: GPL-3.0-or-later
// Where the key ring's parts go, in ImGui points. The ring is the twelve keys on a circle in
// fifths: C at the top, G to its right, then D, A, E, B, F#, Db, Ab, Eb, Bb and F, so the keys a
// fifth apart (the ones that share most of their notes) sit side by side. No ImGui here, so
// tests/key_ring_layout_test.cpp checks the real numbers at every window size.
//
// Every key is a round button at least `kHit` across (its face and its hit area are the same
// circle), whatever room the ring has: the ring takes the square it is given and the keys grow with
// it up to `kMaxKey`, never smaller than a hit. A press belongs to the key whose circle it lands in
// (`key_ring_nearest`); neighbouring circles are `kGap` (`tok::kHitGap`, 2 mm) apart, so no two
// overlap and a press between two keys does nothing. When the area is too small for keys of a hit,
// the keys stay a hit and the ring overflows the area (`fits` is false): the screen that hosts it
// must give it `min_side`.
#pragma once

#include "face_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>
#include <cmath>

namespace etude::app::ui {

namespace key_ring_tok {
inline constexpr int kKeys = 12;
inline constexpr float kGap = tok::kHitGap; // least gap between two neighbouring keys
inline constexpr float kMaxKey = 56.0f;   // a key's diameter at most, unless the caller allows more
inline constexpr float kStep = 0.5235988f; // 30 degrees: the angle between two neighbouring keys
inline constexpr float kHalfStepSin = 0.2588190f; // sin(15 degrees)
// The name in the middle: its box is this share of the ring's inner circle tall, as wide as the
// circle is at the box's corners.
inline constexpr float kMiddleHeightShare = 0.9f;
// A key's name is this share of the key's diameter tall (bold), so it grows with the circle.
inline constexpr float kKeyLabelShare = 0.43f;
} // namespace key_ring_tok

struct KeyRingPoint {
    float x = 0.0f;
    float y = 0.0f;
};

struct KeyRingLayout {
    Box bounds;           // the square the ring takes, centred in the area
    float key = 0.0f;     // across: each key's face and hit square
    float radius = 0.0f;  // of the circle through the keys' centres
    std::array<KeyRingPoint, key_ring_tok::kKeys> centre{}; // by pitch class (C = 0)
    Box middle;           // inside the keys: the key's name over its mode
    bool fits = false;    // the keys are at least a hit across
};

// The place of pitch class `pc` on the ring, clockwise from the top (C = 0, G = 1, ... F = 11).
inline int key_ring_place(int pc) { return ((pc % 12 + 12) % 12 * 7) % 12; }
// The pitch class at place `place` (the same map: 7 times 7 is 1 mod 12).
inline int key_ring_pitch_class(int place) { return key_ring_place(place); }

// The side of the square a ring of keys `key_pt` across (design points) needs, so its neighbours
// are `kGap` apart: what a host leaves for it.
inline float key_ring_min_side(float key_pt, float u) {
    const float key = key_pt * u;
    const float gap = key_ring_tok::kGap * u;
    const float radius = (key + gap) / (2.0f * key_ring_tok::kHalfStepSin);
    return 2.0f * radius + key;
}

// The ring in `area`. `max_key_pt`: how large the keys may grow, in design points (a screen with
// room for bigger keys, such as the scale overlay's, passes more).
inline KeyRingLayout key_ring_layout(const Box& area, float u,
                                     float max_key_pt = key_ring_tok::kMaxKey) {
    KeyRingLayout lay;
    const float hit = tok::kHit * u;
    const float gap = key_ring_tok::kGap * u;
    const float side = std::max(0.0f, std::min(area.w(), area.h()));
    // The key that makes neighbours `gap` apart when the ring and its keys fill `side`.
    const float fill = (side * key_ring_tok::kHalfStepSin - gap) / (1.0f + key_ring_tok::kHalfStepSin);
    const float most = std::max(max_key_pt * u, hit);
    lay.key = std::min(fill, most);
    lay.fits = lay.key >= hit - 1e-3f;
    if (!lay.fits) {
        lay.key = hit;
    }
    lay.radius = lay.fits ? (side - lay.key) * 0.5f
                          : (lay.key + gap) / (2.0f * key_ring_tok::kHalfStepSin);
    const float extent = 2.0f * lay.radius + lay.key;
    const float cx = area.cx();
    const float cy = area.cy();
    lay.bounds = {cx - extent * 0.5f, cy - extent * 0.5f, cx + extent * 0.5f, cy + extent * 0.5f};
    for (int pc = 0; pc < key_ring_tok::kKeys; ++pc) {
        const float angle = static_cast<float>(key_ring_place(pc)) * key_ring_tok::kStep;
        lay.centre[static_cast<std::size_t>(pc)] = {cx + lay.radius * std::sin(angle),
                                                    cy - lay.radius * std::cos(angle)};
    }
    // The inner circle, clear of the keys by a gap; the name's box inside it.
    const float inner = std::max(0.0f, lay.radius - lay.key * 0.5f - gap);
    const float half_h = inner * key_ring_tok::kMiddleHeightShare * 0.5f;
    const float half_w = std::sqrt(std::max(0.0f, inner * inner - half_h * half_h));
    lay.middle = {cx - half_w, cy - half_h, cx + half_w, cy + half_h};
    return lay;
}

// The name and the mode in the middle, sized by the room inside the keys so they grow with the ring:
// the name bold at 0.55 of the inner circle's radius (at least the display size, at most 44 points),
// the mode's word bold at 0.24 of it (at least the body size, at most the title size), both shrunk
// together until the block is inside the inner circle with `kMiddlePad` to spare. The block is the
// name's capitals, a gap and the word's capitals, centred on the ring's middle; `cy` are the
// centres of their capital bands. `word_room` is how wide the word may be where it stands (the
// circle's chord there); a longer word shrinks, down to `kModeFloor`, then is cut.
namespace key_ring_tok {
inline constexpr float kNameShare = 0.55f;
inline constexpr float kNameMax = 44.0f;
inline constexpr float kModeShare = 0.24f;
inline constexpr float kModeFloor = tok::kLabel; // the word's least size
inline constexpr float kMiddlePad = tok::kS2;    // between the block and the circle's edge
inline constexpr float kCap = 0.72f;             // capitals' height in ems (a fit estimate)
inline constexpr float kNameWide = 1.2f;         // a name and its sign across, in ems
inline constexpr float kWordGap = 0.45f;         // between the name and the word, in the word's ems
} // namespace key_ring_tok

struct KeyRingMiddle {
    float name_size = 0.0f;
    float mode_size = 0.0f;
    float block_h = 0.0f;
    float name_cy = 0.0f;
    float mode_cy = 0.0f;
    float word_room = 0.0f;
    float radius = 0.0f; // the room: the inner circle less the padding
};

inline KeyRingMiddle key_ring_middle(const KeyRingLayout& lay, float u) {
    using namespace key_ring_tok;
    KeyRingMiddle m;
    const float inner = std::max(0.0f, lay.radius - lay.key * 0.5f - kGap * u);
    m.radius = std::max(0.0f, inner - kMiddlePad * u);
    float name = std::clamp(inner * kNameShare, tok::kDisplay * u, kNameMax * u);
    float mode = std::clamp(inner * kModeShare, tok::kBody * u, tok::kTitle * u);
    // The block shrinks together until its corner (the name's side, the block's top) is inside.
    const auto block = [&](float n, float w) { return n * kCap + w * kWordGap + w * kCap; };
    const float h = block(name, mode);
    const float corner = std::hypot(name * kNameWide * 0.5f, h * 0.5f);
    if (corner > m.radius && corner > 0.0f) {
        const float s = m.radius / corner;
        name *= s;
        mode *= s;
    }
    m.name_size = name;
    m.mode_size = mode;
    m.block_h = block(name, mode);
    const float top = lay.bounds.cy() - m.block_h * 0.5f;
    m.name_cy = top + name * kCap * 0.5f;
    m.mode_cy = top + name * kCap + mode * kWordGap + mode * kCap * 0.5f;
    const float far = m.block_h * 0.5f;
    m.word_room = 2.0f * std::sqrt(std::max(0.0f, m.radius * m.radius - far * far));
    return m;
}

// The pitch class of the key whose circle holds `(x, y)`, or -1 (a press between two keys, in the
// middle or outside the ring). The circles are at least a hit across and a gap apart, so at most
// one holds a point.
inline int key_ring_nearest(const KeyRingLayout& lay, float x, float y) {
    int best = -1;
    float best_d2 = lay.key * lay.key * 0.25f;
    for (int pc = 0; pc < key_ring_tok::kKeys; ++pc) {
        const KeyRingPoint& c = lay.centre[static_cast<std::size_t>(pc)];
        const float d2 = (c.x - x) * (c.x - x) + (c.y - y) * (c.y - y);
        if (d2 <= best_d2) {
            best_d2 = d2;
            best = pc;
        }
    }
    return best;
}

// The key a lift at (`up_x`, `up_y`) fires for a press that landed at (`down_x`, `down_y`): the key
// the press landed on, and only when the finger lifts on that same key (-1: sliding off to another
// key, to a gap or out of the ring cancels it). The caller has checked the press is a tap (inside
// the slop, not a hold).
inline int key_ring_release(const KeyRingLayout& lay, float down_x, float down_y, float up_x,
                            float up_y) {
    const int landed = key_ring_nearest(lay, down_x, down_y);
    return landed == key_ring_nearest(lay, up_x, up_y) ? landed : -1;
}

// A key's square: its face and its hit area.
inline Box key_ring_key_box(const KeyRingLayout& lay, int pc) {
    const KeyRingPoint& c = lay.centre[static_cast<std::size_t>(pc)];
    const float half = lay.key * 0.5f;
    return {c.x - half, c.y - half, c.x + half, c.y + half};
}

} // namespace etude::app::ui
