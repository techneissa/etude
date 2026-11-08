// SPDX-License-Identifier: GPL-3.0-or-later
// A note's name drawn the way the app draws names on its blocks and chord labels: a bold letter and
// each sharp or flat sign as a shape (core accidental_glyph.hpp) raised beside the letter's top,
// not the font's ♯ and ♭, which at small sizes are blobs that hang below the baseline.
#pragma once

#include "kit.hpp"

namespace etude::app::ui {

// `plain` is the name as the code spells it ("C", "Bb", "F#", "F##"): a letter A to G and signs.
// The letter and its signs are one group, centred in `slot` across; the letter is placed by its ink
// like every label (`label_at`), so the audit measures it. Text that is not a name is drawn as one
// label.
void note_name_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                  ImVec4 colour, const char* plain);

// How wide `note_name_at` draws `plain` (its ink across): the letter and its signs.
float note_name_width(ImFont* font, float size, const char* plain);

// A chord's name as the free-play label draws it, from its parts (core ChordNameParts, plain
// spelling): the root with its signs, the quality as written ("maj7", "m7b5", "7b9", "6/9": a 'b'
// in a quality is a flat sign), and the slash bass; every sign a shape, the rest one text run. Left
// aligned in `slot` (its cap band centred on the slot's middle, as `label_at`), shrunk to fit the
// slot's width when it is wider.
void chord_name_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
                   ImVec4 colour, const char* root, const char* quality, const char* bass);

// How wide `chord_name_at` draws the name at `size`.
float chord_name_width(ImFont* font, float size, const char* root, const char* quality,
                       const char* bass);

} // namespace etude::app::ui
