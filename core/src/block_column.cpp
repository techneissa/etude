// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/block_column.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

BlockColumn block_column(const KeyboardLayout& layout, const KeyViewFrame& view, int note,
                         float width_px) {
    BlockColumn c;
    if (!layout.range().contains(note) || !std::isfinite(width_px) || !(width_px > 0.0f)) {
        return c;
    }
    const KeyRect lane = view.to_window(layout.lane(note), layout.white_key_count());
    if (!(lane.width > 0.0f)) {
        return c;
    }
    c.visible = true;
    c.black = lane.black;
    c.lane_x = lane.x * width_px;
    c.lane_w = lane.width * width_px;
    const bool key_width = layout.blocks() == BlockLayout::KeyWidth;
    const float gutter =
        key_width ? 2.0f * c.lane_w * (c.black ? kBlackBlockInset : kWhiteBlockInset)
                  : std::min(std::max(kLaneGutterShare * c.lane_w, kMinLaneGutterPx), c.lane_w);
    c.x = c.lane_x + 0.5f * gutter;
    c.w = c.lane_w - gutter;
    c.layer = c.black ? 1 : 0;
    if (c.black && key_width) {
        c.outline_px = std::max(kBlackBlockOutlineShare * c.w, kMinBlackBlockOutlinePx);
    }
    return c;
}

} // namespace etude
