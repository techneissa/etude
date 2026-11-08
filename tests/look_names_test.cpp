// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/look.hpp"
#include "etude/pitch.hpp"

#include <algorithm>
#include <string>
#include <vector>

#include <doctest/doctest.h>

using namespace etude;

namespace {

LookSettings names(NameMode mode, bool flats = false, bool octave = false) {
    LookSettings s;
    s.name_mode = mode;
    s.names_spelling = flats ? NameSpelling::Flats : NameSpelling::Sharps;
    s.names_show_octave = octave;
    return s;
}

} // namespace

TEST_CASE("letters with sharps") {
    const char* expected[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const LookSettings s = names(NameMode::Letters);
    for (int pc = 0; pc < 12; ++pc) {
        CHECK(note_label(60 + pc, s) == expected[pc]);
        CHECK(note_label(24 + pc, s) == expected[pc]);
    }
}

TEST_CASE("letters with flats") {
    const char* expected[12] = {"C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B"};
    const LookSettings s = names(NameMode::Letters, true);
    for (int pc = 0; pc < 12; ++pc) {
        CHECK(note_label(60 + pc, s) == expected[pc]);
    }
}

TEST_CASE("fixed-do solfege with sharps") {
    const char* expected[12] = {"Do",  "Do#", "Re",   "Re#", "Mi",  "Fa",
                                "Fa#", "Sol", "Sol#", "La",  "La#", "Si"};
    const LookSettings s = names(NameMode::Solfege);
    for (int pc = 0; pc < 12; ++pc) {
        CHECK(note_label(60 + pc, s) == expected[pc]);
    }
}

TEST_CASE("fixed-do solfege with flats") {
    const char* expected[12] = {"Do",   "Reb", "Re",  "Mib", "Mi",  "Fa",
                                "Solb", "Sol", "Lab", "La",  "Sib", "Si"};
    const LookSettings s = names(NameMode::Solfege, true);
    for (int pc = 0; pc < 12; ++pc) {
        CHECK(note_label(60 + pc, s) == expected[pc]);
    }
}

TEST_CASE("the octave is appended in scientific pitch notation") {
    CHECK(note_label(60, names(NameMode::Letters, false, true)) == "C4");
    CHECK(note_label(61, names(NameMode::Letters, false, true)) == "C#4");
    CHECK(note_label(61, names(NameMode::Letters, true, true)) == "Db4");
    CHECK(note_label(69, names(NameMode::Letters, false, true)) == "A4");
    CHECK(note_label(59, names(NameMode::Letters, false, true)) == "B3");
    CHECK(note_label(21, names(NameMode::Letters, false, true)) == "A0");
    CHECK(note_label(0, names(NameMode::Letters, false, true)) == "C-1");
    CHECK(note_label(127, names(NameMode::Letters, false, true)) == "G9");
    CHECK(note_label(60, names(NameMode::Solfege, false, true)) == "Do4");
    CHECK(note_label(70, names(NameMode::Solfege, true, true)) == "Sib4");
}

TEST_CASE("every pitch class with the octave, in both modes") {
    const char* letters[12] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const char* solfege[12] = {"Do",  "Do#", "Re",   "Re#", "Mi",  "Fa",
                               "Fa#", "Sol", "Sol#", "La",  "La#", "Si"};
    for (int pc = 0; pc < 12; ++pc) {
        CHECK(note_label(48 + pc, names(NameMode::Letters, false, true)) ==
              std::string(letters[pc]) + "3");
        CHECK(note_label(48 + pc, names(NameMode::Solfege, false, true)) ==
              std::string(solfege[pc]) + "3");
    }
}

TEST_CASE("names off or an invalid note give an empty label") {
    CHECK(note_label(60, names(NameMode::Off)).empty());
    CHECK(note_label(60, names(NameMode::Off, true, true)).empty());
    CHECK(note_label(-1, names(NameMode::Letters)).empty());
    CHECK(note_label(128, names(NameMode::Solfege)).empty());
}

TEST_CASE("the label does not depend on where names are drawn") {
    LookSettings s = names(NameMode::Letters);
    s.names_on_blocks = false;
    s.names_on_keys = false;
    CHECK(note_label(60, s) == "C"); // the renderer reads the where-flags, the text is the same
}

TEST_CASE("names are switched separately for blocks and keys") {
    LookSettings s = names(NameMode::Letters);
    const bool combos[4][2] = {{false, false}, {true, false}, {false, true}, {true, true}};
    for (const auto& c : combos) {
        s.names_on_blocks = c[0];
        s.names_on_keys = c[1];
        CHECK(s.shows_block_names() == c[0]);
        CHECK(s.shows_key_names() == c[1]);
    }
}

TEST_CASE("names off hides them everywhere, whatever the where-switches say") {
    LookSettings s = names(NameMode::Off);
    s.names_on_blocks = true;
    s.names_on_keys = true;
    CHECK_FALSE(s.shows_block_names());
    CHECK_FALSE(s.shows_key_names());
}

TEST_CASE("the short label is the letter or syllable, keeping a black key's accidental") {
    for (bool flats : {false, true}) {
        for (bool octave : {false, true}) {
            const LookSettings letters = names(NameMode::Letters, flats, octave);
            const LookSettings solfege = names(NameMode::Solfege, flats, octave);
            const char* sharp_letters[12] = {"C",  "C#", "D",  "D#", "E",  "F",
                                             "F#", "G",  "G#", "A",  "A#", "B"};
            const char* flat_letters[12] = {"C",  "Db", "D",  "Eb", "E",  "F",
                                            "Gb", "G",  "Ab", "A",  "Bb", "B"};
            const char* sharp_solfege[12] = {"Do",  "Do#", "Re",   "Re#", "Mi",  "Fa",
                                             "Fa#", "Sol", "Sol#", "La",  "La#", "Si"};
            const char* flat_solfege[12] = {"Do",   "Reb", "Re",  "Mib", "Mi",  "Fa",
                                            "Solb", "Sol", "Lab", "La",  "Sib", "Si"};
            for (int pc = 0; pc < 12; ++pc) {
                CHECK(note_short_label(60 + pc, letters) ==
                      (flats ? flat_letters[pc] : sharp_letters[pc]));
                CHECK(note_short_label(24 + pc, solfege) ==
                      (flats ? flat_solfege[pc] : sharp_solfege[pc]));
            }
        }
    }
    // Never the octave, whatever the numbering.
    CHECK(note_short_label(61, names(NameMode::Letters, false, true)) == "C#");
    CHECK(note_short_label(0, names(NameMode::Letters, false, true)) == "C"); // C-1
}

TEST_CASE("a black key's short label never drops the accidental, in any naming scheme") {
    // C# must not read as C: that names the wrong key. Every note 0..127, every scheme and
    // spelling, with and without the octave number, and in both octave numberings.
    for (NameMode mode : {NameMode::Letters, NameMode::Solfege}) {
        for (bool flats : {false, true}) {
            for (bool octave : {false, true}) {
                for (OctaveNumbering numbering :
                     {OctaveNumbering::Scientific, OctaveNumbering::Yamaha}) {
                    LookSettings s = names(mode, flats, octave);
                    s.octave_numbering = numbering;
                    for (int note = 0; note < 128; ++note) {
                        const std::string full = note_label(note, s);
                        const std::string shortest = note_short_label(note, s);
                        REQUIRE_FALSE(shortest.empty());
                        const bool has_accidental =
                            shortest.find('#') != std::string::npos || shortest.back() == 'b';
                        CHECK(has_accidental == is_black_key(note));
                        // It is the full name less the octave number: a prefix of it, with no
                        // digit.
                        CHECK(full.compare(0, shortest.size(), shortest) == 0);
                        CHECK(shortest.find_first_of("-0123456789") == std::string::npos);
                        // Two different keys of one octave never share a short label.
                        if (note + 1 < 128) {
                            CHECK(shortest != note_short_label(note + 1, s));
                        }
                    }
                }
            }
        }
    }
}

TEST_CASE("the short label is empty with names off or outside 0..127") {
    CHECK(note_short_label(60, names(NameMode::Off, false, false)).empty());
    CHECK(note_short_label(-1, names(NameMode::Letters, false, false)).empty());
    CHECK(note_short_label(128, names(NameMode::Letters, false, false)).empty());
}

TEST_CASE("a name comes in three pieces: letter, accidental, octave") {
    LookSettings s = names(NameMode::Letters, false, true);
    NameParts p = note_name_parts(61, s); // C#4
    CHECK(p.letter == "C");
    CHECK(p.accidental == "#");
    CHECK(p.octave == "4");
    s.names_spelling = NameSpelling::Flats;
    p = note_name_parts(61, s);
    CHECK(p.letter == "D");
    CHECK(p.accidental == "b");
    s.name_mode = NameMode::Solfege;
    p = note_name_parts(68, s); // Lab4
    CHECK(p.letter == "La");
    CHECK(p.accidental == "b");
    s.names_spelling = NameSpelling::Sharps;
    p = note_name_parts(68, s); // Sol#4
    CHECK(p.letter == "Sol");
    CHECK(p.accidental == "#");
    // A white key has no accidental; the octave is empty unless it is shown, and may be negative.
    p = note_name_parts(60, names(NameMode::Letters));
    CHECK(p.letter == "C");
    CHECK(p.accidental.empty());
    CHECK(p.octave.empty());
    CHECK(note_name_parts(0, names(NameMode::Letters, false, true)).octave == "-1");
    // Off and out-of-range notes have no pieces.
    CHECK(note_name_parts(60, names(NameMode::Off)).letter.empty());
    CHECK(note_name_parts(-1, names(NameMode::Letters)).letter.empty());
    CHECK(note_name_parts(128, names(NameMode::Solfege)).letter.empty());
}

TEST_CASE("the full and short labels are the pieces end to end") {
    for (NameMode mode : {NameMode::Letters, NameMode::Solfege}) {
        for (bool flats : {false, true}) {
            for (bool octave : {false, true}) {
                const LookSettings s = names(mode, flats, octave);
                for (int note = 0; note < 128; ++note) {
                    const NameParts p = note_name_parts(note, s);
                    CHECK(note_label(note, s) == p.letter + p.accidental + p.octave);
                    CHECK(note_short_label(note, s) == p.letter + p.accidental);
                    CHECK(p.accidental.empty() == !is_black_key(note));
                }
            }
        }
    }
}

TEST_CASE("possible name pieces cover every piece any naming settings give, once each") {
    const std::vector<std::string> pieces = possible_name_pieces();
    const auto has = [&](const std::string& piece) {
        return std::find(pieces.begin(), pieces.end(), piece) != pieces.end();
    };
    for (const std::string& piece : pieces) {
        CHECK_FALSE(piece.empty());
        CHECK(std::count(pieces.begin(), pieces.end(), piece) == 1);
    }
    for (NameMode mode : {NameMode::Letters, NameMode::Solfege}) {
        for (bool flats : {false, true}) {
            for (OctaveNumbering numbering :
                 {OctaveNumbering::Scientific, OctaveNumbering::Yamaha}) {
                LookSettings s = names(mode, flats, true);
                s.octave_numbering = numbering;
                for (int note = 0; note < 128; ++note) {
                    const NameParts p = note_name_parts(note, s);
                    CHECK(has(p.letter));
                    CHECK(has(p.octave));
                }
            }
        }
    }
    // The ends: the lowest Yamaha octave, the highest octave, the longest syllable.
    CHECK(has("-2"));
    CHECK(has("9"));
    CHECK(has("Sol"));
    CHECK(has("B"));
    CHECK_FALSE(has("#")); // the accidental is drawn, not set in text
}

TEST_CASE("By key is settled by the key being played; Sharps and Flats stay") {
    LookSettings by_key;
    by_key.name_mode = NameMode::Letters;
    REQUIRE(by_key.names_spelling == NameSpelling::ByKey);
    // Unsettled, it spells with sharps.
    CHECK(note_label(63, by_key) == "D#");
    const LookSettings flat_key = with_spelling_for_key(by_key, true);
    const LookSettings sharp_key = with_spelling_for_key(by_key, false);
    CHECK(flat_key.names_spelling == NameSpelling::Flats);
    CHECK(sharp_key.names_spelling == NameSpelling::Sharps);
    // E flat major's black keys: E flat, B flat, A flat; G major's: F sharp.
    CHECK(note_label(63, flat_key) == "Eb");
    CHECK(note_label(70, flat_key) == "Bb");
    CHECK(note_label(68, flat_key) == "Ab");
    CHECK(note_label(66, sharp_key) == "F#");
    CHECK(note_label(61, sharp_key) == "C#");
    // Do Re Mi follow the same way.
    LookSettings solfege = by_key;
    solfege.name_mode = NameMode::Solfege;
    CHECK(note_label(63, with_spelling_for_key(solfege, true)) == "Mib");
    CHECK(note_label(66, with_spelling_for_key(solfege, false)) == "Fa#");
    // A look that chose is not moved by the key.
    LookSettings sharps = by_key;
    sharps.names_spelling = NameSpelling::Sharps;
    LookSettings flats = by_key;
    flats.names_spelling = NameSpelling::Flats;
    CHECK(with_spelling_for_key(sharps, true) == sharps);
    CHECK(with_spelling_for_key(flats, false) == flats);
    CHECK(note_label(63, with_spelling_for_key(sharps, true)) == "D#");
    CHECK(note_label(63, with_spelling_for_key(flats, false)) == "Eb");
    // The pieces any spelling can need are the same set, so a key change measures nothing new.
    const std::vector<std::string> pieces = possible_name_pieces();
    for (const LookSettings& l : {flat_key, sharp_key}) {
        for (int note = 0; note < 128; ++note) {
            const NameParts p = note_name_parts(note, l);
            CHECK(std::find(pieces.begin(), pieces.end(), p.letter) != pieces.end());
        }
    }
}

TEST_CASE("the sheet music's names are one choice: none, letters or solfege") {
    LookSettings s;
    s.names_on_sheet = false;
    s.sheet_naming = SheetNaming::Solfege;
    CHECK(sheet_names_choice(s) == 0);
    set_sheet_names_choice(s, 1);
    CHECK(s.names_on_sheet);
    CHECK(s.sheet_naming == SheetNaming::Letters);
    CHECK(sheet_names_choice(s) == 1);
    set_sheet_names_choice(s, 2);
    CHECK(s.sheet_naming == SheetNaming::Solfege);
    CHECK(sheet_names_choice(s) == 2);
    // Off keeps the naming, so turning names on again brings back the last one.
    set_sheet_names_choice(s, 0);
    CHECK_FALSE(s.names_on_sheet);
    CHECK(s.sheet_naming == SheetNaming::Solfege);
    CHECK(sheet_names_choice(s) == 0);
}
