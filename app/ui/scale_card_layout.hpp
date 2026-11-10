// SPDX-License-Identifier: GPL-3.0-or-later
// Where the scale overlay's popover puts its parts, in ImGui points (no ImGui here). The card hangs
// under the top row: the key ring on the left, as large as the room allows, and on the right the
// fourteen scale types in a grid of three columns by five rows (the last row holds two), then a
// row with the Fingers toggle and the Off control. Every cell is at least a hit across and tall;
// the ring's keys are at least a hit (key_ring_layout.hpp) and grow with the card's height. A type's
// words that do not fit their cell wrap to two lines at the space (`scale_labels`), at the caption
// size when even that is not enough; the words are never abbreviated.
#pragma once

#include "key_ring_layout.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace etude::app::ui {

namespace scale_card_tok {
inline constexpr int kTypes = 14;
inline constexpr int kColumns = 3;
inline constexpr int kRows = 6;             // five of types, one of Fingers and Off
inline constexpr int kRowsTight = 5;        // the types, Fingers and Off sharing the last cell
inline constexpr float kMaxRowH = 56.0f;    // a row stretches to this when the card is taller
inline constexpr float kMaxInnerH = 340.0f; // the card's inside is this tall at most
inline constexpr float kRingKeyMax = 96.0f; // the ring's keys grow to this many points
} // namespace scale_card_tok

struct ScaleCardLayout {
    Box card;
    Box ring;                                       // the square the key ring takes
    std::array<Box, scale_card_tok::kTypes> type{}; // the hit cells, in kAllScaleTypes order
    Box fingers; // two cells wide (half a cell in the tightest card)
    Box off;
    bool tight = false; // five rows: Fingers and Off a cell of a hit each, drawn as icons
    bool fits = false;  // the card is inside the room and the ring's keys are at least a hit
};

namespace scale_card_detail {

// The card with `pad` round its contents down to `bottom`, the grid's cells at most `cell_w` wide
// (less when the ring and the grid would not both fit across `display_w`).
inline ScaleCardLayout build(float display_w, float top, float bottom, float anchor_x, float cell_w,
                             float u, float pad, bool tight) {
    using namespace scale_card_tok;
    ScaleCardLayout lay;
    // The tightest card gives the ring and the grid every point: a hit's dead gap between them and
    // a small margin to the window's edge.
    const float gap = tight ? tok::kHitGap * u : tok::kS3 * u;
    const float edge = tight ? tok::kS1 * u : tok::kS2 * u;
    const float hit = tok::kHit * u;
    const float dead = tok::kHitGap * u; // between two hit areas: each cell gives half to its sides
    const float cell_least = hit + dead;
    const float ring_least = key_ring_min_side(tok::kHit, u);
    const float grid_room = display_w - 2.0f * edge - 2.0f * pad - gap - ring_least;
    const float cell_room = grid_room / static_cast<float>(kColumns);
    // The tightest grid's last row is four cells (two types, Fingers, Off) in the width of three.
    const float cw = std::max(tight ? cell_least * 4.0f / 3.0f : cell_least,
                              std::min(cell_w, cell_room));
    const float grid_w = static_cast<float>(kColumns) * cw;
    const int rows = tight ? kRowsTight : kRows;
    const float least_inner = static_cast<float>(rows) * cell_least;
    const float room_h = std::max(0.0f, bottom - top - 2.0f * pad);
    const float inner_h = std::max(least_inner, std::min(room_h, kMaxInnerH * u));
    const float ring_room_w = display_w - 2.0f * edge - 2.0f * pad - gap - grid_w;
    const float ring_side = std::max(ring_least, std::min(inner_h, ring_room_w));
    const float w = 2.0f * pad + ring_side + gap + grid_w;
    const float h = 2.0f * pad + inner_h;
    const float x = std::clamp(anchor_x - pad, edge, std::max(edge, display_w - w - edge));
    lay.card = {x, top, x + w, top + h};
    const float ring_y = top + pad + (inner_h - ring_side) * 0.5f;
    lay.ring = {x + pad, ring_y, x + pad + ring_side, ring_y + ring_side};
    const float pitch = std::min(inner_h / static_cast<float>(rows), kMaxRowH * u);
    const float grid_x = x + pad + ring_side + gap;
    const float grid_y = top + pad + (inner_h - pitch * static_cast<float>(rows)) * 0.5f;
    // A hit area is its cell less half the dead gap on each side.
    const float half = dead * 0.5f;
    const auto hit_cell = [&](float x0, float y0, float x1, float y1) {
        return Box{x0 + half, y0 + half, x1 - half, y1 - half};
    };
    const float last_y = grid_y + pitch * static_cast<float>(rows - 1);
    lay.tight = tight;
    for (int i = 0; i < kTypes; ++i) {
        const int row = i / kColumns;
        const int col = i % kColumns;
        float x0 = grid_x + cw * static_cast<float>(col);
        float x1 = x0 + cw;
        if (tight && row == rows - 1) {
            // The last row: the two types share what Fingers and Off (a cell each) leave.
            const float type_w = (grid_w - 2.0f * cell_least) * 0.5f;
            x0 = grid_x + type_w * static_cast<float>(col);
            x1 = x0 + type_w;
        }
        lay.type[static_cast<std::size_t>(i)] =
            hit_cell(x0, grid_y + pitch * static_cast<float>(row), x1,
                     grid_y + pitch * static_cast<float>(row + 1));
    }
    if (tight) {
        lay.fingers = hit_cell(grid_x + grid_w - 2.0f * cell_least, last_y,
                               grid_x + grid_w - cell_least, last_y + pitch);
        lay.off = hit_cell(grid_x + grid_w - cell_least, last_y, grid_x + grid_w, last_y + pitch);
    } else {
        lay.fingers = hit_cell(grid_x, last_y, grid_x + 2.0f * cw, last_y + pitch);
        lay.off = hit_cell(grid_x + 2.0f * cw, last_y, grid_x + 3.0f * cw, last_y + pitch);
    }
    lay.fits = lay.card.y1 <= bottom + 1e-3f && lay.card.x0 >= -1e-3f &&
               lay.card.x1 <= display_w + 1e-3f && ring_side <= ring_room_w + 1e-3f;
    return lay;
}

} // namespace scale_card_detail

