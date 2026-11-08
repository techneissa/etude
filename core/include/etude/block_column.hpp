// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/keyboard.hpp"
#include "etude/keyboard_view.hpp"

namespace etude {

// Where a note's falling block goes across the window, in pixels, and how it is drawn. Every width
// is a share of the keyboard's width through the view (`KeyViewFrame::to_window`), so blocks are
// wide in a wide window and narrow in a narrow one, and follow the view's zoom and pan.

// KeyWidth: the share of the key's width left free on each side of its block.
inline constexpr float kWhiteBlockInset = 0.10f;
inline constexpr float kBlackBlockInset = 0.06f;
// EvenLanes: clear space between neighbouring blocks, as a share of the lane's width, never under
// `kMinLaneGutterPx`: half of it is taken off each side of the block, so two blocks side by side
// never touch.
inline constexpr float kLaneGutterShare = 0.06f;
inline constexpr float kMinLaneGutterPx = 1.0f;
// With KeyWidth, a black key's block lies over the edges of its white neighbours' blocks: it is
// drawn over them with a dark outline this share of its width wide (at least
// `kMinBlackBlockOutlinePx`), inside its own edge, so the overlap reads cleanly.
inline constexpr float kBlackBlockOutlineShare = 0.07f;
inline constexpr float kMinBlackBlockOutlinePx = 1.0f;

struct BlockColumn {
    bool visible = false; // the note is in the view's layout range
    bool black = false;
    float lane_x = 0.0f; // the lane (`KeyboardLayout::lane`) in window pixels, for the key's light,
    float lane_w = 0.0f; // its hit effects and anything else centred over the note
    float x = 0.0f;      // the block: the lane less its inset (KeyWidth) or gutter (EvenLanes)
    float w = 0.0f;
    // Blocks are drawn in layers, 0 first: the white keys' blocks, then the black keys' over them.
    int layer = 0;
    // The dark outline inside the block's edge, 0 for none.
    float outline_px = 0.0f;
};

// The block column of `note` for a keyboard drawn `width_px` wide, in the layout's own block
// layout (`KeyboardLayout::blocks`). A note outside the layout's range is not visible and has no
// width.
BlockColumn block_column(const KeyboardLayout& layout, const KeyViewFrame& view, int note,
                         float width_px);

} // namespace etude
