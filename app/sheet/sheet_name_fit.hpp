// SPDX-License-Identifier: GPL-3.0-or-later
// Note names on the sheet, the app's half: their words for a look, measured with the app's font,
// their one size, and the style core's place_sheet_names() places them with. Shared by the view
// and `--sheet-dump`, so both place names the same way.
#pragma once

#include "text.hpp"

#include "etude/look.hpp"
#include "etude/sheet_names.hpp"
#include "etude/sheet_view.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace etude::app {

// A name's font size: this many staff spaces, at least `kSheetNameFontMm` and at most
// `kSheetNameFontMaxMm` on the screen (a big staff keeps quiet names).
inline constexpr float kSheetNameFontMm = 2.7f;
inline constexpr float kSheetNameFontMaxMm = 3.4f;
inline constexpr float kSheetNameFontSpaces = 1.2f;
// A crowded column may step down to this share of that size, never under the app's smallest
// legible name (min_name_font_px).
inline constexpr float kSheetNameFloorShare = 0.8f;
// The mark where names were left out: three dots, each this share of the names' line height (and
// at least `kSheetNameMoreDotMinPx` on the screen) with a dot's room between them, a little fainter
// than the names.
inline constexpr float kSheetNameMoreDot = 0.15f;
inline constexpr float kSheetNameMoreDotMinPx = 2.0f;
inline constexpr float kSheetNameMoreAlpha = 0.6f;

struct SheetNameTexts {
    std::array<std::string, 128> text;      // per MIDI key
    std::array<float, 128> unit_width{};    // its width per pixel of line height
    float line_em = 1.2f;                   // the font's line height per pixel of size

    // Letters or Do Re Mi by `look.sheet_naming`; octave numbers and sharps or flats as the names
    // say. Allocates: call when the look changes.
    void make(const LookSettings& look, Text& font);
};

// The chord symbols placed since the names were last made: their words (chord_name, as the chord
// label names a chord) and widths per pixel of line height, each measured once.
struct SheetSymbols {
    std::vector<std::string> text;
    std::vector<float> unit_width;
    std::unordered_map<std::string, std::int32_t> ids;

    void clear();
    // Gives each column with a chord its symbol and width; sharps or flats by `flats`. Allocates
    // for a symbol not seen before: call when the names are placed, never per frame.
    void name_columns(std::vector<SheetNameColumn>& columns, bool flats, float line_em, Text& font);
};

// The scale a system is drawn at, screen pixels per picture pixel: across the systems' width
// (`engraved_width` as engraved, `system_width_px` on the screen), smaller when that would make it
// taller than its slot and the paper's margin. Names are placed at the scale their system draws at.
float sheet_system_scale(const SheetSystem& sys, float engraved_width, float system_width_px, float slot_px,
                         float margin_px);

// The names' font size in pixels at a staff space (both on the screen).
float sheet_name_font_px(float staff_space_px, float px_per_mm);

// The room the names take in the plan (core SheetNameRoom), for stacks of `stacks` names.
SheetNameRoom sheet_name_room(const SheetNameStacks& stacks, float px_per_mm, float line_em);

// The style in the system's picture at `staff_space_px` (as engraved), for names drawn at
// `scale` (screen pixels per picture pixel), with the slot limits left open.
SheetNameStyle sheet_name_style(float staff_space_px, float scale, float px_per_mm,
                                const SheetNameTexts& texts);

// Note names are given room across the page too: Verovio's spacing (its spacingLinear and
// spacingNonLinear options) with names on, fewer bars to a system; 0 is Verovio's own.
struct SheetNameSpacing {
    float linear = 0.0f;
    float non_linear = 0.0f;
};
SheetNameSpacing sheet_name_spacing(bool names, SheetNaming naming);

} // namespace etude::app
