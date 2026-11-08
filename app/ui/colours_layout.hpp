// SPDX-License-Identifier: GPL-3.0-or-later
// How the Effects page lays out in the body the sheet gives it, with no ImGui in it, so
// tests/colours_layout_test.cpp checks the rules with the real tokens. Sizes in points; `u` is the
// kit's unit (points per design point).
//
// The page is the live preview and six blocks. Colours: the scheme chips; the palettes (the six
// built-in and the two saved ones); the scheme's swatches. Hit effects: the hit-effect tiles; the
// press rows (Key flash, Held glow, Combo: what playing shows); the screen rows (Trail fade, notes
// rising during a song, Background, High contrast, Reduced effects: how the notes and the screen
// look). The preview is at the top of both halves, so every change shows at once on falling notes
// and keys. While a swatch is being changed the colour control (the saturation/brightness square
// and the hue bar) takes the palettes' place and the quick picks with the recent colours take the
// place of the hit tiles and effect rows (or of the swatches, in the halves); the preview stays on
// screen to show the colour on a falling note, where it fits. The first of these arrangements that
// fits the body's height, with nothing to scroll, in the picking state as well:
// - Whole: two columns, the colours (chips, swatches, then the palettes or the colour control) left
//   and the preview with the effects (or the quick picks) right. The swatches sit under the chips so
//   they stay where they are when the control opens beneath them.
// - Split in two halves, a Colours | Hit effects switch in the header showing one at a time. The
//   effects half is the preview, the hit tiles and the press rows beside the screen rows. The colours
//   half is the chips and palettes (or the control) beside the preview and the swatches (or the
//   quick picks) (`SplitColumns`), or, when that is too tall, the stacked form (`SplitStacked`): the
//   preview across the full width at the top of both halves, and under it the chips, palettes and
//   swatches (Colours) or the hit tiles and press rows beside the screen rows (Hit effects). While
//   picking, the stacked form is the preview (where it fits) over the colour control and the quick
//   picks in two columns with nothing else (no chips or swatches).
#pragma once

#include "tokens.hpp"

#include "etude/palette.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace etude::app::ui {

inline constexpr float kSchemeChipH = tok::kHit + tok::kS2; // icon over its caption
inline constexpr float kSectionH = 26.0f;                   // the kit's section heading
inline constexpr float kPaletteMinW = 150.0f;               // a palette row's name and strip
// Six built-in palettes and the two saved ones: room kept for all eight whichever scheme is shown, so
// the page does not change height with the scheme.
inline constexpr int kPaletteCount = 8;
// The colour control's square is 0.6 as tall as it is wide, within these limits.
inline constexpr float kSquareAspect = 0.6f;
inline constexpr float kSquareMinH = 96.0f;
inline constexpr float kSquareMaxH = 260.0f;
inline constexpr int kRecentRows = static_cast<int>(kRecentColours / kQuickColumns);
inline constexpr int kMostSwatches = 12;
inline constexpr float kPreviewAspect = 0.3f; // height over width
inline constexpr float kPreviewMinH = 72.0f;
inline constexpr float kPreviewMaxH = 160.0f;
inline constexpr float kHitTileH = tok::kHit + tok::kS4; // a picture over its caption
inline constexpr int kHitTiles = 4;
inline constexpr int kSchemeChips = 5;
// Key flash, Held glow and Combo: what playing shows, under the hit tiles.
inline constexpr int kPressRows = 3;
// Trail fade, notes rising during a song, Background, High contrast and Reduced effects.
inline constexpr int kScreenRows = 5;
inline constexpr float kBlockGap = tok::kS2;

enum class ColoursArrangement : std::uint8_t { Whole, SplitColumns, SplitStacked };

