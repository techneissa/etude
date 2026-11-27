// SPDX-License-Identifier: GPL-3.0-or-later
// The long-press tip and the frame's last pass.
#pragma once

#include "kit_core.hpp"

namespace etude::app::ui {

// While the item just drawn is held past the long-press time, `label` (may be null) shows over it.
void long_press_tip(Kit& k, const char* label);
// Draws the long-press label and the (i) bubble on top of everything. Call once at the end.
void finish_frame(Kit& k);

} // namespace etude::app::ui
