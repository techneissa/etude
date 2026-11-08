// SPDX-License-Identifier: GPL-3.0-or-later
// What the Melodies page draws that is not a control (learn_melodies.cpp): a tile's border and the
// ring round Play, both filling clockwise from the top as a piece's progress; the fades at a row's cut
// edges; and the picture on the Learn gallery's Melodies tile (a short phrase on a staff, a down
// arrow, then falling blocks).
#pragma once

#include "face_layout.hpp"
#include "kit.hpp"

namespace etude::app::ui {

// A rounded rectangle's border, `thick` wide and inside `a`..`b`, in `track` all round and over it in
// `fill` from the top middle clockwise for `share` (0 to 1) of its length; a full share is the
// whole border in `fill`.
void melody_draw_border(ImDrawList* dl, ImVec2 a, ImVec2 b, float radius, float thick, float share,
                        ImU32 fill, ImU32 track);

// A ring of radius `r` centred on `c`, `thick` wide: `track` all round, `fill` from the top
// clockwise for `share` of it.
void melody_draw_ring(ImDrawList* dl, ImVec2 c, float r, float thick, float share, ImU32 fill,
                      ImU32 track);

// A fade into the screen's colour at the left and right edges of `view` where the row has more.
void melody_draw_fades(const Kit& k, const Box& view, bool more_left, bool more_right);

// The same at the top and bottom edges of `body` where the page has more to scroll to.
void melody_draw_page_fades(const Kit& k, const Box& body, bool more_up, bool more_down);

// The Melodies tile's picture inside `box`, in `own` (the player's pick for Melodies): no frame, as
// the keys' pictures have none.
void melody_draw_picture(const Kit& k, ImDrawList* dl, const Box& box, RGBA own);

} // namespace etude::app::ui
