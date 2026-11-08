// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <optional>

#include "etude/color.hpp"
#include "etude/look.hpp"
#include "etude/palette.hpp"

namespace etude {

// The two palettes of the player's own (`LookSettings::saved_palettes`). A slot holds the twelve
// note colours as they were when it was saved, so it is a palette of the Note scheme: the Effects
// page lists the slots after the built-in palettes while Colour by is Note. They are saved with
// the look like the note colours, an older file has two empty slots, and "Reset all" keeps them.

// What a slot holds. An empty slot, or a slot outside 0..kSavedPalettes - 1, is not filled.
bool saved_palette_filled(const LookSettings& look, int slot);
// Stores the note colours as they are now in `slot` (replacing what it held) and makes it the
// palette the note colours came from, so Reset goes back to it. False for a slot that is not one.
bool save_palette_slot(LookSettings& look, int slot);
// Puts the slot's colours on the notes and makes it their palette, like `apply_palette`. False,
// nothing changed, for an empty slot.
bool apply_saved_palette(LookSettings& look, int slot);
// Empties the slot. When the notes' palette was that slot it goes back to the built-in palette
// they had before (`LookSettings::palettes`), the colours themselves staying as they are.
void clear_palette_slot(LookSettings& look, int slot);
// The slots as they were, so a save over a slot or a clear can be taken back (the Undo of its toast).
struct SavedPaletteState {
    std::array<SavedPalette, kSavedPalettes> slots{};
    int note_slot = -1;
};
SavedPaletteState saved_palette_state(const LookSettings& look);
// Puts the slots and the slot the notes came from back; the note colours themselves stay.
void restore_saved_palettes(LookSettings& look, const SavedPaletteState& state);

// A slot's swatches as a palette strip shows them (the twelve note colours).
Swatches saved_palette_swatches(const LookSettings& look, int slot);

// The palette a scheme's colours are now: a built-in one, a saved slot (the Note scheme only, when
// the notes came from it and still are its colours), or none (Custom).
struct ActivePalette {
    std::optional<Palette> built_in;
    int slot = -1;
    bool custom() const { return !built_in && slot < 0; }
};
ActivePalette active_palette(const LookSettings& look, ColorScheme scheme);

// Brings the slots back to a state the app can hold: an empty slot has the default colours, every
// colour is opaque, and the slot the notes came from is a filled one (else -1).
void settle_saved_palettes(LookSettings& look);

// --- A swatch's own reset ----------------------------------------------------------------------

// The colour swatch `index` of `scheme` has in the palette the scheme's colours came from: the
// remembered built-in palette's, or for the Note scheme the saved slot they came from.
RGBA palette_swatch_color(const LookSettings& look, ColorScheme scheme, int index);
// Whether swatch `index` already is that colour.
bool swatch_is_palette_colour(const LookSettings& look, ColorScheme scheme, int index);
// Puts that one swatch back, leaving every other as it is. False for an index the scheme does not
// have, or when it already is the palette's colour.
bool reset_swatch(LookSettings& look, ColorScheme scheme, int index);

} // namespace etude
