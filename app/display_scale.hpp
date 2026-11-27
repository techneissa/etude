// SPDX-License-Identifier: GPL-3.0-or-later
// The output pixels per millimetre the frame draws with.
#pragma once

struct SDL_Window;

namespace etude::app {

// Output pixels per millimetre the frame draws with (the panel's real size and the UI scale, see
// display_panel.hpp), as the window sets it each frame; before the first frame, SDL's pixel density
// at 96 dpi.
float display_px_per_mm(SDL_Window* window);
void set_display_px_per_mm(float px_per_mm);

} // namespace etude::app
