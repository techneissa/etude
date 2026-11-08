// SPDX-License-Identifier: GPL-3.0-or-later
// A glyph of the music font (Noto Music: the treble clef, notes) drawn by its ink: the title medals'
// emblems and the Learn gallery's Melodies picture use it.
#pragma once

#include "kit.hpp"

namespace etude::app::ui {

// The glyph `cp` centred on `centre` with its ink `ink_h` tall, in `ink`. Nothing when the kit has
// no music font or the font has no such glyph.
void draw_music_glyph(const Kit& k, ImDrawList* dl, ImVec2 centre, ImWchar cp, float ink_h, ImU32 ink);

} // namespace etude::app::ui
