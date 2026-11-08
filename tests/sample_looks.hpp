// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>

#include "etude/color.hpp"
#include "etude/look.hpp"
#include "etude/palette.hpp"

// Looks that differ from the default in many options at once, for tests that must hold over a
// spread of looks (the app has no built-in looks; these are test fixtures only).
namespace etude::test {

enum class Sample : std::uint8_t { Classic, NeonNight, Glass, FrostedPastel, HighContrast, Minimal };
inline constexpr std::array<Sample, 6> kAllSamples{Sample::Classic,       Sample::NeonNight,
                                                   Sample::Glass,         Sample::FrostedPastel,
                                                   Sample::HighContrast,  Sample::Minimal};

inline LookSettings sample_look(Sample id) {
    LookSettings s;
    switch (id) {
    case Sample::Classic:
        s.note_style = NoteStyle::Solid;
        s.style_intensity = 1.0f;
        s.name_mode = NameMode::Off;
        s.held_glow = false;
        s.hit_effect = HitEffect::None;
        s.trail_fade = false;
        s.background = BackgroundStyle::Plain;
        break;
    case Sample::NeonNight:
        s.note_style = NoteStyle::Neon;
        s.style_intensity = 0.85f;
        s.block_glow = 55;
        s.color_scheme = ColorScheme::ByPitchClass;
        s.name_mode = NameMode::Off;
        s.background = BackgroundStyle::Motion;
        s.hit_effect = HitEffect::Sparks;
        s.visible_seconds = 4.5f;
        break;
    case Sample::Glass:
        s.note_style = NoteStyle::Glass;
        s.style_intensity = 0.8f;
        s.left_color = {94, 234, 212, 255};
        s.right_color = {130, 170, 255, 255};
        s.hit_effect = HitEffect::Ripple;
        break;
    case Sample::FrostedPastel:
        s.note_style = NoteStyle::Soft;
        s.style_intensity = 0.7f;
        s.color_scheme = ColorScheme::ByPitchClass;
        for (std::size_t i = 0; i < s.pitch_colors.size(); ++i) {
            s.pitch_colors[i] = from_oklch(0.86, 0.07, 25.0 + 30.0 * static_cast<double>(i));
        }
        s.palettes[static_cast<std::size_t>(ColorScheme::ByPitchClass)] = Palette::Pastel;
        s.name_mode = NameMode::Solfege;
        s.hit_effect = HitEffect::Glow;
        s.grade_opacity = 80;
        break;
    case Sample::HighContrast:
        s.note_style = NoteStyle::Solid;
        s.style_intensity = 1.0f;
        s.left_color = {255, 200, 60, 255};
        s.right_color = {90, 210, 255, 255};
        s.high_contrast = true;
        s.names_show_octave = true;
        s.held_glow = false;
        s.hit_effect = HitEffect::None;
        s.trail_fade = false;
        s.background = BackgroundStyle::Plain;
        s.chord_label_size = 1.5f;
        break;
    case Sample::Minimal:
        s.note_style = NoteStyle::Solid;
        s.style_intensity = 0.0f;
        s.name_mode = NameMode::Off;
        s.key_flash = false;
        s.held_glow = false;
        s.hit_effect = HitEffect::None;
        s.trail_fade = false;
        s.background = BackgroundStyle::Plain;
        s.reduced_effects = true;
        s.visible_seconds = 5.0f;
        s.grade_opacity = 60;
        break;
    }
    return s;
}

} // namespace etude::test
