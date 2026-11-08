// SPDX-License-Identifier: GPL-3.0-or-later
// The Effects page: the colour scheme as a row of chips, the curated palettes as rows
// with a swatch strip, the scheme's own swatches with an inline saturation/brightness square, hue
// bar, recent colours and a grid of quick picks, the live preview of falling notes, the hit effects
// as picture tiles and the effect switches. It lays itself out from colours_layout.hpp: whole in
// two columns where it fits, else in two halves behind a Colours | Hit effects switch in the sheet's
// header. The colour logic (palettes, swatches, the picker's arithmetic, the quick picks, recent
// colours) is core's palette.hpp.
#pragma once

#include "kit.hpp"

namespace etude::app::ui {

// Draws the page at the cursor in a body `body` points big (the sheet's page area).
void colours_page(Kit& k, UiModel& model, ImVec2 body);
// The page's Colours | Hit effects switch at the right end of the sheet's header row, drawn at the
// cursor, only while the page is split in two halves. `body` as above.
void colours_header(Kit& k, ImVec2 body);

} // namespace etude::app::ui
