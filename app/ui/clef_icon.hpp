// SPDX-License-Identifier: GPL-3.0-or-later
// A treble clef drawn from Bravura's gClef (the music font Verovio ships beside the app), for the
// places the icon font has no clef: the song-opened toast's composer line. The glyph is rasterised
// once per pixel size with lunasvg into a white-on-clear texture and tinted by the draw call, so
// no frame rasterises.
#pragma once

#include <imgui.h>

namespace etude::app::ui {

struct Kit;

// Width over height of the clef's ink, 0 when the font file is not there (the clef is then left
// out and nothing is reserved for it).
float clef_aspect();
// Draws the clef `height` points tall, its ink box centred on `centre`, in `colour`.
void draw_clef(const Kit& k, ImDrawList* dl, ImVec2 centre, float height, ImVec4 colour);
// Frees the cached texture; call while the renderer is alive.
void release_clef();

} // namespace etude::app::ui
