// SPDX-License-Identifier: GPL-3.0-or-later
// A header that opens and closes its group.
#pragma once

#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// The header of a group that opens and closes, in two steps so the group can be sized from its
// state after a tap before anything is drawn. The box at `pos`, `size` big, is the hit; the caller
// draws its label (no wider than `disclosure_label_right`) and the group. The open state is kept
// per `id` (ImGui's state storage), `default_open` until first changed, so every caller has its
// own.
struct Disclosure {
    ImGuiID id = 0;
    bool open = true; // after this frame's tap
    bool held = false;
    bool hover = false;
};
// Reads the tap, flips the stored state on one, and returns it.
Disclosure disclosure_tap(Kit& k, const char* id, ImVec2 pos, ImVec2 size,
                          bool default_open = true);
// Draws the chevron at the right end of the box: pointing right when shut, a quarter turn to down
// when open, turning on a spring.
void disclosure_chevron(Kit& k, const Disclosure& d, ImVec2 pos, ImVec2 size);
// The x a label must stay left of, so it never runs under the chevron.
float disclosure_label_right(const Kit& k, ImVec2 pos, ImVec2 size);

} // namespace etude::app::ui
