// SPDX-License-Identifier: GPL-3.0-or-later
// Where the scale overlay's marks go on one key, in window pixels (no SDL here, so
// tests/scale_marks_layout_test.cpp checks the real numbers at the window sizes): the root key's
// name near its front edge, the finger number clear above it in the open part of the key. A white
// key's open part is the part below the black keys' feet; a black key's is its face above its name.
// The number is as large as the note labels at least, bold, and is dropped (never squeezed) when
// the open part cannot hold it.
#pragma once

#include "etude/screen_layout.hpp"

#include <algorithm>

namespace etude::app {

struct MarkBox {
    float x0 = 0.0f;
    float y0 = 0.0f;
    float x1 = 0.0f;
    float y1 = 0.0f;
    float w() const { return x1 - x0; }
    float h() const { return y1 - y0; }
    float cx() const { return (x0 + x1) * 0.5f; }
    float cy() const { return (y0 + y1) * 0.5f; }
    bool overlaps(const MarkBox& o) const {
        return x0 < o.x1 && o.x0 < x1 && y0 < o.y1 && o.y0 < y1;
    }
};

namespace scale_marks_tok {
inline constexpr float kBlackLabelLine = 0.88f; // a black key's names end this far down its length
inline constexpr float kFrontMargin =
    4.0f;                                 // a white key's names end this many units above its foot
inline constexpr float kInkHeight = 0.8f; // a digit's ink box, in ems
inline constexpr float kInkWidth = 0.7f;
inline constexpr float kBarHeight = 0.17f; // the root bar, in key widths
inline constexpr float kGapUnits = 3.0f;   // the least gap between marks and to an edge
} // namespace scale_marks_tok

struct ScaleMarkKey {
    bool black = false;
    float x = 0.0f; // the key's left edge and width
    float width = 0.0f;
    float top = 0.0f; // the key area's top and bottom
    float bottom = 0.0f;
    float black_length = 0.0f;
    float unit = 1.0f;
    float label_px = 0.0f; // the size of the names the keys may carry (reserved even when unused)
    bool root = false;
    bool has_marker = false; // a C key's marker (name and octave) is its name: the root adds none
    float root_px = 0.0f;
    float finger_px = 0.0f; // the size wanted; 0: no number on this key
};

struct ScaleMarkBoxes {
    bool name_shown = false;
    MarkBox name; // the root's name
    bool finger_shown = false;
    float finger_px = 0.0f; // the size it fits at
    MarkBox finger;         // the digit's ink box
    MarkBox key;            // the part of the key the marks may use
};

inline ScaleMarkBoxes scale_marks_layout(const ScaleMarkKey& k) {
    using namespace scale_marks_tok;
    ScaleMarkBoxes out;
    const float gap = kGapUnits * k.unit;
    const float cx = k.x + 0.5f * k.width;
    const float key_top = k.black ? k.top : k.top + k.black_length;
    const float key_bottom = k.black ? k.top + k.black_length : k.bottom;
    out.key = {k.x, key_top, k.x + k.width, key_bottom};
    const float label_bottom =
        k.black ? k.top + kBlackLabelLine * k.black_length : k.bottom - kFrontMargin * k.unit;
    const float name_px = std::max(k.label_px, k.root && !k.has_marker ? k.root_px : 0.0f);
    const float name_top = label_bottom - name_px;
    if (k.root && !k.has_marker && k.root_px >= kMinLabelFontPx) {
        const float w = std::min(1.3f * k.root_px, k.width);
        out.name = {cx - 0.5f * w, label_bottom - k.root_px, cx + 0.5f * w, label_bottom};
        out.name_shown = true;
    }
    if (k.finger_px <= 0.0f) {
        return out;
    }
    // The open part: under the key's top (and the root's bar), above the names.
    const float bar = k.root ? std::max(2.0f * k.unit, kBarHeight * k.width) + gap : 0.0f;
    const float free_top = key_top + gap + bar;
    const float free_bottom = name_top - gap;
    const float px = std::min(
        {k.finger_px, (free_bottom - free_top) / kInkHeight, (k.width - 2.0f * gap) / kInkWidth});
    if (px < kMinLabelFontPx) {
        return out;
    }
    const float cy = 0.5f * (free_top + free_bottom);
    out.finger = {cx - 0.5f * kInkWidth * px, cy - 0.5f * kInkHeight * px,
                  cx + 0.5f * kInkWidth * px, cy + 0.5f * kInkHeight * px};
    out.finger_px = px;
    out.finger_shown = true;
    return out;
}

} // namespace etude::app
