// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

#include "etude/accidental_glyph.hpp"
#include "etude/look.hpp"
#include "etude/screen_layout.hpp"

namespace etude {

// Where a falling block's note name goes: across its own block, or nowhere. A name never leaves
// its block (except under the "Always" setting, see below). Black keys' blocks are narrow (about
// 3 to 4 mm on a laptop screen), so a black key's name is the letter with a small accidental
// raised beside it, like a superscript, never the bare letter: "C" on a C# key names the wrong
// key.
//
// A name is made of up to three pieces drawn side by side: the letter or syllable, the accidental
// as a drawn glyph (`accidental_glyph.hpp`) whose top is level with the letter's capitals, and the
// octave number at the letter's size. The planner needs how wide the letter and the octave number
// are; core has no font, so the caller passes the widths measured with the real one at every whole
// size (`NameWidths`), and the glyph's width follows from its own rules. It tries, in order:
//   1. the whole name, octave number included, at the biggest size that fits, down to the
//      minimum legible size (`min_font_px`);
//   2. the name without the octave number, the same;
//   3. a name with an accidental, without the octave, at smaller sizes still, down to
//      `min_small_font_px`;
//   4. nothing.
// Every size is a whole number of pixels: the minimum rounds up, the maximum rounds down, so what
// is drawn is exactly what was planned. Everything is pure placement with fixed-size state, so it
// can run every frame.

// At most this many blocks, the ones nearest the keys, are ranked in one call (the rest come back
// hidden), and at most `BlockNameParams::max_names` of them get a name. A caller that collects
// blocks in a buffer this size keeps the nearest with `name_candidate_slot`.
inline constexpr std::size_t kMaxNameCandidates = 256;
// Clear space between a name's box and the block's sides, and between it and the block's top and
// bottom edges. The box already holds the outline's width on every side, but an outline that
// reaches the block's border reads as running over it, so a name is planned with `kNamePadXPx` clear
// to each side at the biggest size that gives it; only when no size (down to the smallest) does,
// the sides get none and the box fills the block (a black key's block is only about 3.5 mm wide on
// a laptop screen). The box is never wider than the block.
inline constexpr float kNamePadXPx = 1.0f;
inline constexpr float kNamePadYPx = 2.0f;

enum class NameForm : std::uint8_t {
    Hidden, // nothing fits
    Full,   // the whole name, octave number included when the look shows one ("C#4")
    Short,  // the name without its octave number ("C#", "C")
};

// How wide the text pieces of a name are at every whole font size, in pixels, measured by the
// caller with the real font at that size: `letter[px]` is the letter or syllable at `px` pixels,
// `octave[px]` the octave number. A size past the end of a table, or with no positive finite
// width, is never planned. `octave` is empty when the look shows no octave numbers.
struct NameWidths {
    std::span<const float> letter;
    std::span<const float> octave;
};

struct BlockNameIn {
    RectF block; // the whole block in window pixels, it may run past the clip
    int note = 0;
    bool black = false; // the block of a black key, which the black key names setting governs
    AccidentalKind accidental = AccidentalKind::None; // drawn after the letter
    NameWidths width;
    // The key's lane (`BlockColumn::lane_x`, `lane_w`) across, in window pixels: as far as a name
    // spilled under the Always setting may reach. Empty (the default) is the block itself.
    float lane_x = 0.0f;
    float lane_w = 0.0f;
};

struct BlockNameParams {
    RectF clip; // where names may appear, normally the note area
    float max_font_px = 0.0f;
    float min_font_px = kMinNameFontPx;
    float min_small_font_px = kMinSmallNameFontPx;
    float line_em = 1.24f; // the text box's height in em, measured with the real font
    // Where the letters' capitals are in that box, in em, measured with the real font: how tall
    // they are, and how far their top is below the box's top. The accidental's glyph is sized from
    // the first and hung from the second.
    float cap_em = 0.668f;
    float cap_top_em = 0.282f;
    BlackKeyNames black_names = BlackKeyNames::Auto;
    std::size_t max_names = 48;
    // A rect the names keep clear of (the key label): a block it touches gets its name above it,
    // or none when the part of the block above does not hold one. Empty for no such rect.
    RectF keep_clear;
};

struct BlockNamePlacement {
    NameForm form = NameForm::Hidden;
    // The whole box, empty when hidden: the pieces and `outline_px` of clear space round them.
    RectF rect;
    float font_px = 0.0f;    // the letter's and the octave's size, whole pixels
    float outline_px = 0.0f; // the outline drawn round the glyphs, inside `rect`
    // The pieces left to right from `rect.x + outline_px`, widths in pixels: the letter, the
    // accidental's glyph (`glyph_w` by `glyph_h`, `None` and 0 without one) and, in the Full form
    // only, the octave number. The glyph hangs `glyph_dy` below the top of the letter's text box.
    float letter_w = 0.0f;
    AccidentalKind accidental = AccidentalKind::None;
    float glyph_w = 0.0f;
    float glyph_h = 0.0f;
    float glyph_dy = 0.0f;
    float octave_w = 0.0f;
};

// The outline width for text at `font_px`, whole pixels, at least 1.
float name_outline_px(float font_px);

// How far up from a block's foot a name at `font_px` can reach: the planner's box (the text's
// line, or an accidental hanging below it, and the outline above and below) and the pad under it.
// Anything drawn across a block above this height never meets its name.
float name_clearance_px(const BlockNameParams& params, float font_px);

// True when the rectangles share area (touching edges do not count).
bool rects_overlap(const RectF& a, const RectF& b);

// Plans the names of `blocks`, one result per block in `out` (same order; `out` holds at least
// `blocks.size()` entries). Only the `params.max_names` blocks nearest the bottom of `clip` (the
// keys) are named, nearest first, then the lower note, whatever order they come in; a block with
// no visible part, a zero or negative size, a bad number or no letter is never named. The sizes
// tried run from `max_font_px`, never more than the clip's height, down. Within those, the white
// keys' blocks settle first, then the black keys', and no name overlaps one already placed: a name
// that would takes the next option. The name is centred on the visible part of its block, its
// bottom `kNamePadYPx` above the block's bottom edge (or the clip's), and fits inside the block
// with `kNamePadXPx` (when some size allows it) to each side and `kNamePadYPx` above and below. Black keys' blocks are drawn
// over white keys' blocks, so a white key's name keeps clear of every black key's block in `blocks`
// (named or not): it goes in the widest part of its block that none covers at the name's height,
// centred there. Black keys follow `params.black_names`: Off names none; Always, when no option
// fits, draws the name without its octave at the minimum size (`min_small_font_px`) centred on the
// block anyway, spilling past its sides, but only inside the key's lane (`BlockNameIn::lane_x`),
// so never over a neighbour's block beyond the lane, and never over a name already placed (it
// still has to fit the block's height); when that is not possible it is hidden. Returns how many
// names were placed.
std::size_t plan_block_names(const BlockNameParams& params, std::span<const BlockNameIn> blocks,
                             std::span<BlockNamePlacement> out);

// For a caller that collects the frame's blocks in a buffer of `capacity` before planning: where
// `block` goes so the buffer keeps the blocks nearest the bottom of `clip`, ranked as
// plan_block_names ranks them (then the earlier one). Returns `held.size()` to append while there
// is room, the index of the entry it replaces when it is nearer than the farthest one held, or
// `capacity` to drop it.
std::size_t name_candidate_slot(std::span<const BlockNameIn> held, std::size_t capacity,
                                const BlockNameIn& block, const RectF& clip);

// A text texture made at `from_px` (`from_w` by `from_h` pixels) can stand in, stretched, for the
// same text at `px` until that is made. Its height is the one the size ratio gives and its width
// is stretched to the planned `width`; this returns the height, or 0 when it does not fit: taller
// than `max_height`, or stretched so differently across and down (the difference of the two
// factors more than `max_skew` of the vertical one) that the letters would look wrong, or any
// input is not a positive finite number. A name that gets 0 is left out of the frame, never drawn
// outside its box or in distorted letters.
float stand_in_height(float from_px, float from_w, float from_h, float px, float width,
                      float max_height, float max_skew);

} // namespace etude
