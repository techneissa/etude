// SPDX-License-Identifier: GPL-3.0-or-later
// Where the About page's icon colours go, with no ImGui in it. Sizes in pixels (`u` is the kit's unit), from the top left of
// the room; the colours sit straight on the page, with no card behind them.
//
// Rules:
// - One cell for each of the eight colours: the icon over its name, at every size. The whole cell is
//   the hit area, at least `tok::kHit` each way, and no two are closer than `tok::kHitGap`.
// - The cells are as wide as the longest name needs (`name_w`), so no name is cut. They go in one row
//   where all eight fit, else in rows of four, else of two, else one under the other.
// - The player's current title medal, with the title under it, is on the left and its stats on the
//   right (its medal count, the next title and the colour that unlocks), both level with the cells, when they fit beside a row of eight or two rows of four. Where
//   they do not (a 7 inch panel) they are one line above the cells: the medal and its title, then the
//   stats. The medal and its title are one touch target; it opens the Medals view.
// - A line under the cells says what the colour last touched needs, when it is locked; where the words
//   do not fit across the room it is two lines. The stats line above the cells has room for two too.
#pragma once

#include "add_songs_layout.hpp"
#include "tokens.hpp"

#include <algorithm>
#include <array>

namespace etude::app::ui {

namespace picker_tok {
inline constexpr int kCount = 8;
inline constexpr float kSwatch = 40.0f; // the icon drawn in a cell
inline constexpr float kNameH = tok::kCaption * phone_tok::kLineRatio;
inline constexpr float kHintH = tok::kLabel * phone_tok::kLineRatio; // the line under the cells
inline constexpr float kMedal = 56.0f;      // the title medal beside the cells (its disc)
inline constexpr float kMedalLine = 40.0f;  // ... in the line above them
inline constexpr float kTitleH = tok::kLabel * phone_tok::kLineRatio; // the title under the medal
inline constexpr float kStatH = tok::kCaption * phone_tok::kLineRatio; // one stat line
inline constexpr int kStatLines = 3;
// The chosen colour's highlight, in that colour's own palette (icon_colour.hpp): a glow under the
// icon of `kGlowLayers` rounded rects growing `kGlowSpread` points out, each fainter, a faint tint of
// the cell, and a rim that is mostly the colour. The glow stays inside the cell's pad.
inline constexpr int kGlowLayers = 5;
inline constexpr float kGlowSpread = 4.0f;
inline constexpr float kGlowAlpha = 0.14f; // each layer's alpha at the icon's edge; the others fall off outward
inline constexpr float kTintAlpha = tok::kSelectedFill; // the cell's tint
inline constexpr float kRimAlpha = 0.85f;
} // namespace picker_tok

struct IconPickerCell {
    Rect hit;    // the whole cell
    Rect swatch; // the icon
    Rect name;   // the colour's name
};

struct IconPickerSizes {
    float w = 0.0f;       // the room across
    float u = 1.0f;       // the kit's unit
    float name_w = 0.0f;  // the widest colour name at the real font
    float title_w = 0.0f; // the widest title at the real font (the medal's words)
    float stats_w = 0.0f; // the widest stat line at the real font
    bool sides = false;   // the medal and its stats are shown
    bool hint = false;    // lines under the cells are kept for the locked colour touched
    int hint_lines = 1;   // ... one, or two where the words do not fit one
};

struct IconPickerLayout {
    std::array<IconPickerCell, picker_tok::kCount> cells;
    int per_row = 1;
    bool beside = false; // the sides are left and right of the cells, else (when shown) above
    Rect medal_hit;      // the medal and its title: one touch target
    Rect medal;          // the disc
    Rect medal_title;    // its title (under the disc beside the cells, right of it in the line)
    std::array<Rect, picker_tok::kStatLines> stat; // beside: the stat lines, one above the other
    Rect line;           // above: the stats in one line, right of the medal
    Rect hint;           // the line (or two) under the cells; no height unless asked for
    float height = 0.0f; // everything, from the top to the bottom
};

namespace picker_detail {

inline float gap_of(float u) { return std::max(tok::kHitGap, tok::kS2) * u; }

inline float cell_width(const IconPickerSizes& s) {
    return std::max(tok::kHit * s.u, s.name_w + 2.0f * tok::kS1 * s.u);
}

inline float cell_height(float u) {
    return (tok::kS2 + picker_tok::kSwatch + tok::kS1 + picker_tok::kNameH + tok::kS2) * u;
}

inline float grid_width(int per_row, float cell_w, float gap) {
    return static_cast<float>(per_row) * cell_w + static_cast<float>(per_row - 1) * gap;
}

inline float medal_block_width(const IconPickerSizes& s) {
    return std::max({tok::kHit * s.u, picker_tok::kMedal * s.u, s.title_w + 2.0f * tok::kS1 * s.u});
}

inline float medal_block_height(float u) {
    return (tok::kS1 + picker_tok::kMedal + tok::kS1 + picker_tok::kTitleH + tok::kS1) * u;
}

inline float stats_block_width(const IconPickerSizes& s) {
    return s.stats_w + tok::kS1 * s.u;
}

// The cells from `top` in rows of `per_row`, the block centred between `from` and `to`.
inline void place_cells(IconPickerLayout& g, const IconPickerSizes& s, int per_row, float from,
                        float to, float top) {
    const float gap = gap_of(s.u);
    const float cell_w = cell_width(s);
    const float cell_h = cell_height(s.u);
    const float left = from + std::max(0.0f, (to - from - grid_width(per_row, cell_w, gap)) * 0.5f);
    for (int i = 0; i < picker_tok::kCount; ++i) {
        const float x = left + static_cast<float>(i % per_row) * (cell_w + gap);
        const float y = top + static_cast<float>(i / per_row) * (cell_h + gap);
        IconPickerCell& c = g.cells[static_cast<std::size_t>(i)];
        c.hit = {x, y, cell_w, cell_h};
        const float side = picker_tok::kSwatch * s.u;
        c.swatch = {x + (cell_w - side) * 0.5f, y + tok::kS2 * s.u, side, side};
        c.name = {x, c.swatch.bottom() + tok::kS1 * s.u, cell_w, picker_tok::kNameH * s.u};
    }
}

inline float grid_height(int per_row, float u) {
    const int rows = (picker_tok::kCount + per_row - 1) / per_row;
    return static_cast<float>(rows) * cell_height(u) + static_cast<float>(rows - 1) * gap_of(u);
}

// The sides left and right of the cells, level with them. Returns whether they fit.
inline bool place_beside(IconPickerLayout& g, const IconPickerSizes& s) {
    const float gap = tok::kS4 * s.u;
    const float left_w = medal_block_width(s);
    const float right_w = stats_block_width(s);
    for (const int per_row : {picker_tok::kCount, 4}) {
        const float cells_w = grid_width(per_row, cell_width(s), gap_of(s.u));
        if (left_w + right_w + 2.0f * gap + cells_w > s.w) {
            continue;
        }
        g.beside = true;
        g.per_row = per_row;
        const float cells_h = grid_height(per_row, s.u);
        const float medal_h = medal_block_height(s.u);
        const float stats_h = picker_tok::kStatLines * picker_tok::kStatH * s.u;
        const float row_h = std::max({cells_h, medal_h, stats_h});
        place_cells(g, s, per_row, left_w + gap, s.w - right_w - gap, (row_h - cells_h) * 0.5f);
        const float medal_top = (row_h - medal_h) * 0.5f;
        g.medal_hit = {0.0f, medal_top, left_w, medal_h};
        const float d = picker_tok::kMedal * s.u;
        g.medal = {(left_w - d) * 0.5f, medal_top + tok::kS1 * s.u, d, d};
        g.medal_title = {0.0f, g.medal.bottom() + tok::kS1 * s.u, left_w, picker_tok::kTitleH * s.u};
        const float stats_top = (row_h - stats_h) * 0.5f;
        const float lh = picker_tok::kStatH * s.u;
        for (int i = 0; i < picker_tok::kStatLines; ++i) {
            g.stat[static_cast<std::size_t>(i)] = {s.w - right_w, stats_top + static_cast<float>(i) * lh,
                                                   right_w, lh};
        }
        g.height = row_h;
        return true;
    }
    return false;
}

// The sides as one line above the cells, which come below it in rows of eight, four, two or one.
inline void place_above(IconPickerLayout& g, const IconPickerSizes& s) {
    const float line_h = tok::kHit * s.u;
    const float medal_w = line_h + tok::kS2 * s.u + s.title_w + tok::kS1 * s.u;
    g.medal_hit = {0.0f, 0.0f, std::min(medal_w, s.w), line_h};
    const float d = picker_tok::kMedalLine * s.u;
    g.medal = {(line_h - d) * 0.5f, (line_h - d) * 0.5f, d, d};
    g.medal_title = {line_h + tok::kS2 * s.u, 0.0f, s.title_w + tok::kS1 * s.u, line_h};
    const float x = g.medal_hit.right() + tok::kS3 * s.u;
    g.line = {x, 0.0f, std::max(0.0f, s.w - x), line_h};
}

} // namespace picker_detail

inline IconPickerLayout icon_picker(const IconPickerSizes& s) {
    IconPickerLayout g;
    float top = 0.0f;
    if (!s.sides || !picker_detail::place_beside(g, s)) {
        if (s.sides) {
            picker_detail::place_above(g, s);
            top = g.medal_hit.bottom() + picker_detail::gap_of(s.u);
        }
        const float cell_w = picker_detail::cell_width(s);
        const float gap = picker_detail::gap_of(s.u);
        for (const int candidate : {picker_tok::kCount, 4, 2}) {
            if (picker_detail::grid_width(candidate, cell_w, gap) <= s.w) {
                g.per_row = candidate;
                break;
            }
        }
        picker_detail::place_cells(g, s, g.per_row, 0.0f, s.w, top);
        g.height = top + picker_detail::grid_height(g.per_row, s.u);
    }
    if (s.hint) {
        g.hint = {0.0f, g.height + tok::kS1 * s.u, s.w,
                  static_cast<float>(std::max(1, s.hint_lines)) * picker_tok::kHintH * s.u};
        g.height = g.hint.bottom();
    }
    return g;
}

} // namespace etude::app::ui
