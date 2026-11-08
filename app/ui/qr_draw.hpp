// SPDX-License-Identifier: GPL-3.0-or-later
// A QR code drawn on the screen, for the pages that show an address to a phone (Add songs and the
// About page's link sheet). The code is made once per address and drawn in whole framebuffer
// pixels, black on a white tile with its quiet zone inside the tile.
#pragma once

#include "kit.hpp"

#include "../qr_code.hpp"

#include <string>

namespace etude::app::ui {

// The code for `url`, made again only when the address changes.
const QrGrid& qr_for(const std::string& url);

// Draws `grid` in black on a white tile at `a`, `side` across: whole framebuffer pixels per module,
// so the squares stay sharp, and the quiet zone round them inside the tile.
void draw_qr(const Kit& k, ImDrawList* dl, ImVec2 a, float side, const QrGrid& grid);

} // namespace etude::app::ui
