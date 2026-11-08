// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/note_style.hpp"

#include "etude/pitch.hpp"

#include <algorithm>
#include <cmath>

namespace etude {
namespace {

// The block glow at 100: its strength (neon's a little stronger) and its width as a share of a
// white key, `kBlockGlowWidthBase` of it at 0 growing to (base + 1) at 100.
constexpr float kBlockGlowGain = 0.45f;
constexpr float kNeonGlowGain = 0.55f;
constexpr float kBlockGlowCurve = 0.6f;
constexpr float kBlockGlowWidthShare = 0.5f;
constexpr float kBlockGlowWidthBase = 0.5f;
// Beat lines: faint in every style, so they can be counted without drawing the eye. This is
// Glass's; the other styles set their own so a line changes their body about as much.
constexpr float kBeatLineAlpha = 0.32f;

float mixf(float a, float b, float t) { return a + (b - a) * t; }

std::uint8_t to_byte(float v) {
    return static_cast<std::uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f)));
}

// Pushes each channel away from (s > 1) or toward (s < 1) the color's grey.
RGBA saturate(RGBA c, float s) {
    const float grey = 0.2126f * c.r + 0.7152f * c.g + 0.0722f * c.b;
    return {to_byte(grey + (c.r - grey) * s), to_byte(grey + (c.g - grey) * s),
            to_byte(grey + (c.b - grey) * s), c.a};
}

RGBA over(RGBA fg, float alpha, RGBA bg) {
    alpha = std::clamp(alpha, 0.0f, 1.0f);
    return {to_byte(mixf(bg.r, fg.r, alpha)), to_byte(mixf(bg.g, fg.g, alpha)),
            to_byte(mixf(bg.b, fg.b, alpha)), 255};
}

} // namespace

StyleRecipe style_recipe(NoteStyle style, float intensity, int glow) {
    const float k = std::isnan(intensity) ? 0.0f : std::clamp(intensity, 0.0f, 1.0f);
    // The block glow: none at 0, the style's own strength times the setting, wider as it grows.
    const float g = static_cast<float>(std::clamp(glow, kMinBlockGlow, kMaxBlockGlow)) /
                    static_cast<float>(kMaxBlockGlow);
    float glow_gain = kBlockGlowGain;
    StyleRecipe r;
    r.beat_line_alpha = kBeatLineAlpha;
    switch (style) {
    case NoteStyle::Solid:
        // One flat colour with a crisp edge and tighter corners: no gradient.
        r.saturation = 1.0f + 0.12f * k;
        r.edge_feather_px = 0.6f;
        r.corner_scale = 0.45f;
        r.beat_line_alpha = 0.18f;
        break;
    case NoteStyle::Outline:
        // Hollow: a thick border in the note's own colour around a faint fill.
        r.body_alpha_top = 0.08f + 0.04f * k;
        r.body_alpha_bottom = 0.14f + 0.06f * k;
        r.saturation = 1.1f + 0.1f * k;
        r.rim_alpha = 1.0f;
        r.rim_width_px = 3.5f + 1.5f * k;
        r.rim_is_extra = false;
        r.edge_feather_px = 0.8f;
        r.corner_scale = 0.8f;
        r.beat_line_dark = false;
        r.beat_line_alpha = 0.28f;
        break;
    case NoteStyle::Glass:
        // See-through, a strong highlight band and cap, a light rim and a soft inner gradient.
        r.body_alpha_top = 0.22f + 0.1f * k;
        r.body_alpha_bottom = 0.55f + 0.15f * k;
        r.saturation = 1.1f + 0.1f * k;
        r.top_shade = 0.75f;
        r.inner_shade_alpha = 0.12f + 0.1f * k;
        r.inner_width_share = 0.25f;
        r.gloss_alpha = 0.18f + 0.14f * k;
        r.specular_alpha = 0.6f + 0.35f * k;
        r.rim_alpha = 0.55f + 0.35f * k;
        r.rim_width_px = 1.2f + 0.3f * k;
        r.rim_lighten = 0.75f;
        r.beat_line_dark = false;
        break;
    case NoteStyle::Neon:
        // A near-black body and a white-hot tube edge with the colour glowing inside it. Its glow
        // is the strongest at the same setting, but the tube is the look.
        r.body_alpha_top = 0.9f;
        r.body_alpha_bottom = 0.92f;
        r.saturation = 1.35f + 0.2f * k;
        r.core_darken = 0.86f;
        r.inner_glow_alpha = 0.45f + 0.3f * k;
        r.inner_width_share = 0.26f;
        r.rim_alpha = 1.0f;
        r.rim_width_px = 2.2f + 1.0f * k;
        r.rim_lighten = 0.5f + 0.15f * k;
        r.rim_is_extra = false;
        r.edge_feather_px = 1.5f;
        r.corner_scale = 1.4f;
        r.beat_line_dark = false;
        glow_gain = kNeonGlowGain;
        r.beat_line_alpha = 0.22f;
        break;
    case NoteStyle::Candy:
        // Saturated, light at the top and deep at the key end, a glossy band and cap, round.
        r.saturation = 1.3f + 0.25f * k;
        r.top_lighten = 0.32f + 0.18f * k;
        r.inner_shade_alpha = 0.18f + 0.12f * k;
        r.inner_width_share = 0.3f;
        r.gloss_alpha = 0.45f + 0.35f * k;
        r.bottom_shade_alpha = 0.3f + 0.2f * k;
        r.specular_alpha = 0.3f + 0.3f * k;
        r.specular_length = 0.35f;
        r.corner_scale = 1.3f;
        r.beat_line_alpha = 0.17f;
        break;
    case NoteStyle::Soft:
        // Pastel and matte with a feathered edge and small corners.
        r.saturation = 0.85f;
        r.body_lighten = 0.38f + 0.1f * k;
        r.edge_feather_px = 2.5f + 1.5f * k;
        r.inner_glow_alpha = 0.1f + 0.1f * k;
        r.inner_width_share = 0.4f;
        r.corner_scale = 0.6f;
        r.beat_line_alpha = 0.15f;
        break;
    case NoteStyle::Pixel:
        // Square, hard-edged and bevelled like an old game tile.
        r.saturation = 1.15f + 0.1f * k;
        r.edge_feather_px = 0.0f;
        r.corner_scale = 0.0f;
        r.bevel_alpha = 0.4f + 0.25f * k;
        r.bevel_share = 0.16f + 0.04f * k;
        r.beat_line_alpha = 0.19f;
        break;
    case NoteStyle::Ink:
        // A pale paper block with a dark ink line in the note's colour, like engraved music.
        r.body_lighten = 0.66f + 0.06f * k;
        r.rim_alpha = 1.0f;
        r.rim_width_px = 2.8f + 0.6f * k;
        r.rim_darken = 0.55f + 0.1f * k;
        r.rim_is_extra = false;
        r.edge_feather_px = 0.6f;
        r.corner_scale = 0.35f;
        r.beat_line_alpha = 0.16f;
        break;
    }
    // Strength rises fast at first, so the default (30) already reads as a halo.
    r.glow_alpha = glow_gain * std::pow(g, kBlockGlowCurve);
    r.glow_width_share = kBlockGlowWidthShare * (kBlockGlowWidthBase + g);
    return r;
}

