// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/saved_palettes.hpp"

#include <string>

#include <doctest/doctest.h>

#include "etude/look.hpp"
#include "etude/palette.hpp"

using namespace etude;

namespace {

LookSettings note_look() {
    LookSettings s;
    s.color_scheme = ColorScheme::ByPitchClass;
    return s;
}

// The note colours as someone's own: every note a distinct, recognisable colour.
void paint_notes(LookSettings& look, std::uint8_t base) {
    for (std::size_t i = 0; i < look.pitch_colors.size(); ++i) {
        look.pitch_colors[i] = RGBA{static_cast<std::uint8_t>(base + i), 40,
                                    static_cast<std::uint8_t>(200 - i), 255};
    }
}

} // namespace

TEST_CASE("a look has two empty saved palettes, and the notes come from no slot") {
    const LookSettings s;
    for (int slot = 0; slot < kSavedPalettes; ++slot) {
        CHECK_FALSE(saved_palette_filled(s, slot));
    }
    CHECK(s.note_slot == -1);
    CHECK_FALSE(saved_palette_filled(s, -1));
    CHECK_FALSE(saved_palette_filled(s, kSavedPalettes));
}

TEST_CASE("saving puts the twelve note colours as they are now in the slot, and only that slot") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    REQUIRE(save_palette_slot(s, 1));
    CHECK(saved_palette_filled(s, 1));
    CHECK_FALSE(saved_palette_filled(s, 0));
    CHECK(s.saved_palettes[1].colors == s.pitch_colors);
    CHECK(s.note_slot == 1); // the notes now are that palette

    paint_notes(s, 90);
    REQUIRE(save_palette_slot(s, 1)); // a filled slot is saved over
    CHECK(s.saved_palettes[1].colors == s.pitch_colors);
    CHECK_FALSE(save_palette_slot(s, 2));
    CHECK_FALSE(save_palette_slot(s, -1));
}

TEST_CASE("applying a slot sets the note colours like a palette, and an empty slot does nothing") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    const auto mine = s.pitch_colors;
    apply_palette(s, Palette::Vivid);
    CHECK(s.pitch_colors != mine);
    CHECK(s.note_slot == -1); // a built-in palette is the notes' palette again

    const LookSettings before = s;
    CHECK_FALSE(apply_saved_palette(s, 1)); // empty
    CHECK(s == before);

    REQUIRE(apply_saved_palette(s, 0));
    CHECK(s.pitch_colors == mine);
    CHECK(s.note_slot == 0);
    CHECK(s.left_color == before.left_color);
    CHECK(s.octave_colors == before.octave_colors);
    const ActivePalette active = active_palette(s, ColorScheme::ByPitchClass);
    CHECK(active.slot == 0);
    CHECK_FALSE(active.built_in.has_value());
}

TEST_CASE("clearing empties the slot, keeps the colours on the notes and drops the slot as their palette") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    paint_notes(s, 50);
    save_palette_slot(s, 1);
    const auto notes = s.pitch_colors;
    clear_palette_slot(s, 1);
    CHECK_FALSE(saved_palette_filled(s, 1));
    CHECK(saved_palette_filled(s, 0));
    CHECK(s.saved_palettes[1] == SavedPalette{});
    CHECK(s.pitch_colors == notes);
    CHECK(s.note_slot == -1);
    CHECK(active_palette(s, ColorScheme::ByPitchClass).custom());

    clear_palette_slot(s, 5); // not a slot: nothing happens
    CHECK(saved_palette_filled(s, 0));
}

TEST_CASE("the slots are saved with the look; a file from before them loads as two empty slots") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 1);
    ParseReport report;
    const LookSettings back = parse_look(serialize(s), &report);
    CHECK(report.clean());
    CHECK(back == s);
    CHECK(saved_palette_filled(back, 1));
    CHECK(back.note_slot == 1);
    CHECK(back.saved_palettes[1].colors == s.pitch_colors);

    // An older file: no saved palette keys at all.
    ParseReport old_report;
    const LookSettings old = parse_look("version = 1\nnote_style = solid\n", &old_report);
    CHECK(old_report.clean());
    CHECK_FALSE(saved_palette_filled(old, 0));
    CHECK_FALSE(saved_palette_filled(old, 1));
    CHECK(old.note_slot == -1);
    CHECK(old.saved_palettes == LookSettings{}.saved_palettes);

    // Cleared, it is saved as empty and loads empty.
    clear_palette_slot(s, 1);
    const std::string text = serialize(s);
    CHECK(text.find("saved_palette_2 = false\n") != std::string::npos);
    CHECK(parse_look(text) == s);
}

TEST_CASE("a file that names colours for an empty slot, or a slot that is empty, is made sound") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    std::string text = serialize(s);
    // Slot 1 unfilled but with colours in the file: the colours are dropped.
    const std::string key = "saved_palette_1 = true\n";
    REQUIRE(text.find(key) != std::string::npos);
    text.replace(text.find(key), key.size(), "saved_palette_1 = false\n");
    const LookSettings emptied = parse_look(text);
    CHECK_FALSE(saved_palette_filled(emptied, 0));
    CHECK(emptied.saved_palettes[0] == SavedPalette{});
    CHECK(emptied.note_slot == -1); // it named a slot that is empty

    // A slot number out of range is clamped, then dropped: the notes came from no slot.
    CHECK(parse_look("note_palette_slot = 7\n").note_slot == -1);
}

