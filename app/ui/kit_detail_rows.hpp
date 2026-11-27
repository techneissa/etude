// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's rows: the Phone switch and a tile that leads to a place.
#pragma once

#include "add_songs_layout.hpp"
#include "kit_core.hpp"

#include <imgui.h>

namespace etude::app::ui {

// The Phone row of the Add songs page, laid out by `g` (add_songs_layout.hpp) from the cursor: an
// icon, the title over a detail line (in `detail_ink`), and a big switch right after them. A tap
// anywhere on the row turns the switch. True when it was tapped.
bool switch_row_detail(Kit& k, const char* icon, const char* label, const char* detail,
                       ImVec4 detail_ink, bool* value, const ToggleRowGeometry& g);
// A tile with an icon, a title and one line, at `area` from `origin`: a place the page leads to.
// `muted` draws it faint (not set up yet). True on a tap.
bool detail_tile(Kit& k, const char* id, const char* icon, const char* title, const char* detail,
                 ImVec2 origin, const Rect& area, bool muted);

} // namespace etude::app::ui
