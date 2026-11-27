// SPDX-License-Identifier: GPL-3.0-or-later
// A pressable area whose face the caller draws.
#pragma once

#include "face_layout.hpp"
#include "kit_core.hpp"

namespace etude::app::ui {

// A pressable area whose face the caller draws (a card or a row, with `item_state` and
// `draw_state` right after); true when it was pressed. The caller notes it for the audit.
bool press_area(Kit& k, const char* id, const Box& area);

} // namespace etude::app::ui