TEST_CASE("Reset all keeps the saved palettes, and the preset slots carry them") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    const LookSettings reset = reset_look(s);
    CHECK(saved_palette_filled(reset, 0));
    CHECK(reset.saved_palettes == s.saved_palettes);
    CHECK(reset.note_slot == -1);
    CHECK(reset.pitch_colors == LookSettings{}.pitch_colors);
}

TEST_CASE("a note colour's own reset puts back that note from the current palette and no other") {
    LookSettings s = note_look();
    apply_palette(s, Palette::Ocean);
    const auto ocean = s.pitch_colors;
    s.pitch_colors[1] = RGBA{1, 2, 3, 255};  // C#
    s.pitch_colors[2] = RGBA{4, 5, 6, 255};  // D
    CHECK_FALSE(swatch_is_palette_colour(s, ColorScheme::ByPitchClass, 1));
    CHECK(palette_swatch_color(s, ColorScheme::ByPitchClass, 1) == ocean[1]);

    REQUIRE(reset_swatch(s, ColorScheme::ByPitchClass, 1));
    CHECK(s.pitch_colors[1] == ocean[1]);
    CHECK(s.pitch_colors[2] == RGBA{4, 5, 6, 255}); // D keeps its colour
    for (std::size_t i = 0; i < 12; ++i) {
        if (i != 2) {
            CHECK(s.pitch_colors[i] == ocean[i]);
        }
    }
    CHECK_FALSE(reset_swatch(s, ColorScheme::ByPitchClass, 1)); // it already is
    CHECK_FALSE(reset_swatch(s, ColorScheme::ByPitchClass, 12));
    CHECK_FALSE(reset_swatch(s, ColorScheme::ByPitchClass, -1));
}

TEST_CASE("the reset of a note that came from a saved palette goes back to that palette's colour") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 1);
    const auto mine = s.pitch_colors;
    s.pitch_colors[7] = RGBA{9, 9, 9, 255};
    CHECK(palette_swatch_color(s, ColorScheme::ByPitchClass, 7) == mine[7]);
    REQUIRE(reset_swatch(s, ColorScheme::ByPitchClass, 7));
    CHECK(s.pitch_colors == mine);

    // The whole-scheme Reset goes back to the slot too.
    s.pitch_colors[0] = RGBA{8, 8, 8, 255};
    reset_to_palette(s);
    CHECK(s.pitch_colors == mine);

    // With the slot cleared the palette is the built-in one the notes had before.
    clear_palette_slot(s, 1);
    s.pitch_colors[3] = RGBA{7, 7, 7, 255};
    CHECK(palette_swatch_color(s, ColorScheme::ByPitchClass, 3) ==
          palette_swatches(Palette::Classic, ColorScheme::ByPitchClass).colors[3]);
}

TEST_CASE("every scheme's swatches reset one at a time") {
    for (const ColorScheme scheme : kAllColorSchemes) {
        LookSettings s;
        s.color_scheme = scheme;
        apply_palette(s, Palette::Sunset);
        const Swatches sunset = scheme_swatches(s, scheme);
        for (int i = 0; i < sunset.count; ++i) {
            CHECK(set_swatch_color(s, scheme, i, RGBA{static_cast<std::uint8_t>(i), 1, 2, 255}));
        }
        for (int i = 0; i < sunset.count; ++i) {
            REQUIRE(reset_swatch(s, scheme, i));
            CHECK(swatch_color(s, scheme, i) == sunset.colors[static_cast<std::size_t>(i)]);
            // Every later swatch is still the changed one.
            for (int j = i + 1; j < sunset.count; ++j) {
                CHECK(swatch_color(s, scheme, j) == RGBA{static_cast<std::uint8_t>(j), 1, 2, 255});
            }
        }
        CHECK(scheme_swatches(s, scheme) == sunset);
    }
}

TEST_CASE("a saved palette's strip shows its twelve colours, an empty one none") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    CHECK(saved_palette_swatches(s, 0).count == 0);
    save_palette_slot(s, 0);
    const Swatches strip = saved_palette_swatches(s, 0);
    REQUIRE(strip.count == 12);
    CHECK(strip.colors[3] == s.pitch_colors[3]);
}

TEST_CASE("undo of a save over a slot puts the slot and the notes' palette back") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    const auto first = s.pitch_colors;
    apply_palette(s, Palette::Vivid);
    const SavedPaletteState before = saved_palette_state(s);
    REQUIRE(before.note_slot == -1);
    paint_notes(s, 90);
    REQUIRE(save_palette_slot(s, 0)); // the overwrite
    CHECK(s.saved_palettes[0].colors != first);
    restore_saved_palettes(s, before);
    CHECK(s.saved_palettes[0].colors == first);
    CHECK(s.note_slot == -1);
    CHECK(saved_palette_filled(s, 0));
}

TEST_CASE("undo of a clear brings the slot back, and the other slot is untouched") {
    LookSettings s = note_look();
    paint_notes(s, 10);
    save_palette_slot(s, 0);
    paint_notes(s, 50);
    save_palette_slot(s, 1);
    const SavedPaletteState before = saved_palette_state(s);
    clear_palette_slot(s, 1);
    REQUIRE_FALSE(saved_palette_filled(s, 1));
    restore_saved_palettes(s, before);
    CHECK(s == [&] { LookSettings t = s; return t; }());
    CHECK(saved_palette_filled(s, 1));
    CHECK(s.note_slot == 1);
    CHECK(s.saved_palettes[1].colors == s.pitch_colors);
    CHECK(saved_palette_filled(s, 0));
    // A state naming a slot that is empty settles to no slot rather than a dangling one.
    SavedPaletteState odd;
    odd.note_slot = 1;
    restore_saved_palettes(s, odd);
    CHECK(s.note_slot == -1);
}
