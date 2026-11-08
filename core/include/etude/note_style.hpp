// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>
#include <string_view>

#include "etude/color.hpp"
#include "etude/effects_governor.hpp"
#include "etude/hand.hpp"
#include "etude/look.hpp"

namespace etude {

// How each note style is painted, as numbers the renderer reads. One recipe
// paints a note block and its lit key alike, so the two always match. Black-key notes are darker
// through `note_color` and fuller through `fuller_recipe`, in every style.
//
// Layers, bottom to top: glow (additive, around the block), body (a vertical gradient), inner
// shade and inner glow (fading in from the edge), the style extras (gloss band, bottom shade,
// specular streak, grain), the rim and bevel, and the beat lines. Lengths ending in `_px` are 1080p
// pixels; widths ending in `_share` are a share of the block's width; alphas are 0..1.
struct StyleRecipe {
    // Body. The far end (top of a rising block) and the key end each have an opacity. The colour is
    // first saturated by `saturation` (1 keeps it), mixed toward black by `core_darken`, then the
    // far end is scaled by `top_shade` and mixed toward white by `top_lighten`.
    float body_alpha_top = 1.0f;
    float body_alpha_bottom = 1.0f;
    float saturation = 1.0f;
    float core_darken = 0.0f;
    float top_shade = 1.0f;
    float top_lighten = 0.0f;
    float edge_feather_px = 1.2f; // soft edge outside the body
    // Both ends mixed toward white after the darkening: pastel or paper bodies.
    float body_lighten = 0.0f;
    // The look's corner radius and its cap are multiplied by this: 0 is square, 2 a capsule.
    float corner_scale = 1.0f;

    // Fading in from the edge: a dark shade (depth) and a glow in the note's colour lightened.
    float inner_shade_alpha = 0.0f;
    float inner_glow_alpha = 0.0f;
    float inner_width_share = 0.3f;

    // Extras, dropped at EffectsLevel::NoStyleExtras.
    float gloss_alpha = 0.0f;        // candy, glass: bright band down the left part and a cap
    float bottom_shade_alpha = 0.0f; // candy: darker toward the key end
    float specular_alpha = 0.0f;     // glass: thin bright streak near the left edge
    float specular_length = 0.85f;   // share of the block's height the streak runs down
    float noise_alpha = 0.0f;        // grain texture
    float rim_alpha = 0.0f;          // outline in the note's colour mixed toward white
    float rim_width_px = 0.0f;
    float rim_lighten = 0.0f;
    float rim_darken = 0.0f; // the rim mixed toward black after lightening: an ink line
    bool rim_is_extra =
        true; // outline, neon and ink keep their rim at every level: it is the style
    // A bevel: a light edge along the top and left, a dark one along the bottom and right, each
    // `bevel_share` of the block's width and at least `bevel_min_px`; kept at every level.
    float bevel_alpha = 0.0f;
    float bevel_share = 0.0f;
    float bevel_min_px = 2.0f;
    // Beat lines (the look's `beat_lines`): `beat_line_px` thick at `beat_line_alpha`, dark on
    // a light or solid body, or the note's colour lightened on a dark or hollow one; faint in
    // every style. Kept at every level.
    float beat_line_alpha = 0.0f;
    float beat_line_px = 1.5f;
    bool beat_line_dark = true;

    // Glow around the block, as a share of a white key's width, and its strength when the note is
    // neither held nor just pressed (held glow and the press flash add to it). Both come from the
    // look's block glow: none at 0.
    float glow_alpha = 0.0f;
    float glow_width_share = 0.45f;
};

// The recipe for `style` at `intensity` (0..1, clamped; NaN counts as 0) and block glow `glow`
// (0..100, clamped). Every strength in the recipe grows with the intensity, never shrinks; the
// glow grows with `glow` and is off at 0.
StyleRecipe style_recipe(NoteStyle style, float intensity, int glow = kDefaultBlockGlow);
inline StyleRecipe style_recipe(const LookSettings& look) {
    return style_recipe(look.note_style, look.style_intensity, look.block_glow);
}

// A black key's block (and its lit key) is the fuller version of the style: the same hue, more
// opaque and a little deeper. Its body's opacity goes `kBlackBlockOpacityGain` of the way to
// opaque, and its colour is mixed `kBlackBlockDeepen` further toward black on top of the style's
// own darkening. Everything else, and so the style's character, stays.
inline constexpr float kBlackBlockOpacityGain = 0.4f;
inline constexpr float kBlackBlockDeepen = 0.15f;
// A pale body (`body_lighten`) or a light far end (`top_lighten`) keeps this share of its
// lightening on a black key, so it is the stronger tint.
inline constexpr float kBlackBlockLightenKeep = 0.3f;
StyleRecipe fuller_recipe(StyleRecipe recipe);
// The recipe `note`'s block is painted with: the look's style, fuller for a black key.
StyleRecipe note_recipe(const LookSettings& look, int note);

// The recipe as drawn at an effects level: the glow narrower, the extras off. The body, inner
// shade and glow, and a rim that is the style's body (ghost, neon) are never touched, so notes and
// keys look the same at every level apart from the extras.
StyleRecipe degraded(StyleRecipe recipe, const EffectsBudget& budget);

// The colour a block's key end shows over `background`, where its note name sits: the body
// (saturated, darkened) blended at `body_alpha_bottom`, plus the inner glow.
RGBA style_surface_color(const StyleRecipe& recipe, RGBA note, RGBA background);

// Black or white for a note name on a block in the look's style: whichever reads better on
// `style_surface_color` (with the note's own recipe, `note_recipe`) over `kBackgroundColor`. At
// least `kLabelMinContrast`.
RGBA style_label_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand);

// A lit key under a see-through style (body less than opaque) is painted over the background
// mixed this far toward the note colour, so it shows the style as its block does and still reads
// as lit.
inline constexpr float kKeyUnderlayTint = 0.4f;
bool style_is_see_through(const StyleRecipe& recipe);
// The colour under a lit key's body: `kBackgroundColor` tinted toward `note` for a see-through
// style, the note itself (unused) for an opaque one.
RGBA key_underlay_color(const StyleRecipe& recipe, RGBA note);
// Black or white for a name on a lit key: as style_label_color, on the key's own underlay.
RGBA style_key_label_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand);

// "solid", "outline", "glass", "neon", "candy", "soft", "pixel", "ink".
std::string_view note_style_name(NoteStyle style);
// The reverse, for the command line and the settings file. The six older style names are read as
// the style that replaced them (`legacy_note_style`). False, `*out` untouched, for an unknown name.
bool parse_note_style(std::string_view name, NoteStyle* out);
// The style an older saved name stands for: filled is Solid, glassy and frosted Glass, shiny
// Candy, ghost Outline and neon Neon. False for any other name.
bool legacy_note_style(std::string_view name, NoteStyle* out);
inline constexpr NoteStyle kAllNoteStyles[] = {
    NoteStyle::Solid, NoteStyle::Outline, NoteStyle::Glass, NoteStyle::Neon,
    NoteStyle::Candy, NoteStyle::Soft,    NoteStyle::Pixel, NoteStyle::Ink};

} // namespace etude
