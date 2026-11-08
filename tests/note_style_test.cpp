// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/note_style.hpp"
#include "etude/palette.hpp"

#include <doctest/doctest.h>

#include "sample_looks.hpp"

#include <algorithm>
#include <cmath>

using namespace etude;
using namespace etude::test;

namespace {

// The rim's colour as the painter mixes it (saturation is left out: it moves both hands alike).
RGBA rim_colour(const StyleRecipe& r, RGBA note) {
    return darken(lighten(note, r.rim_lighten), r.rim_darken);
}

// Every number in a recipe that measures how strong something is.
float strength_sum(const StyleRecipe& r) {
    return r.inner_shade_alpha + r.inner_glow_alpha + r.gloss_alpha + r.bottom_shade_alpha +
           r.specular_alpha + r.noise_alpha + r.rim_alpha + r.rim_width_px + r.glow_alpha +
           r.glow_width_share + std::abs(1.0f - r.top_shade) + r.top_lighten + r.rim_darken +
           r.body_lighten + r.bevel_alpha + r.bevel_share + std::abs(1.0f - r.saturation);
}

} // namespace

TEST_CASE("names round-trip, the six older names read as their new style, unknown are refused") {
    for (NoteStyle s : kAllNoteStyles) {
        NoteStyle back = NoteStyle::Solid;
        CHECK(parse_note_style(note_style_name(s), &back));
        CHECK(back == s);
    }
    const struct {
        const char* old_name;
        NoteStyle now;
    } kMoves[] = {{"filled", NoteStyle::Solid},  {"glassy", NoteStyle::Glass},
                  {"frosted", NoteStyle::Glass}, {"shiny", NoteStyle::Candy},
                  {"ghost", NoteStyle::Outline}, {"neon", NoteStyle::Neon}};
    for (const auto& m : kMoves) {
        CAPTURE(m.old_name);
        NoteStyle got = NoteStyle::Ink;
        CHECK(parse_note_style(m.old_name, &got));
        CHECK(got == m.now);
    }
    NoteStyle untouched = NoteStyle::Soft;
    CHECK_FALSE(parse_note_style("sparkly", &untouched));
    CHECK_FALSE(legacy_note_style("solid", &untouched)); // a current name is not an old one
    CHECK(untouched == NoteStyle::Soft);
}

TEST_CASE("each style has the trait that sets it apart") {
    const auto r = [](NoteStyle s) { return style_recipe(s, 0.6f); };
    const StyleRecipe solid = r(NoteStyle::Solid);
    CHECK(solid.body_alpha_bottom == 1.0f);
    CHECK(solid.top_shade == 1.0f); // flat
    CHECK(solid.corner_scale < 1.0f);
    CHECK(solid.rim_alpha == 0.0f);

    const StyleRecipe outline = r(NoteStyle::Outline);
    CHECK(outline.body_alpha_bottom < 0.25f); // hollow
    CHECK(outline.rim_width_px >= 3.5f);
    CHECK_FALSE(outline.rim_is_extra);

    const StyleRecipe glass = r(NoteStyle::Glass);
    CHECK(glass.body_alpha_top < glass.body_alpha_bottom);
    CHECK(glass.body_alpha_bottom < 1.0f);
    CHECK(glass.specular_alpha > 0.5f);

    const StyleRecipe neon = r(NoteStyle::Neon);
    CHECK(neon.core_darken > 0.8f);
    CHECK(neon.rim_lighten > 0.4f);
    CHECK_FALSE(neon.rim_is_extra);

    const StyleRecipe candy = r(NoteStyle::Candy);
    CHECK(candy.top_lighten > 0.3f);
    CHECK(candy.gloss_alpha > 0.4f);
    CHECK(candy.saturation > 1.2f);

    // Soft is pastel, matte and feathered, with small corners like the others (no capsule).
    const StyleRecipe soft = r(NoteStyle::Soft);
    CHECK(soft.body_lighten > 0.3f);
    CHECK(soft.corner_scale <= 1.0f);
    CHECK(soft.edge_feather_px > 2.0f);
    CHECK(soft.gloss_alpha == 0.0f);

    // Pixel is square and bevelled, and its beat lines are as faint as every other style's.
    const StyleRecipe pixel = r(NoteStyle::Pixel);
    CHECK(pixel.corner_scale == 0.0f);
    CHECK(pixel.edge_feather_px == 0.0f);
    CHECK(pixel.bevel_alpha > 0.0f);
    for (NoteStyle s : kAllNoteStyles) {
        CHECK(r(s).beat_line_alpha > 0.0f);
        CHECK(r(s).beat_line_alpha <= r(NoteStyle::Glass).beat_line_alpha);
        CHECK(r(s).beat_line_px <= r(NoteStyle::Glass).beat_line_px);
    }

    const StyleRecipe ink = r(NoteStyle::Ink);
    CHECK(ink.body_lighten > 0.6f); // paper
    CHECK(ink.rim_darken > 0.5f);   // dark ink line
    CHECK_FALSE(ink.rim_is_extra);
}

