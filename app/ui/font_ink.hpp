// SPDX-License-Identifier: GPL-3.0-or-later
// Where a glyph's ink lies in its box, read from the font atlas, and the capitals' band of a font.
#pragma once

#include <imgui.h>

namespace etude::app::ui {

// Where a font icon's ink lies, from the pen position, in the pixels of the size it was baked at
// (`l`, `r`: left and right; `t`, `b`: top and bottom). Read once per glyph and size from the
// glyph's bitmap in the atlas: its coverage gives the edges to a fraction of a pixel. Without the
// bitmap (not on the CPU yet) the glyph's own bounds stand in.
struct GlyphInk {
    float l = 0.0f;
    float t = 0.0f;
    float r = 0.0f;
    float b = 0.0f;
};

// The ink of `cp` in `font`'s baked size, cached per font, code point and size.
GlyphInk glyph_ink(ImFont* font, ImFontBaked* baked, const ImFontGlyph* g, ImWchar cp);

// The capitals' band of `font` at `size`: the top and bottom of a capital H, from the pen's y.
struct CapBand {
    float top = 0.0f;
    float bottom = 0.0f;
};

CapBand cap_band(ImFont* font, float size);

// Empties both caches: they are keyed by a font's address, and a new UI's fonts may land at an
// address an old one had.
void forget_font_ink();

} // namespace etude::app::ui
