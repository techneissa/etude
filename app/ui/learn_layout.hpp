// SPDX-License-Identifier: GPL-3.0-or-later
// Where the Learn screen's parts go, in ImGui points (its sizes are `learn_tok` in tokens.hpp). No
// ImGui here, so tests/learn_layout_test.cpp checks the real numbers at every window size.
//
// The screen covers everything under the top row. Its two pages, both with a header row (back, the
// page's name, and on the cards the four tabs):
//   gallery   four tiles (Scales and Chords over Ear and Melodies), each a preview over a footer line,
//             in a grid, and the Continue row under them.
//   scales    the scale card, one structure at every size, drawn in a unit that grows with the window.
//             The left column has the scale's name centred over the key picker (the ring of fifths or
//             the twelve keys), with its look switch under it (in the header when the window is too
//             short for both). The column at its right has the fourteen scale types over the
//             keyboard over the foot row: the types in two rows of seven when each fits its words,
//             else one strip that scrolls sideways (chip_strip.hpp); the keyboard as wide as the
//             column, its keys never taller than a natural proportion, the right hand's fingers
//             above it and the left hand's below; the foot row, centred under the keyboard, with
//             the hands' toggles, which choose what Play plays, then Play. The types and the foot
//             are 1.3 times a hit tall, the keyboard giving way.
#pragma once

#include "face_layout.hpp"
#include "learn_card_layout.hpp"
#include "key_ring_layout.hpp"
#include "tokens.hpp"

#include "etude/scale_diagram.hpp"
#include "etude/scales.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace etude::app::ui {

// ---- The gallery ----------------------------------------------------------------------------

struct LearnTileBoxes {
    Box tile;
    Box preview;
    Box icon;    // the footer's icon chip
    Box text;    // the title over its detail
    Box chevron; // the arrow (or the soon mark) at the right
    bool side = false; // the side-by-side form: a small picture at the left of the title, in one row
                       // (no icon chip, no detail line); `icon` is then empty
};

struct LearnGalleryLayout {
    Box header;
    Box back;
    Box title;
    std::array<LearnTileBoxes, kLearnCards> tiles;
    int columns = 0;
    int rows = 0;
    Box cont;     // the Continue row; empty when there is no room for it
    Box cont_icon;
    Box cont_text;
    Box cont_chevron;
};

// The gallery is a 2x2 grid (Scales and Chords over Ear and Melodies) with the
// Continue row under it. A tile is its picture and its title row and no more: the picture's keys keep
// the natural proportion (`kPreviewKeyAspect`, never stretched tall to fill the tile), so the tile is
// as tall as that picture across its width needs, and where the window is too short it is the tile that
// shrinks (the keys with it) before the Continue row goes: keys under `kTilePicGood` tall are not worth
// a Continue row, which is dropped to give them the room (800x480 at the scale 1.0). Where even a picture of
// `kTilePicMin` will not go over the title row (the 7 inch panel's 800x480, where a 12 mm title row leaves
// no room for keys) a tile is one row: the keys at natural proportion at its left (`kSidePicShare` of its
// width at most), then the title and the arrow, a hit tall at least; the Continue row comes back where it
// fits. The grid, with the Continue row, is centred in the room left under the header.
// What the tiles come to for the room under the header: their height, whether they are the one-row
// form and whether the Continue row has room.
struct LearnTileSize {
    float h = 0.0f;
    bool side = false;
    bool with_continue = false;
};

inline LearnTileSize learn_tile_size(float room, float tile_w, float rows, float u) {
    using namespace learn_tok;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    const float between = tok::kS3 * u;
    const float foot = gap + hit + gap; // the title row with the air a tile keeps round it
    // The tile the picture's natural keys across this width make: the keys are drawn straight on the
    // tile (no frame behind them), so it is a gap, the keys and the title row.
    const float natural_h = gap + (tile_w - 2.0f * gap) / 8.0f * kPreviewKeyAspect + foot;
    const float cont_h = kContinueH * u;
    const auto grid_h = [&](float tile_h) { return rows * tile_h + (rows - 1.0f) * between; };
    const auto fits_continue = [&](float tile_h) { return grid_h(tile_h) + between + cont_h <= room; };
    LearnTileSize t;
    t.h = natural_h;
    t.with_continue = fits_continue(t.h);
    if (t.with_continue) {
        return t;
    }
    // Shrink the tiles to make room for the Continue row, if their keys stay large enough to be
    // worth it: a window that cannot has the keys and no Continue row.
    t.h = (room - between - cont_h - (rows - 1.0f) * between) / rows;
    t.with_continue = t.h - gap - foot >= kTilePicGood * u;
    if (t.with_continue) {
        return t;
    }
    const float room_h = (room - (rows - 1.0f) * between) / rows;
    t.h = std::min(natural_h, room_h);
    if (t.h - gap - foot >= kTilePicMin * u) {
        return t;
    }
    // Not even a small picture fits over a title row a hit tall: one row per tile, the keys at its
    // left, as tall as their share of the width makes them; the least a tile is, a hit tall, if
    // that brings the Continue row back.
    t.side = true;
    t.h = std::max(foot, std::min(tile_w * kSidePicShare / 8.0f * kPreviewKeyAspect + 2.0f * gap, room_h));
    t.with_continue = fits_continue(t.h);
    if (!t.with_continue && t.h > foot && fits_continue(foot)) {
        t.h = foot;
        t.with_continue = true;
    }
    return t;
}