TEST_CASE("block glow: none at 0, growing to 100, the same at any intensity, neon's strongest") {
    for (NoteStyle s : kAllNoteStyles) {
        CAPTURE(note_style_name(s));
        CHECK(style_recipe(s, 0.6f, 0).glow_alpha == 0.0f);
        float last_alpha = -1.0f;
        float last_width = -1.0f;
        for (int g : {0, 1, 30, 70, 100}) {
            const StyleRecipe r = style_recipe(s, 0.6f, g);
            CHECK(r.glow_alpha >= last_alpha);
            CHECK(r.glow_width_share >= last_width);
            last_alpha = r.glow_alpha;
            last_width = r.glow_width_share;
            CHECK(style_recipe(s, 0.0f, g).glow_alpha == r.glow_alpha);
        }
        CHECK(style_recipe(s, 0.6f, 1).glow_alpha > 0.0f);
        CHECK(style_recipe(s, 0.6f, -5).glow_alpha == 0.0f);
        CHECK(style_recipe(s, 0.6f, 500).glow_alpha == style_recipe(s, 0.6f, 100).glow_alpha);
        CHECK(style_recipe(s, 0.6f, 30).glow_alpha <=
              style_recipe(NoteStyle::Neon, 0.6f, 30).glow_alpha);
        // The look carries it.
        LookSettings look;
        look.note_style = s;
        look.block_glow = 70;
        CHECK(style_recipe(look).glow_alpha ==
              style_recipe(s, look.style_intensity, 70).glow_alpha);
    }
    // At the default, neon's glow stays a halo, not a bloom: narrower than a white key.
    const StyleRecipe neon = style_recipe(NoteStyle::Neon, 0.6f, kDefaultBlockGlow);
    CHECK(neon.glow_alpha < 0.5f);
    CHECK(neon.glow_width_share < 0.5f);
}

TEST_CASE("intensity only ever strengthens a style, and is clamped") {
    for (NoteStyle s : kAllNoteStyles) {
        CAPTURE(note_style_name(s));
        float last = -1.0f;
        for (float k : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
            const float sum = strength_sum(style_recipe(s, k));
            CHECK(sum >= last);
            last = sum;
        }
        CHECK(strength_sum(style_recipe(s, 0.0f)) < strength_sum(style_recipe(s, 1.0f)));
        CHECK(strength_sum(style_recipe(s, -3.0f)) == strength_sum(style_recipe(s, 0.0f)));
        CHECK(strength_sum(style_recipe(s, 7.0f)) == strength_sum(style_recipe(s, 1.0f)));
        CHECK(strength_sum(style_recipe(s, std::nanf(""))) == strength_sum(style_recipe(s, 0.0f)));
    }
}

