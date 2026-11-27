// SPDX-License-Identifier: GPL-3.0-or-later
// The drawn pair of hands.
#pragma once

#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// The drawn left and right hands side by side (the same glyphs as the bar's hand toggles), for a
// row or card that speaks of both. `size` is one hand's; the pair is `hand_pair_width(size)` wide
// and centred on `centre`. `left` and `right` choose which are drawn (one alone sits in its own
// slot, so a row's text starts at the same x either way).
void hand_pair(const Kit& k, ImDrawList* dl, ImVec2 centre, float size, ImVec4 colour,
               bool left = true, bool right = true);
float hand_pair_width(float size);

} // namespace etude::app::ui
