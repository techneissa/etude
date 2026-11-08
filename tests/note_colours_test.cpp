// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/note_colours.hpp"

#include <doctest/doctest.h>

#include "etude/note_style.hpp"
#include "etude/palette.hpp"

using namespace etude;

TEST_CASE("the note colour table gives what the per-note functions give") {
    for (const Palette palette : {Palette::Classic, Palette::Vivid}) {
        for (const ColorScheme scheme : kAllColorSchemes) {
            for (const bool high : {false, true}) {
                LookSettings look;
                look.color_scheme = scheme;
                apply_palette(look, palette);
                look.high_contrast = high;
                const NoteColourTable table(look);
                for (int note = 0; note < 128; ++note) {
                    for (const int velocity : {0, 1, 40, 63, 64, 100, 126, 127}) {
                        for (const Hand hand : {Hand::Unknown, Hand::Left, Hand::Right}) {
                            const auto v = static_cast<std::uint8_t>(velocity);
                            CAPTURE(static_cast<int>(palette));
                            CAPTURE(static_cast<int>(scheme));
                            CAPTURE(high);
                            CAPTURE(note);
                            CAPTURE(velocity);
                            CAPTURE(static_cast<int>(hand));
                            REQUIRE(table.colour(note, v, hand) == note_color(look, note, v, hand));
                            REQUIRE(table.label(note, v, hand) ==
                                    style_label_color(look, note, v, hand));
                            REQUIRE(table.key_label(note, v, hand) ==
                                    style_key_label_color(look, note, v, hand));
                        }
                    }
                }
            }
        }
    }
}
