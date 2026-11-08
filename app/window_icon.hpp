// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/icon_colour.hpp"

#include <SDL3/SDL.h>

namespace etude::app {

// Sets the app icon on `window` in `colour` from the PNGs built into the binary (256 px, with 64 px
// as the alternate for small sizes). SDL gives a Mac's Dock the same icon. A failure is logged and
// the window keeps the system's default.
void set_window_icon(SDL_Window* window, etude::IconColour colour = etude::kDefaultIconColour);

} // namespace etude::app