TEST_CASE("degrading never touches the body: notes and keys stay drawn at every level") {
    for (NoteStyle s : kAllNoteStyles) {
        CAPTURE(note_style_name(s));
        const StyleRecipe r = style_recipe(s, 0.8f);
        for (EffectsLevel level : kAllEffectsLevels) {
            CAPTURE(effects_level_name(level));
            const StyleRecipe d = degraded(r, effects_budget(level));
            CHECK(d.body_alpha_top == r.body_alpha_top);
            CHECK(d.body_alpha_bottom == r.body_alpha_bottom);
            CHECK(d.saturation == r.saturation);
            CHECK(d.core_darken == r.core_darken);
            CHECK(d.inner_glow_alpha == r.inner_glow_alpha);
            CHECK(d.inner_shade_alpha == r.inner_shade_alpha);
            CHECK(d.glow_alpha == r.glow_alpha);
            if (!r.rim_is_extra) {
                CHECK(d.rim_alpha == r.rim_alpha);
            }
            CHECK(d.body_alpha_bottom > 0.0f);
        }
    }
}

TEST_CASE("the extras go only at the last level, after the glow narrows") {
    const StyleRecipe r = style_recipe(NoteStyle::Glass, 1.0f);
    CHECK(degraded(r, effects_budget(EffectsLevel::NoMotion)).specular_alpha == r.specular_alpha);
    CHECK(degraded(r, effects_budget(EffectsLevel::NoMotion)).glow_width_share ==
          r.glow_width_share);
    const StyleRecipe narrow = degraded(r, effects_budget(EffectsLevel::NarrowGlow));
    CHECK(narrow.glow_width_share < r.glow_width_share);
    CHECK(narrow.specular_alpha == r.specular_alpha);
    const StyleRecipe floor = degraded(r, effects_budget(EffectsLevel::NoStyleExtras));
    CHECK(floor.specular_alpha == 0.0f);
    CHECK(floor.rim_alpha == 0.0f);
    CHECK(
        degraded(style_recipe(NoteStyle::Candy, 1.0f), effects_budget(EffectsLevel::NoStyleExtras))
            .gloss_alpha == 0.0f);
    // Outline, neon and ink keep their outline, and pixel its bevel and beat lines: they are what
    // the note is.
    for (NoteStyle s : {NoteStyle::Outline, NoteStyle::Neon, NoteStyle::Ink}) {
        CHECK(degraded(style_recipe(s, 1.0f), effects_budget(kLowestEffectsLevel)).rim_alpha >
              0.0f);
    }
    const StyleRecipe pixel = style_recipe(NoteStyle::Pixel, 1.0f);
    const StyleRecipe pixel_floor = degraded(pixel, effects_budget(kLowestEffectsLevel));
    CHECK(pixel_floor.bevel_alpha == pixel.bevel_alpha);
    CHECK(pixel_floor.beat_line_alpha == pixel.beat_line_alpha);
    CHECK(pixel_floor.corner_scale == pixel.corner_scale);
}

TEST_CASE("names stay readable on every style, scheme and preset") {
    for (Sample id : kAllSamples) {
        for (NoteStyle s : kAllNoteStyles) {
            for (float k : {0.0f, 0.6f, 1.0f}) {
                LookSettings look = sample_look(id);
                look.note_style = s;
                look.style_intensity = k;
                for (int note = 21; note <= 108; ++note) {
                    for (std::uint8_t v : {std::uint8_t{20}, std::uint8_t{100}}) {
                        const RGBA surface = style_surface_color(
                            note_recipe(look, note), note_color(look, note, v, Hand::Right),
                            kBackgroundColor);
                        const RGBA label = style_label_color(look, note, v, Hand::Right);
                        CHECK(contrast_ratio(label, surface) >= kLabelMinContrast);
                        // A lit key: the same style over its tinted underlay.
                        const RGBA c = note_color(look, note, v, Hand::Right);
                        const StyleRecipe r = note_recipe(look, note);
                        const RGBA key_surface =
                            style_surface_color(r, c, key_underlay_color(r, c));
                        CHECK(contrast_ratio(style_key_label_color(look, note, v, Hand::Right),
                                             key_surface) >= kLabelMinContrast);
                    }
                }
            }
        }
    }
}

