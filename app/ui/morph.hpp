// SPDX-License-Identifier: GPL-3.0-or-later
// Drawn stroke icons that morph into one another when a control changes state (play into pause,
// the song sound's three states, the gear into a close cross). The morph itself is core's
// `etude/icon_morph.hpp`, after the technique of morphicons (MIT License, Copyright (c) 2026
// Guillermo, https://github.com/guillermolg00/morphicons). The icons are in morph_glyphs.hpp:
// Lucide's path data (ISC licence) and the app's own hands.
#pragma once

#include "morph_glyphs.hpp"

#include "etude/icon_morph.hpp"

#include <imgui.h>

namespace etude::app::ui {

struct Kit;

// Draws `glyph` at rest, `size` points across (the 24-unit grid), without any state.
void draw_glyph(ImDrawList* dl, MorphGlyph glyph, ImVec2 centre, float size, ImVec4 colour);

// Draws the icon of the control `key`: when `glyph` differs from what it showed last frame it
// morphs there on a spring, from wherever it is; with reduced effects, or when the control was not
// drawn last frame, it switches at once.
void morph_glyph(const Kit& k, ImDrawList* dl, ImGuiID key, MorphGlyph glyph, ImVec2 centre,
                 float size, ImVec4 colour, Spring spring = kSpringSnappy);

inline constexpr float kQuarterTurn = 1.5707964f; // a chevron's open state: right turns to down

// Draws `glyph` turned `angle` radians clockwise about its centre. When `angle` changes the glyph
// turns there on a spring, from wherever it is; with reduced effects, or when the control was not
// drawn last frame, it switches at once. The glyph is never deformed.
void turn_glyph(const Kit& k, ImDrawList* dl, ImGuiID key, MorphGlyph glyph, float angle,
                ImVec2 centre, float size, ImVec4 colour, Spring spring = kSpringSmooth);

// Draws the skip buttons' step `seconds` (kSkipMin..kSkipMax) as digits `size` points across the
// 24-unit grid they are centred in. When it changes the numbers roll like a counter (the old one
// slides out and fades, the new one slides in from the direction of change), clipped to the ring,
// on a spring; with reduced effects, or when the control was not drawn last frame, it switches at
// once.
void roll_number(const Kit& k, ImDrawList* dl, ImGuiID key, int seconds, ImVec2 centre, float size,
                 ImVec4 colour, Spring spring = kSpringSmooth);

} // namespace etude::app::ui
