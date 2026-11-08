// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/scale_overlay.hpp"

#include "etude/exercises.hpp"
#include "etude/session_state.hpp"
#include "etude/settings_doc.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("scale overlay: the mask is the core scale's, for a few keys and types") {
    const struct {
        int root;
        ScaleType type;
    } cases[] = {{3, ScaleType::Major},         {9, ScaleType::NaturalMinor},
                 {0, ScaleType::Blues},         {7, ScaleType::Dorian},
                 {6, ScaleType::WholeTone},     {2, ScaleType::MajorPentatonic},
                 {11, ScaleType::HarmonicMinor}};
    for (const auto& c : cases) {
        const ScaleOverlayMask m = make_scale_overlay({true, c.root, c.type, false});
        CHECK(m.on);
        CHECK(m.pitch_classes == scale_pitch_class_mask(c.root, c.type));
        for (int note = 0; note < 128; ++note) {
            CHECK(m.in_scale(note) == is_in_scale(note, c.root, c.type));
            CHECK(m.is_root(note) == is_root(note, c.root));
        }
    }
}

TEST_CASE("scale overlay: off holds nothing, and notes outside MIDI are in no scale") {
    const ScaleOverlayMask off = make_scale_overlay({false, 3, ScaleType::Major, true});
    CHECK_FALSE(off.on);
    CHECK_FALSE(off.in_scale(60));
    CHECK_FALSE(off.is_root(60));
    CHECK_FALSE(off.fingers_shown);
    const ScaleOverlayMask on = make_scale_overlay({true, 0, ScaleType::Major, false});
    CHECK_FALSE(on.in_scale(-1));
    CHECK_FALSE(on.in_scale(128));
}

TEST_CASE("scale overlay: fingers follow the right hand's table from the root near middle C") {
    const ScaleOverlayMask m = make_scale_overlay({true, 0, ScaleType::Major, true});
    CHECK(m.fingers_shown);
    // C major, right hand: 1 2 3 1 2 3 4 1 2 3 1 2 3 4 5 from the C at 60.
    const int expect[] = {1, 2, 3, 1, 2, 3, 4, 1, 2, 3, 1, 2, 3, 4, 5};
    const int steps[] = {0, 2, 4, 5, 7, 9, 11, 12, 14, 16, 17, 19, 21, 23, 24};
    for (int i = 0; i < 15; ++i) {
        CHECK(m.finger[static_cast<std::size_t>(60 + steps[i])] == expect[i]);
    }
    CHECK(m.finger[59] == 0);
    // A key with no table, and fingers off, show none.
    CHECK_FALSE(make_scale_overlay({true, 0, ScaleType::Blues, true}).fingers_shown);
    CHECK_FALSE(scale_has_fingering(ScaleType::Blues));
    CHECK(scale_has_fingering(ScaleType::HarmonicMinor));
    CHECK_FALSE(make_scale_overlay({true, 0, ScaleType::Major, false}).fingers_shown);
}

TEST_CASE("scale overlay: the player's choice is saved per profile and round-trips") {
    AppSettings a;
    CHECK_FALSE(a.scale_overlay);
    a.scale_overlay = true;
    a.scale_root = 3;
    a.scale_type = ScaleType::WholeTone;
    a.scale_fingers = true;
    SettingsDocument doc = SettingsDocument::parse("");
    write_app_settings(doc, a);
    const AppSettings b = read_app_settings(SettingsDocument::parse(doc.serialize()));
    CHECK(b.scale_overlay);
    CHECK(b.scale_root == 3);
    CHECK(b.scale_type == ScaleType::WholeTone);
    CHECK(b.scale_fingers);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscale_type = polka\nscale_root = 40\n"))
              .scale_type == ScaleType::Major);
    CHECK(read_app_settings(SettingsDocument::parse("[app]\nscale_root = 40\n")).scale_root == 11);
}

TEST_CASE("scale overlay: the root is spelled as the key is written, Eb not D#") {
    const struct {
        int root;
        ScaleType type;
        char letter;
        int accidental;
    } cases[] = {{3, ScaleType::Major, 'E', -1},
                 {1, ScaleType::Major, 'D', -1},
                 {1, ScaleType::NaturalMinor, 'C', 1},
                 {3, ScaleType::NaturalMinor, 'E', -1}, // Eb minor, not D# minor
                 {10, ScaleType::Major, 'B', -1},
                 {6, ScaleType::Major, 'F', 1}, // six signs either way: the tie goes to sharps
                 {0, ScaleType::Major, 'C', 0},
                 {9, ScaleType::NaturalMinor, 'A', 0}};
    for (const auto& c : cases) {
        CAPTURE(c.root);
        const ScaleOverlayMask m = make_scale_overlay({true, c.root, c.type, false});
        CHECK(m.root_letter == c.letter);
        CHECK(m.root_accidental == c.accidental);
    }
}

TEST_CASE("scale overlay: Eb major's fingers sit on its notes from the Eb nearest middle C") {
    const ScaleOverlayMask m = make_scale_overlay({true, 3, ScaleType::Major, true});
    REQUIRE(m.fingers_shown);
    const std::vector<int> table = scale_fingers(3, ScaleType::Major, Hand::Right, 2);
    REQUIRE(table.size() == 15);
    const std::span<const int> steps = scale_intervals(ScaleType::Major);
    for (std::size_t i = 0; i < table.size(); ++i) {
        const int note = 63 + 12 * static_cast<int>(i / 7) + steps[i % 7]; // Eb4 is 63
        CAPTURE(note);
        CHECK(m.finger[static_cast<std::size_t>(note)] == table[i]);
        CHECK(m.in_scale(note));
    }
    CHECK(m.finger[63] == 3); // the right hand starts on its third finger in Eb major
    CHECK(m.finger[62] == 0); // nothing below the Eb (D is not in the scale either)
    CHECK(m.finger[51] == 0); // nor an octave lower
}