// The blocks' sizes at a given width.
inline int palette_columns(float width, float u) {
    return std::clamp(static_cast<int>((width + kBlockGap * u) / ((kPaletteMinW + kBlockGap) * u)),
                      1, 4);
}
inline float palettes_height(float width, float u) {
    const int cols = palette_columns(width, u);
    const int rows = (kPaletteCount + cols - 1) / cols;
    return (kSectionH + tok::kCell * static_cast<float>(rows) - tok::kHitGap) * u;
}
// The colour control: a heading row with Done, the square and the hue bar.
inline float picker_square_height(float width, float u) {
    return std::round(std::clamp(width * kSquareAspect, kSquareMinH * u, kSquareMaxH * u));
}
inline float picker_height(float width, float u) {
    // The heading row, the square and the hue bar, a dead gap between.
    return (2.0f * tok::kHit + 2.0f * tok::kHitGap) * u + picker_square_height(width, u);
}
// The quick picks: a heading and the grid (`kQuickRows` rows of `kQuickColumns` hit areas, stretched
// to the width), then a heading and the recent colours in `kRecentRows` rows of the same cells.
inline float quick_picks_min_width(float u) {
    return (tok::kCell * static_cast<float>(kQuickColumns) - tok::kHitGap) * u;
}
inline float quick_picks_height(float u) {
    return (2.0f * kSectionH +
            tok::kCell * static_cast<float>(static_cast<int>(kQuickRows) + kRecentRows) -
            2.0f * tok::kHitGap) *
           u;
}
inline int swatches_per_row(float width, float u) {
    return std::clamp(static_cast<int>((width + tok::kHitGap * u) / (tok::kCell * u)), 1,
                      kMostSwatches);
}
// A heading row (with Reset), a dead gap and the swatch rows, for twelve swatches.
inline float swatches_height(float width, float u) {
    const int per_row = swatches_per_row(width, u);
    const int rows = (kMostSwatches + per_row - 1) / per_row;
    return (tok::kHit + tok::kCell * static_cast<float>(rows)) * u;
}
inline float preview_height(float width, float u) {
    return std::round(std::clamp(width * kPreviewAspect, kPreviewMinH * u, kPreviewMaxH * u));
}

// `n` rows of controls, a dead gap between them.
inline float control_rows_height(int n, float u) {
    return (tok::kHit * static_cast<float>(n) + tok::kHitGap * static_cast<float>(n - 1)) * u;
}

// A cell the hold menu was opened on, in screen points; the menu goes as soon as the cell is not
// there any more (the page scrolled, or the page changed under it), so the card never sits over
// other cells while it still acts on the first.
struct CellRect {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
};
inline bool cell_moved(const CellRect& was, const CellRect& now, float eps = 0.5f) {
    return std::fabs(was.x0 - now.x0) > eps || std::fabs(was.y0 - now.y0) > eps ||
           std::fabs(was.x1 - now.x1) > eps || std::fabs(was.y1 - now.y1) > eps;
}

struct ColoursLayout {
    ColoursArrangement arrangement = ColoursArrangement::Whole;
    float gap = 0.0f;   // between the columns and between blocks
    float col_w = 0.0f; // a column's width in the two-column arrangements
    // While picking in the stacked arrangement: the colour control's column and the quick picks'
    // (at least `quick_picks_min_width`), side by side across the body.
    float pick_left_w = 0.0f;
    float pick_right_w = 0.0f;
    float chip_h = 0.0f;
    float tiles_h = 0.0f;   // heading and tiles
    // The preview's height: its share of the column's width, and in the halves as tall as the
    // body leaves room for, up to `kPreviewMaxH`.
    float preview_h = 0.0f;
    float press_h = 0.0f;   // the press rows: Key flash, Held glow, Combo
    float screen_h = 0.0f;  // the screen rows
    // Both in one column (the whole page's): the press rows, the dead gap that ends them (the
    // spacing every row leaves under itself) and the screen rows, nothing else between.
    float effects_h = 0.0f;
    // The preview across the top of both stacked halves, one height for both so it does not jump
    // when the switch is tapped (`kPreviewMinH` at least), and with the colour control open (0
    // where it does not fit and is left out).
    float stacked_preview_h = 0.0f;
    float stacked_pick_preview_h = 0.0f;
    float stacked_effects_h = 0.0f; // the stacked effects half: the preview over its two columns
    // The tallest column of each arrangement, with the preview at its share of the width: the
    // height each needs.
    float whole_h = 0.0f;
    float split_columns_h = 0.0f;
    float split_stacked_h = 0.0f;
    float effects_half_h = 0.0f;
    // The tallest column as laid out, the preview grown.
    float height = 0.0f;

    bool split() const { return arrangement != ColoursArrangement::Whole; }
    // The colours blocks' width: a column, or the full width when stacked.
    float colours_w(float body_w) const {
        return arrangement == ColoursArrangement::SplitStacked ? body_w : col_w;
    }
};

// Where the swatches start in the whole page's left column, from the column's top: under the chips,
// whether the colour control is open or not (it opens beneath them).
inline float whole_swatches_top(const ColoursLayout& l, bool /*picking*/) {
    return l.chip_h + l.gap;
}

