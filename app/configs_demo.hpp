// SPDX-License-Identifier: GPL-3.0-or-later
// Developer: looks that differ at a glance, for the Configs page's shots (--configs-demo fills the
// three saved configs with the first three; --profiles-demo gives each other profile one of the
// rest). Not used anywhere else.
#pragma once

#include "etude/look.hpp"
#include "etude/palette.hpp"

#include <cstddef>

namespace etude::app {

inline LookSettings configs_demo_look(std::size_t index) {
    LookSettings look;
    switch (index % 6) {
    case 0: // neon notes, a colour for each note
        look.note_style = NoteStyle::Neon;
        look.style_intensity = 0.85f;
        look.block_glow = 55;
        look.color_scheme = ColorScheme::ByPitchClass;
        apply_palette(look, Palette::Vivid);
        look.hit_effect = HitEffect::Sparks;
        break;
    case 1: // solid notes, yellow and pink hands
        look.note_style = NoteStyle::Solid;
        look.style_intensity = 1.0f;
        look.left_color = {255, 214, 10, 255};
        look.right_color = {255, 69, 120, 255};
        look.hit_effect = HitEffect::Ripple;
        break;
    case 2: // soft pastel notes, a colour for each note
        look.note_style = NoteStyle::Soft;
        look.style_intensity = 0.7f;
        look.color_scheme = ColorScheme::ByPitchClass;
        apply_palette(look, Palette::Pastel);
        look.hit_effect = HitEffect::Glow;
        break;
    case 3: // candy notes by octave
        look.note_style = NoteStyle::Candy;
        look.color_scheme = ColorScheme::ByOctave;
        break;
    case 4: // pixel notes, soft to hard
        look.note_style = NoteStyle::Pixel;
        look.color_scheme = ColorScheme::ByVelocity;
        look.hit_effect = HitEffect::Glow;
        break;
    default: // ink notes in one colour
        look.note_style = NoteStyle::Ink;
        look.color_scheme = ColorScheme::Single;
        look.single_color = {255, 120, 150, 255};
        break;
    }
    return look;
}

} // namespace etude::app
