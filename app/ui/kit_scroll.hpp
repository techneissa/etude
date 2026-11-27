// SPDX-License-Identifier: GPL-3.0-or-later
// Drag to scroll a child window.
#pragma once

#include "kit_core.hpp"

namespace etude::app::ui {

// Drag anywhere to scroll the current child window (touch), except on a slider that took the
// touch. A drag starts after `kTouchSlopPt`; a finger's fast drag coasts on after the lift, its
// speed halving on the recogniser's schedule, and a new touch or either end of the page stops it
// at once. A mouse drag scrolls without coasting.
// `lock_axis`: a sideways drag is left to a row inside the window that scrolls sideways itself.
void drag_scroll(Kit& k, bool lock_axis = false);

} // namespace etude::app::ui
