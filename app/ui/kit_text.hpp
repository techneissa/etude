// SPDX-License-Identifier: GPL-3.0-or-later
// Text: measuring and labels placed by their ink.
#pragma once

#include "face_layout.hpp"
#include "ink_measure.hpp"
#include "kit_core.hpp"

#include <imgui.h>

#include <string_view>

namespace etude::app::ui {

// Where the ink of one character lies, from the pen position the text is drawn at (`min` the top
// left, `max` the bottom right), at the font size `size`: measured from the glyph's bitmap, so a
// letter can be placed by its ink. False when the font has no such glyph.
bool font_ink(ImFont* font, float size, char c, ImVec2* min, ImVec2* max);
// `text` as the screen shows it: a note named in it ("Prelude in Bb", "C#4") carries the signs ♭
// and ♯, never the letter b or a number sign (core `accidental_display_text_into`). Text with no
// note name comes back as it was (no copy); the rest is written into one of a few strings kept
// between calls, valid until the eighth call after it. Every text the kit draws or measures goes
// through it (`text_at`, `text_size`, `label_at`, `label_stack`, `fit`), so the same words measure
// and draw alike; text drawn straight with `AddText` calls it first.
const char* screen_text(const char* text);
std::string_view screen_text(std::string_view text);
void text_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, ImVec2 pos, ImVec4 colour,
             const char* text);
ImVec2 text_size(ImFont* font, float size, const char* text);
// `text_size(...).x`, kept per font, size and words until the fonts are rebuilt (`forget_fonts`), for
// text measured every frame that seldom changes.
float text_width(ImFont* font, float size, const char* text);
// A label placed by its ink, never by its font's line box (which holds room for descenders: a
// word without them, "All" or a number, would sit high). Vertical: the capitals' band is centred
// on the slot's middle. Horizontal, by `align`: `Left` starts the pen at the slot's left edge,
// `Centre` centres the visible letters (the advance less the side bearings of the first and last
// letter), `Right` puts their right edge at the slot's right edge. Every text in a control goes
// through this; the icon audit leaves these labels out of its second frame and measures where they
// were (icon_audit.hpp).
// `slot` is the control's face (or a row's, or one line's box), in ImGui points.
LabelMetrics label_metrics(ImFont* font, float size, const char* text);
// The width `label_at` draws `text` at: a size under 40 pt is rounded to a whole point there, so
// anything placed against the end of a label (an info button) measures with this, not `text_size`.
float label_width(ImFont* font, float size, const char* text);
void label_at(const Kit& k, ImDrawList* dl, ImFont* font, float size, const Box& slot,
              LabelAlign align, ImVec4 colour, const char* text);
// The pen position `label_at` would draw at, for text drawn another way (a gradient's).
ImVec2 label_pen(ImFont* font, float size, const Box& slot, LabelAlign align, const char* text);
// Empties the caches keyed by a font's address (ink, capital band, fitted lines, label metrics): a
// new UI's fonts may land at an address an old one had, and its text must be measured, and its
// glyphs baked, as a first run does. Called as the fonts are built (`add_ui_fonts`) and when the UI
// goes.
void forget_fonts();

} // namespace etude::app::ui