TEST_CASE("translucent styles take light names on the dark background") {
    LookSettings look;
    look.right_color = {255, 220, 120, 255}; // a light color that takes black text when solid
    look.note_style = NoteStyle::Solid;
    CHECK(style_label_color(look, 64, 100, Hand::Right) == RGBA{0, 0, 0, 255});
    look.note_style = NoteStyle::Outline;
    CHECK(style_label_color(look, 64, 100, Hand::Right) == RGBA{255, 255, 255, 255});
    look.note_style = NoteStyle::Neon;
    CHECK(style_label_color(look, 64, 100, Hand::Right) == RGBA{255, 255, 255, 255});
}

TEST_CASE("black-key notes are darker than white-key notes in every style") {
    LookSettings look;
    for (NoteStyle s : kAllNoteStyles) {
        look.note_style = s;
        const StyleRecipe r = style_recipe(look);
        const RGBA white_key =
            style_surface_color(r, note_color(look, 60, 100, Hand::Right), kBackgroundColor);
        const RGBA black_key =
            style_surface_color(r, note_color(look, 61, 100, Hand::Right), kBackgroundColor);
        CHECK(relative_luminance(black_key) < relative_luminance(white_key));
    }
}

TEST_CASE("a lit key under a see-through style sits on a tinted underlay") {
    const RGBA blue{90, 170, 255, 255};
    for (NoteStyle s :
         {NoteStyle::Solid, NoteStyle::Candy, NoteStyle::Soft, NoteStyle::Pixel, NoteStyle::Ink}) {
        CHECK_FALSE(style_is_see_through(style_recipe(s, 0.6f)));
    }
    for (NoteStyle s : {NoteStyle::Glass, NoteStyle::Outline, NoteStyle::Neon}) {
        const StyleRecipe r = style_recipe(s, 0.6f);
        CHECK(style_is_see_through(r));
        const RGBA under = key_underlay_color(r, blue);
        CHECK(perceptual_distance(under, kBackgroundColor) > 0.05);
        CHECK(relative_luminance(under) < relative_luminance(blue));
    }
}

TEST_CASE("black keys' blocks are the fuller version of every style, and their names still read") {
    for (NoteStyle s : kAllNoteStyles) {
        for (float k : {0.0f, 0.6f, 1.0f}) {
            const StyleRecipe plain = style_recipe(s, k);
            const StyleRecipe full = fuller_recipe(plain);
            INFO("style " << note_style_name(s) << " intensity " << k);
            // More opaque (an opaque body stays opaque) and deeper; the rest is the same style.
            CHECK(full.body_alpha_top >= plain.body_alpha_top);
            CHECK(full.body_alpha_bottom >= plain.body_alpha_bottom);
            CHECK(full.body_alpha_bottom <= 1.0f);
            if (plain.body_alpha_bottom < 1.0f) {
                CHECK(full.body_alpha_bottom > plain.body_alpha_bottom);
            }
            CHECK(full.core_darken > plain.core_darken);
            CHECK(full.core_darken < 1.0f);
            CHECK(full.saturation == plain.saturation);
            CHECK(full.rim_alpha == plain.rim_alpha);
            CHECK(full.inner_glow_alpha == plain.inner_glow_alpha);
            CHECK(full.gloss_alpha == plain.gloss_alpha);
            CHECK(full.glow_alpha == plain.glow_alpha);
            // Only black keys take it.
            LookSettings look;
            look.note_style = s;
            look.style_intensity = k;
            CHECK(note_recipe(look, 61).core_darken == full.core_darken);
            CHECK(note_recipe(look, 60).core_darken == plain.core_darken);
            // The name on a black key's block reads on the fuller surface, for both hands and every
            // colour scheme: black or white text, at least the label minimum.
            for (ColorScheme scheme :
                 {ColorScheme::ByHand, ColorScheme::ByPitchClass, ColorScheme::ByOctave,
                  ColorScheme::ByVelocity, ColorScheme::Single}) {
                look.color_scheme = scheme;
                for (Hand hand : {Hand::Left, Hand::Right}) {
                    for (int note : {22, 49, 61, 63, 66, 68, 70, 94, 106}) {
                        const RGBA c = note_color(look, note, 100, hand);
                        const RGBA surface = style_surface_color(full, c, kBackgroundColor);
                        const RGBA label = style_label_color(look, note, 100, hand);
                        CHECK(contrast_ratio(label, surface) >= kLabelMinContrast);
                        // The outline drawn round the name is the opposite shade of the text.
                        const RGBA outline = label == RGBA{0, 0, 0, 255} ? RGBA{255, 255, 255, 255}
                                                                         : RGBA{0, 0, 0, 255};
                        CHECK(contrast_ratio(label, outline) >= 21.0 - 1e-6);
                    }
                }
            }
        }
    }
}