// `display_w`, `display_h`: the window in points. `top`: where the card starts (under the top row);
// `bottom`: the keyboard's top, down to which the card should end. `anchor_x`: the pill's left
// edge, which the card lines up with. `cell_w`: the grid cells' width (the widest type's words),
// `u`: the kit's unit. When the card does not fit above the keyboard (a small window at a large
// scale), it tightens its padding and may cover the keyboard while it is open, down to the window's
// edge; the cells then narrow (their words end in an ellipsis) and the ring keeps keys of a hit.
// Where even that has no room for six rows (an 800x480 window at 1.5 times), the grid is five rows
// and Fingers and Off share its last cell, an icon each (`tight`).
inline ScaleCardLayout scale_card_layout(float display_w, float display_h, float top, float bottom,
                                         float anchor_x, float cell_w, float u) {
    ScaleCardLayout lay =
        scale_card_detail::build(display_w, top, bottom, anchor_x, cell_w, u, tok::kS3 * u, false);
    if (lay.fits) {
        return lay;
    }
    const float window_bottom = display_h - tok::kS1 * u;
    lay = scale_card_detail::build(display_w, top, window_bottom, anchor_x, cell_w, u, tok::kS1 * u,
                                   false);
    if (lay.fits) {
        return lay;
    }
    // The tightest card hangs straight from the top row and may reach the window's own edge.
    return scale_card_detail::build(display_w, top - tok::kS1 * u, display_h, anchor_x, cell_w, u,
                                    tok::kS1 * u, true);
}

// How the fourteen types' words sit in their cells: the size (design points) and the lines. A
// word that does not fit its cell on one line wraps at its space; if a word still does not fit,
// every label takes the caption size, which all of them then keep.
struct ScaleLabels {
    float pt = tok::kLabel;
    std::array<std::string, scale_card_tok::kTypes> first;
    std::array<std::string, scale_card_tok::kTypes> second; // empty: the words are on one line
    bool fits = true;                                       // every line is inside its cell
};

// `names`: the types' words in cell order. `room(i)`: the room the words have in cell i, in
// pixels. `width(text, pt)`: the width of `text` at `pt` design points, in pixels.
template <class Room, class Width>
ScaleLabels scale_labels(const std::array<std::string_view, scale_card_tok::kTypes>& names,
                         Room room, Width width) {
    ScaleLabels out;
    for (const float pt : {tok::kLabel, tok::kCaption}) {
        out = {};
        out.pt = pt;
        for (std::size_t i = 0; i < names.size(); ++i) {
            const std::string_view name = names[i];
            const float r = room(i);
            out.first[i] = std::string(name);
            if (width(name, pt) <= r) {
                continue;
            }
            const std::size_t sp = name.find(' ');
            if (sp != std::string_view::npos && width(name.substr(0, sp), pt) <= r &&
                width(name.substr(sp + 1), pt) <= r) {
                out.first[i] = std::string(name.substr(0, sp));
                out.second[i] = std::string(name.substr(sp + 1));
            } else {
                out.fits = false;
            }
        }
        if (out.fits) {
            break;
        }
    }
    return out;
}

} // namespace etude::app::ui