// The page in a body `w` by `h` points.
inline ColoursLayout colours_layout(float w, float h, float u) {
    ColoursLayout l;
    l.gap = kBlockGap * u;
    l.col_w = std::floor((w - l.gap) * 0.5f);
    l.pick_right_w = std::max(l.col_w, std::ceil(quick_picks_min_width(u)));
    l.pick_left_w = w - l.gap - l.pick_right_w;
    l.chip_h = kSchemeChipH * u;
    l.tiles_h = (kSectionH + kHitTileH) * u;
    l.press_h = control_rows_height(kPressRows, u);
    l.screen_h = control_rows_height(kScreenRows, u);
    l.effects_h = l.press_h + tok::kHitGap * u + l.screen_h;
    const float g = l.gap;
    const float c = l.col_w;
    const float palettes = palettes_height(c, u);
    const float picker = picker_height(c, u);
    const float quicks = quick_picks_height(u);
    const float swatches = swatches_height(c, u);
    const float preview = preview_height(c, u);
    // Two equal columns need room for the quick picks' eight cells.
    const bool columns_fit = c >= quick_picks_min_width(u);

    // Whole: the chips, swatches and palettes (or the control) left; the preview, then the hit
    // tiles and the rows (or the quick picks) right.
    const float whole_left = l.chip_h + g + swatches + g + std::max(palettes, picker);
    const float whole_right = std::max(l.tiles_h + g + l.effects_h, quicks) + g + preview;
    l.whole_h = std::max(whole_left, whole_right);
    // The effects half: the preview, the hit tiles and the press rows beside the screen rows.
    l.effects_half_h = std::max(preview + g + l.tiles_h + g + l.press_h, l.screen_h);
    // The halves: the colours half is the chips and palettes (or the control) beside the preview
    // and the swatches (or the quick picks); stacked, the preview and everything across the width,
    // and while picking the control and the quick picks in two columns under the preview.
    l.split_columns_h = std::max({l.chip_h + g + std::max(palettes, picker),
                                  preview + g + std::max(swatches, quicks), l.effects_half_h});
    const float stacked_closed = l.chip_h + g + palettes_height(w, u) + g + swatches_height(w, u);
    const float stacked_pick = std::max(picker_height(l.pick_left_w, u), quicks);
    // Under the preview the effects half has the tiles and the press rows beside the screen rows.
    const float effects_below = std::max(l.tiles_h + g + l.press_h, l.screen_h);
    const float stacked_body = std::max({stacked_closed, stacked_pick, effects_below});
    l.stacked_preview_h = std::clamp(std::floor(h - g - stacked_body), kPreviewMinH * u,
                                     kPreviewMaxH * u);
    const bool pick_fits = h - g - stacked_pick >= l.stacked_preview_h;
    l.stacked_pick_preview_h = pick_fits ? l.stacked_preview_h : 0.0f;
    const float stacked_h = std::max(
        l.stacked_preview_h + g + stacked_closed,
        stacked_pick + (pick_fits ? l.stacked_preview_h + g : 0.0f));
    l.stacked_effects_h = l.stacked_preview_h + g + effects_below;
    l.split_stacked_h = std::max(stacked_h, l.stacked_effects_h);

    l.preview_h = preview;
    if (columns_fit && l.whole_h <= h) {
        l.arrangement = ColoursArrangement::Whole;
        l.height = l.whole_h;
        return l;
    }
    l.arrangement = columns_fit && l.split_columns_h <= h ? ColoursArrangement::SplitColumns
                                                         : ColoursArrangement::SplitStacked;
    const bool columns = l.arrangement == ColoursArrangement::SplitColumns;
    // A half has room to spare: the preview takes it.
    const float beside = std::max({l.tiles_h + g + l.press_h, columns ? swatches : 0.0f,
                                   columns ? quicks : 0.0f});
    l.preview_h = std::max(preview, std::floor(std::min(kPreviewMaxH * u, h - g - beside)));
    const float effects = std::max(l.preview_h + g + l.tiles_h + g + l.press_h, l.screen_h);
    l.height = columns ? std::max({l.chip_h + g + std::max(palettes, picker),
                                   l.preview_h + g + std::max(swatches, quicks), effects})
                       : l.split_stacked_h;
    return l;
}

// The tallest column of the arrangement in use: never taller than the body where the page fits.
inline float colours_height(const ColoursLayout& l) { return l.height; }

} // namespace etude::app::ui
