// SPDX-License-Identifier: GPL-3.0-or-later
// The app icon's pictures built into the binary: every colour's 256 px and 64 px PNG (the window
// icon, the Dock icon, the About page's icon and swatches, and the launcher icon on Linux). The
// full sets (16 to 512) are app/assets/icon/<colour>/; the binary only needs these two.
#pragma once

#include "etude/icon_colour.hpp"

#include <span>

namespace etude::app {

// The PNG of `colour` at `px` (256, or 64; any other size gives the 256 one).
std::span<const unsigned char> icon_png(etude::IconColour colour, int px);

} // namespace etude::app
