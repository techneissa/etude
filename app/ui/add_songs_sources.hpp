// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's network folder and USB stick: the words on their two tiles, and the two
// views that replace the page's body when a tile is tapped (the network folder's sheet and the
// list of songs on the stick). They draw from ui::SourcesPort's views and call its actions; no file
// is touched here. Laid out by add_songs_layout.hpp.
#pragma once

#include "kit.hpp"
#include "sources_port.hpp"

#include <string>

namespace etude::app::ui {

// The tile's second line. `room` is the width the words have; a long address is dropped for a
// shorter line that fits.
std::string network_tile_detail(Kit& k, const NetworkView& view, float room);
std::string usb_tile_detail(const UsbView& view);

// Draw the view over the page's body, from `origin`, `w` wide and `body_h` tall.
void draw_network_sheet(Kit& k, SourcesPort& port, ImVec2 origin, float w, float body_h);
void draw_usb_list(Kit& k, SourcesPort& port, ImVec2 origin, float w, float body_h);

} // namespace etude::app::ui
