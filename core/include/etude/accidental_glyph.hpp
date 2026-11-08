// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace etude {

// The sharp and the flat in a note's name, drawn as shapes instead of font text: at 6 to 10 pixels
// a font's # and b are smeared by hinting into a blob, while a few pixel-snapped strokes stay
// crisp. A sharp is two vertical strokes crossed by two slightly slanted bars; a flat is a stem
// with a bowl. This is pure geometry in pixels, y down, origin at the glyph's top-left; the
// renderer fills the quads, and an outline is the same quads grown by its width.

enum class AccidentalKind : std::uint8_t { None, Sharp, Flat };

// The kind of the accidental piece of a note name ("#", "b"); None for anything else.
AccidentalKind accidental_kind(std::string_view accidental);

// The smallest a glyph is drawn: below this its strokes run together.
inline constexpr float kMinAccidentalGlyphPx = 6.0f;
// A glyph's height as a share of the letter's cap height (it stands beside the letter's top like
// a superscript), never below `kMinAccidentalGlyphPx`.
inline constexpr float kAccidentalCapShare = 0.75f;
// The glyph's width as a share of its height (sharp, flat), whole pixels, at least `kMin...Px`.
inline constexpr float kSharpWidthShare = 0.75f;
inline constexpr float kFlatWidthShare = 0.5f;
inline constexpr float kMinSharpWidthPx = 5.0f; // two strokes, a gap, and the bars past them
inline constexpr float kMinFlatWidthPx = 3.0f;  // a stem and the bowl's rim

// The glyph's height for a letter whose capitals are `cap_px` tall: whole pixels.
float accidental_glyph_height_px(float cap_px);
// Its width at `height_px` (whole pixels); 0 for `None`.
float accidental_glyph_width_px(AccidentalKind kind, float height_px);

// A convex quadrilateral, corners in order.
struct GlyphQuad {
    std::array<float, 4> x{};
    std::array<float, 4> y{};
};

inline constexpr std::size_t kMaxGlyphQuads = 12;

struct AccidentalShape {
    float width = 0.0f; // equal to accidental_glyph_width_px
    float height = 0.0f;
    std::size_t count = 0;
    std::array<GlyphQuad, kMaxGlyphQuads> quads{};
};

// The shape of `kind` at `height_px` (rounded to a whole number, at least `kMinAccidentalGlyphPx`).
// The glyph's outer corners are on whole pixels (a corner where a stroke meets a slanted bar may
// sit between pixels), every stroke is at least 1 px thick, the quads barely overlap (so a
// translucent fill shows no seams) and lie inside `width` by `height`. Empty for `None`.
AccidentalShape accidental_shape(AccidentalKind kind, float height_px);

} // namespace etude
