// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <span>

namespace etude::app {

// How big the signs are drawn against the letters beside them, as a multiple of the text's em: the
// music font's flat and sharp are small for their em. ImGui sizes a font by its line, which the
// patched copy shares with Atkinson, so its scale is relative to Atkinson's own and each sign has
// its own; SDL_ttf sizes by the em, and one font serves both signs, so it takes their mean.
inline constexpr float kFlatSignScale = 1.25f;
inline constexpr float kSharpSignScale = 1.15f;
inline constexpr float kSignScale = (kFlatSignScale + kSharpSignScale) * 0.5f;

// The music font (Noto Music), which supplies the flat and sharp signs Atkinson lacks, with its
// line metrics (ascent, descent, line gap) set to Atkinson's. A text engine puts a fallback font's
// glyph on a baseline worked out from that font's own ascent, and Noto Music's line is half as
// tall again as Atkinson's, so unpatched its signs hang below the letters. The copy is made once
// and lives as long as the program. Only the metrics change; every outline is the font's own.
std::span<const unsigned char> music_font();
// The same font for an engine that sizes by the em and draws a fallback at a bigger size (SDL_ttf:
// the signs are `kSignScale` bigger): its line is that much shorter, so the bigger size gives
// Atkinson's line again and the baseline, worked out from the ascent, stays where the letters' is.
std::span<const unsigned char> music_font_for_em();

} // namespace etude::app
