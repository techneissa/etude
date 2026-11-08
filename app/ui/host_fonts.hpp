// SPDX-License-Identifier: GPL-3.0-or-later
// The fonts of the UI, added to Dear ImGui's atlas when the Ui is made: Atkinson Hyperlegible regular and
// bold, each with the icon font and the music font's flat and sharp signs merged in, and the music font
// alone for the title medals' notes and clef.
#pragma once

#include "kit.hpp"

namespace etude::app::ui {

// Adds the fonts to the current ImGui context and names them in `kit` (regular, bold, music).
void add_ui_fonts(Kit& kit);

} // namespace etude::app::ui
