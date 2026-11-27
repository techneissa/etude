// SPDX-License-Identifier: GPL-3.0-or-later
// Two lines of text, one over the other.
#pragma once

#include "face_layout.hpp"
#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// Two lines, one over the other, left at `slot.x0`, as one block centred vertically in `slot` (a
// title over its detail). `bottom` may be null for one line.
struct StackLine {
    ImFont* font = nullptr;
    float size = 0.0f;
    ImVec4 colour;
    const char* text = "";
};
// `extra_gap` opens the two lines further apart. `bands`, when given, gets the middle of each line's capital band (what an icon beside the line
// centres on).
struct StackBands {
    float top = 0.0f;
    float bottom = 0.0f;
};
void label_stack(const Kit& k, ImDrawList* dl, const Box& slot, const StackLine& top,
                 const StackLine* bottom, StackBands* bands = nullptr, float extra_gap = 0.0f);

} // namespace etude::app::ui