TEST_CASE("pale styles' black keys keep less of the lightening, and names read in high contrast") {
    for (NoteStyle c : kAllNoteStyles) {
        CAPTURE(note_style_name(c));
        const StyleRecipe plain = style_recipe(c, 0.6f);
        const StyleRecipe full = fuller_recipe(plain);
        CHECK(full.body_alpha_bottom >= plain.body_alpha_bottom);
        if (plain.body_alpha_bottom < 1.0f) {
            CHECK(full.body_alpha_bottom > plain.body_alpha_bottom);
        }
        CHECK(full.core_darken > plain.core_darken);
        CHECK(full.body_lighten <= plain.body_lighten);
        if (plain.body_lighten > 0.0f) {
            CHECK(full.body_lighten < plain.body_lighten); // the stronger tint
        }
        LookSettings look;
        look.note_style = c;
        CHECK(note_recipe(look, 61).core_darken == full.core_darken);
        CHECK(note_recipe(look, 60).core_darken == plain.core_darken);

        for (Sample id : kAllSamples) {
            for (float k : {0.0f, 1.0f}) {
                LookSettings p = sample_look(id);
                p.note_style = c;
                p.style_intensity = k;
                for (ColorScheme scheme : {ColorScheme::ByHand, ColorScheme::ByPitchClass}) {
                    p.color_scheme = scheme;
                    for (bool high : {false, true}) {
                        p.high_contrast = high;
                        for (Hand hand : {Hand::Left, Hand::Right}) {
                            for (int note = 21; note <= 108; ++note) {
                                const RGBA col = note_color(p, note, 100, hand);
                                const RGBA surface = style_surface_color(note_recipe(p, note), col,
                                                                         kBackgroundColor);
                                CHECK(contrast_ratio(style_label_color(p, note, 100, hand),
                                                     surface) >= kLabelMinContrast);
                            }
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("the two hands' blocks stay apart in every style") {
    for (NoteStyle c : kAllNoteStyles) {
        CAPTURE(note_style_name(c));
        LookSettings look;
        look.note_style = c;
        for (int note : {60, 61}) {
            const StyleRecipe r = note_recipe(look, note);
            const RGBA lc = note_color(look, note, 100, Hand::Left);
            const RGBA rc = note_color(look, note, 100, Hand::Right);
            // The body, or for a style whose colour is in its outline (outline, ink), the outline.
            double apart = perceptual_distance(style_surface_color(r, lc, kBackgroundColor),
                                               style_surface_color(r, rc, kBackgroundColor));
            if (r.rim_alpha >= 0.9f && r.rim_width_px >= 2.0f) {
                apart = std::max(apart, perceptual_distance(rim_colour(r, lc), rim_colour(r, rc)));
            }
            // Neon's colour is the glow inside its tube.
            if (r.inner_glow_alpha >= 0.3f) {
                apart =
                    std::max(apart, perceptual_distance(lighten(lc, 0.35f), lighten(rc, 0.35f)));
            }
            CHECK(apart > 0.1);
        }
    }
}
