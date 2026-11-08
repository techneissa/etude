// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of editor.cpp share. Not part of the app's interface.
#pragma once

#include "editor.hpp"

#include "../frame_log.hpp"
#include "../gfx.hpp"
#include "../touch_device.hpp"
#include "icons.hpp"
#include "sections_ui.hpp"

#include "etude/section_editing.hpp"
#include "etude/section_overview.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace etude::app::ui {

inline constexpr ImVec4 kUnknownHand{0.70f, 0.72f, 0.78f, 1.0f};

inline constexpr ImVec4 kTroubleCut{1.0f, 0.62f, 0.30f, 1.0f};

inline ImVec2 min_of(const RectF& r) { return {r.x, r.y}; }

inline ImVec2 max_of(const RectF& r) { return {r.right(), r.bottom()}; }

inline ImVec4 hand_colour(const Kit& k, Hand hand) {
    return hand == Hand::Left ? k.hand_left : (hand == Hand::Right ? k.hand_right : kUnknownHand);
}

// The fold marker of the falling notes (a small triangle, pointing up for a note raised, down for
// one lowered) on a block that moved by octaves; nothing when the block is too small to hold it.
struct FoldMarker {
    bool shown = false;
    ImVec2 tip, left, right;
};

FoldMarker fold_marker(float x0, float y0, float x1, float y1, int fold);

} // namespace etude::app::ui