StyleRecipe fuller_recipe(StyleRecipe r) {
    const auto fuller = [](float alpha) { return alpha + (1.0f - alpha) * kBlackBlockOpacityGain; };
    r.body_alpha_top = fuller(r.body_alpha_top);
    r.body_alpha_bottom = fuller(r.body_alpha_bottom);
    r.core_darken = 1.0f - (1.0f - r.core_darken) * (1.0f - kBlackBlockDeepen);
    r.body_lighten *= kBlackBlockLightenKeep;
    r.top_lighten *= kBlackBlockLightenKeep;
    return r;
}

StyleRecipe note_recipe(const LookSettings& look, int note) {
    const StyleRecipe r = style_recipe(look);
    return is_black_key(note) ? fuller_recipe(r) : r;
}

StyleRecipe degraded(StyleRecipe r, const EffectsBudget& budget) {
    r.glow_width_share *= std::clamp(budget.glow_width, 0.0f, 1.0f);
    if (!budget.style_extras) {
        r.gloss_alpha = 0.0f;
        r.bottom_shade_alpha = 0.0f;
        r.specular_alpha = 0.0f;
        r.noise_alpha = 0.0f;
        if (r.rim_is_extra) {
            r.rim_alpha = 0.0f;
        }
    }
    return r;
}

RGBA style_surface_color(const StyleRecipe& r, RGBA note, RGBA background) {
    const RGBA body = lighten(darken(saturate(note, r.saturation), r.core_darken), r.body_lighten);
    RGBA c = over(body, r.body_alpha_bottom, background);
    // The name sits near the key end, inside the inner glow's reach: count half of it.
    c = over(lighten(note, 0.35f), 0.5f * r.inner_glow_alpha, c);
    return c;
}

RGBA style_label_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand) {
    const RGBA surface = style_surface_color(
        note_recipe(look, note), note_color(look, note, velocity, hand), kBackgroundColor);
    return label_color_for(surface);
}

bool style_is_see_through(const StyleRecipe& r) {
    return std::min(r.body_alpha_top, r.body_alpha_bottom) < 0.99f;
}

RGBA key_underlay_color(const StyleRecipe& r, RGBA note) {
    if (!style_is_see_through(r)) {
        return note;
    }
    return lerp(kBackgroundColor, RGBA{note.r, note.g, note.b, 255}, kKeyUnderlayTint);
}

RGBA style_key_label_color(const LookSettings& look, int note, std::uint8_t velocity, Hand hand) {
    const StyleRecipe r = note_recipe(look, note);
    const RGBA c = note_color(look, note, velocity, hand);
    return label_color_for(style_surface_color(r, c, key_underlay_color(r, c)));
}

std::string_view note_style_name(NoteStyle style) {
    switch (style) {
    case NoteStyle::Solid:
        return "solid";
    case NoteStyle::Outline:
        return "outline";
    case NoteStyle::Glass:
        return "glass";
    case NoteStyle::Neon:
        return "neon";
    case NoteStyle::Candy:
        return "candy";
    case NoteStyle::Soft:
        return "soft";
    case NoteStyle::Pixel:
        return "pixel";
    case NoteStyle::Ink:
        return "ink";
    }
    return "glass";
}

bool legacy_note_style(std::string_view name, NoteStyle* out) {
    struct Old {
        std::string_view name;
        NoteStyle style;
    };
    static constexpr Old kOld[] = {{"filled", NoteStyle::Solid},
                                   {"glassy", NoteStyle::Glass},
                                   {"frosted", NoteStyle::Glass},
                                   {"shiny", NoteStyle::Candy},
                                   {"ghost", NoteStyle::Outline}};
    for (const Old& o : kOld) {
        if (o.name == name) {
            *out = o.style;
            return true;
        }
    }
    return false;
}

bool parse_note_style(std::string_view name, NoteStyle* out) {
    for (NoteStyle s : kAllNoteStyles) {
        if (note_style_name(s) == name) {
            *out = s;
            return true;
        }
    }
    return legacy_note_style(name, out);
}

} // namespace etude