// The boxes of one tile at (x0, y0), `w` by `h`.
inline void learn_place_tile(LearnTileBoxes& t, float x0, float y0, float w, float h, bool side, float u) {
    using namespace learn_tok;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    const float between = tok::kS3 * u;
    t.tile = {x0, y0, x0 + w, y0 + h};
    t.side = side;
    if (side) {
        const float cy = y0 + h * 0.5f;
        const float ph = std::min(h - 2.0f * gap, w * kSidePicShare / 8.0f * kPreviewKeyAspect);
        const float pw = ph / kPreviewKeyAspect * 8.0f;
        t.preview = {x0 + between, cy - ph * 0.5f, x0 + between + pw, cy + ph * 0.5f};
        t.icon = {t.preview.x0, cy, t.preview.x0, cy};
        t.chevron = {x0 + w - between - tok::kIcon * u, cy - hit * 0.5f, x0 + w - between, cy + hit * 0.5f};
        t.text = {t.preview.x1 + between, cy - hit * 0.5f, t.chevron.x0 - gap, cy + hit * 0.5f};
        return;
    }
    t.preview = {x0 + gap, y0 + gap, x0 + w - gap, y0 + h - 2.0f * gap - hit};
    const float cy = y0 + h - gap - hit * 0.5f;
    t.icon = {x0 + between, cy - hit * 0.5f, x0 + between + hit, cy + hit * 0.5f};
    t.chevron = {x0 + w - between - hit * 0.5f, cy - hit * 0.5f, x0 + w - between, cy + hit * 0.5f};
    t.text = {t.icon.x1 + between, cy - hit * 0.5f, t.chevron.x0 - gap, cy + hit * 0.5f};
}

inline LearnGalleryLayout learn_gallery_layout(const Box& area, float u) {
    using namespace learn_tok;
    LearnGalleryLayout g;
    const float pad = kPad * u;
    const float hit = tok::kHit * u;
    const float gap = tok::kS2 * u;
    const float between = tok::kS3 * u;
    // The header stands a dead gap under the top row's buttons.
    const float head_y = area.y0 + tok::kHitGap * u;
    g.header = {area.x0, head_y, area.x1, head_y + hit};
    g.back = {area.x0 + gap, head_y, area.x0 + gap + hit, head_y + hit};
    g.title = {g.back.x1, head_y, area.x1 - pad, head_y + hit};
    const float top = g.header.y1 + gap;
    const float bottom = area.y1 - pad;
    g.columns = kGalleryColumns;
    g.rows = (kLearnCards + kGalleryColumns - 1) / kGalleryColumns;
    const float tile_w = (area.w() - 2.0f * pad - static_cast<float>(g.columns - 1) * between) /
                         static_cast<float>(g.columns);
    const float rows = static_cast<float>(g.rows);
    const LearnTileSize size = learn_tile_size(bottom - top, tile_w, rows, u);
    const float tile_h = std::max(size.h, 0.0f);
    const float cont_h = kContinueH * u;
    const float grid_h = rows * tile_h + (rows - 1.0f) * between;
    const float block_h = grid_h + (size.with_continue ? between + cont_h : 0.0f);
    const float y_top = top + std::max(0.0f, (bottom - top - block_h) * 0.5f);
    const auto place = [&](int slot, LearnTileBoxes& t) {
        const float x0 = area.x0 + pad + static_cast<float>(slot % g.columns) * (tile_w + between);
        const float y0 = y_top + static_cast<float>(slot / g.columns) * (tile_h + between);
        learn_place_tile(t, x0, y0, tile_w, tile_h, size.side, u);
    };
    for (int i = 0; i < kLearnCards; ++i) {
        place(i, g.tiles[static_cast<std::size_t>(i)]);
    }
    if (size.with_continue) {
        const float y0 = y_top + grid_h + between;
        g.cont = {area.x0 + pad, y0, area.x1 - pad, y0 + cont_h};
        const float cy = g.cont.cy();
        g.cont_icon = {g.cont.x0 + between, cy - hit * 0.5f, g.cont.x0 + between + hit,
                       cy + hit * 0.5f};
        g.cont_chevron = {g.cont.x1 - between - hit * 0.5f, cy - hit * 0.5f, g.cont.x1 - between,
                          cy + hit * 0.5f};
        g.cont_text = {g.cont_icon.x1 + between, g.cont.y0, g.cont_chevron.x0 - gap, g.cont.y1};
    }
    return g;
}

// ---- The keys of the diagram -----------------------------------------------------------------

// A key's centre across, from the white keys below it: a white key's middle, a black key's on the
// edge between two whites.
inline float learn_key_cx(const Box& keyboard, const DiagramKey& key, int white_keys) {
    const float w = keyboard.w() / static_cast<float>(std::max(white_keys, 1));
    return keyboard.x0 + (static_cast<float>(key.white) + (key.black ? 0.0f : 0.5f)) * w;
}

inline Box learn_key_box(const Box& keyboard, const DiagramKey& key, int white_keys) {
    const float w = keyboard.w() / static_cast<float>(std::max(white_keys, 1));
    const float cx = learn_key_cx(keyboard, key, white_keys);
    if (!key.black) {
        return {cx - w * 0.5f, keyboard.y0, cx + w * 0.5f, keyboard.y1};
    }
    const float bw = w * learn_tok::kBlackShare;
    return {cx - bw * 0.5f, keyboard.y0, cx + bw * 0.5f,
            keyboard.y0 + keyboard.h() * learn_tok::kBlackHeightShare};
}

// A finger's badge: centred over (or under) its key in `row`.
inline Box learn_badge_box(const Box& row, const Box& keyboard, const DiagramKey& key, int white_keys,
                           float badge) {
    const float cx = learn_key_cx(keyboard, key, white_keys);
    return {cx - badge * 0.5f, row.cy() - badge * 0.5f, cx + badge * 0.5f, row.cy() + badge * 0.5f};
}

} // namespace etude::app::ui
