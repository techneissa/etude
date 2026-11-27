// SPDX-License-Identifier: GPL-3.0-or-later
// Cards, shadows and the note-style preview.
#pragma once

#include "kit_colour.hpp"
#include "kit_core.hpp"

#include "etude/color.hpp"
#include "etude/look.hpp"

#include <imgui.h>

namespace etude::app::ui {

// Where a card may draw. `Current`: inside the list's clip (a scrolling page's own), widened only
// by the card's shadow, and not past the clip of the window around it, so a card in a page that
// scrolls never paints over the sheet's header or the top bar. `Screen`: the whole screen, for a
// popover or a sheet whose card and shadow reach past the window they are drawn in.
enum class CardClip : std::uint8_t { Current, Screen };
// A soft shadow, then the surface, then a hairline and a faint top highlight.
void card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
          ImVec4 fill = col::kSurface, ImDrawFlags corners = ImDrawFlags_RoundCornersAll,
          float shadow = 1.0f, CardClip clip = CardClip::Current);
// A card for a popover, a sheet or a toast: drawn on the whole screen (`CardClip::Screen`), because
// it and its shadow reach past the window it is drawn in.
inline void overlay_card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
                         ImVec4 fill = col::kSurface,
                         ImDrawFlags corners = ImDrawFlags_RoundCornersAll, float shadow = 1.0f) {
    card(k, dl, a, b, radius, fill, corners, shadow, CardClip::Screen);
}
// The same for a sheet or a dialog: the deeper shadow they all use.
inline void sheet_card(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius,
                       ImVec4 fill = col::kSurface) {
    card(k, dl, a, b, radius, fill, ImDrawFlags_RoundCornersAll, 1.4f, CardClip::Screen);
}
void shadow(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float strength);
// A small preview of one note style, painted from its recipe at `glow` (the block glow): a
// white-key block and a narrower, darker, fuller black-key block.
void note_preview(const Kit& k, ImDrawList* dl, ImVec2 a, ImVec2 b, NoteStyle style, RGBA colour,
                  float intensity, int glow = kDefaultBlockGlow);

} // namespace etude::app::ui
