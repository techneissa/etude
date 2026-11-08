// SPDX-License-Identifier: GPL-3.0-or-later
// The title medals (#396): the eight medals of the overall title, Beginner to Virtuoso, each a tier's
// material with a piano emblem that builds up from tier to tier: a quarter note, two beamed eighth notes,
// an octave of keys, a treble clef over keys, an upright piano, a grand piano, a grand with its lid up
// and music on the stand, a grand inside a laurel wreath. The notes and the clef are glyphs of the
// bundled music font; the keys and the pianos are vector art. Embossed on the face like a badge's icon
// (a dark edge down and right, a light one up and left, then the fill), so the small medal of a
// Household row and the switcher (28 pt, no ribbon) reads as well as the large one.
#pragma once

#include "kit.hpp"

#include <string>

namespace etude::app::ui {

// The title medal of `title` (1 .. 8), the disc `d` across centred on `centre`; `large` draws it with
// its ribbon above (as draw_medal does).
void draw_title_medal(const Kit& k, ImDrawList* dl, int title, ImVec2 centre, float d, bool large);

// A title medal as a badge beside a line of text: `d` across, centred on `centre`, no ribbon.
inline void draw_title_medal_small(const Kit& k, ImDrawList* dl, int title, ImVec2 centre, float d) {
    draw_title_medal(k, dl, title, centre, d, false);
}

} // namespace etude::app::ui
