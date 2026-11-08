// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/saved_palettes.hpp"

namespace etude {
namespace {

bool valid_slot(int slot) { return slot >= 0 && slot < kSavedPalettes; }

RGBA opaque_of(RGBA c) {
    c.a = 255;
    return c;
}

constexpr std::size_t scheme_slot(ColorScheme s) { return static_cast<std::size_t>(s); }

// The slot the note colours came from, when it still holds colours.
std::optional<int> note_source_slot(const LookSettings& look) {
    if (saved_palette_filled(look, look.note_slot)) {
        return look.note_slot;
    }
    return std::nullopt;
}

} // namespace

bool saved_palette_filled(const LookSettings& look, int slot) {
    return valid_slot(slot) && look.saved_palettes[static_cast<std::size_t>(slot)].filled;
}

bool save_palette_slot(LookSettings& look, int slot) {
    if (!valid_slot(slot)) {
        return false;
    }
    SavedPalette& saved = look.saved_palettes[static_cast<std::size_t>(slot)];
    saved.filled = true;
    for (std::size_t i = 0; i < saved.colors.size(); ++i) {
        saved.colors[i] = opaque_of(look.pitch_colors[i]);
    }
    look.note_slot = slot;
    return true;
}

bool apply_saved_palette(LookSettings& look, int slot) {
    if (!saved_palette_filled(look, slot)) {
        return false;
    }
    look.pitch_colors = look.saved_palettes[static_cast<std::size_t>(slot)].colors;
    look.note_slot = slot;
    return true;
}

void clear_palette_slot(LookSettings& look, int slot) {
    if (!valid_slot(slot)) {
        return;
    }
    look.saved_palettes[static_cast<std::size_t>(slot)] = {};
    if (look.note_slot == slot) {
        look.note_slot = -1;
    }
}

SavedPaletteState saved_palette_state(const LookSettings& look) {
    return {look.saved_palettes, look.note_slot};
}

void restore_saved_palettes(LookSettings& look, const SavedPaletteState& state) {
    look.saved_palettes = state.slots;
    look.note_slot = state.note_slot;
    settle_saved_palettes(look);
}

Swatches saved_palette_swatches(const LookSettings& look, int slot) {
    Swatches s;
    if (!saved_palette_filled(look, slot)) {
        return s;
    }
    s.colors = {};
    const auto& colors = look.saved_palettes[static_cast<std::size_t>(slot)].colors;
    for (std::size_t i = 0; i < colors.size(); ++i) {
        s.colors[i] = colors[i];
    }
    s.count = static_cast<int>(colors.size());
    return s;
}

void settle_saved_palettes(LookSettings& look) {
    for (SavedPalette& saved : look.saved_palettes) {
        if (!saved.filled) {
            saved = {};
        }
        for (RGBA& c : saved.colors) {
            c = opaque_of(c);
        }
    }
    if (look.note_slot != -1 && !saved_palette_filled(look, look.note_slot)) {
        look.note_slot = -1;
    }
}

ActivePalette active_palette(const LookSettings& look, ColorScheme scheme) {
    ActivePalette out;
    if (scheme == ColorScheme::ByPitchClass) {
        if (const std::optional<int> slot = note_source_slot(look)) {
            if (saved_palette_swatches(look, *slot) == scheme_swatches(look, scheme)) {
                out.slot = *slot;
                return out;
            }
        }
    }
    out.built_in = matching_palette(look, scheme);
    return out;
}

RGBA palette_swatch_color(const LookSettings& look, ColorScheme scheme, int index) {
    if (scheme == ColorScheme::ByPitchClass) {
        if (const std::optional<int> slot = note_source_slot(look)) {
            if (index >= 0 && index < 12) {
                return look.saved_palettes[static_cast<std::size_t>(*slot)]
                    .colors[static_cast<std::size_t>(index)];
            }
        }
    }
    const Swatches base = palette_swatches(look.palettes[scheme_slot(scheme)], scheme);
    if (index < 0 || index >= base.count) {
        return RGBA{0, 0, 0, 255};
    }
    return base.colors[static_cast<std::size_t>(index)];
}

bool swatch_is_palette_colour(const LookSettings& look, ColorScheme scheme, int index) {
    return swatch_color(look, scheme, index) == palette_swatch_color(look, scheme, index);
}

bool reset_swatch(LookSettings& look, ColorScheme scheme, int index) {
    if (index < 0 || index >= swatch_count(scheme) || swatch_is_palette_colour(look, scheme, index)) {
        return false;
    }
    return set_swatch_color(look, scheme, index, palette_swatch_color(look, scheme, index));
}

} // namespace etude
