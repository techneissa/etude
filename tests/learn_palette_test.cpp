// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/color.hpp"
#include "etude/learn_colours.hpp"
#include "etude/look.hpp"

#include <doctest/doctest.h>

#include <array>
#include <set>
#include <string>

using namespace etude;

namespace {

// What the Learn colours are drawn on: the screen (col::kScreenBg), the cards and the bar
// (col::kSurface) and the tiles and popover rows that sit on them (col::kRaised).
constexpr std::array<RGBA, 3> kBackdrops{{{9, 11, 17, 255}, {22, 25, 34, 255}, {32, 36, 48, 255}}};
constexpr RGBA kWhite{255, 255, 255, 255};
// The selected tab is white and its icon the shade darkened by a quarter (kit_controls.cpp).
constexpr float kTabIconDarken = 0.25f;
// A drawn shape (an icon, a ring) must reach the contrast WCAG asks of graphics.
constexpr double kGraphicMinContrast = 3.0;
// Two shades of the palette must look like two choices.
constexpr double kMinShadeDistance = 0.07;

} // namespace

TEST_CASE("learn palette: the defaults are today's section colours, the Ear card's lavender and amber "
          "among the colours that stay") {
    const LookSettings look;
    CHECK(learn_section_colour(look, LearnSection::Scales) == RGBA{46, 196, 182, 255});   // teal
    CHECK(learn_section_colour(look, LearnSection::Chords) == RGBA{242, 122, 181, 255});  // pink
    CHECK(learn_section_colour(look, LearnSection::Ear) == RGBA{242, 119, 106, 255});     // coral
    CHECK(learn_section_colour(look, LearnSection::Melodies) == RGBA{167, 139, 250, 255}); // violet
    CHECK(kEarStartColour == RGBA{184, 120, 232, 255}); // lavender
    CHECK(kEarHintColour == RGBA{200, 150, 10, 255});   // amber
}

TEST_CASE("learn palette: every pick is found by its stored key, which no other shade has") {
    std::set<std::string> keys;
    std::set<std::string> names;
    for (std::size_t i = 0; i < kLearnShadeCount; ++i) {
        CHECK_FALSE(kLearnShades[i].key.empty());
        CHECK_FALSE(kLearnShades[i].name.empty());
        CHECK(keys.insert(std::string(kLearnShades[i].key)).second);
        CHECK(names.insert(std::string(kLearnShades[i].name)).second);
        CHECK(&learn_shade_info(static_cast<LearnShade>(i)) == &kLearnShades[i]);
    }
}

TEST_CASE("learn palette: a section's colour follows its own pick, and two sections may share one") {
    for (std::size_t section = 0; section < kLearnSectionCount; ++section) {
        for (std::size_t shade = 0; shade < kLearnShadeCount; ++shade) {
            LookSettings look;
            look.learn_picks[section] = static_cast<LearnShade>(shade);
            CHECK(learn_section_colour(look, static_cast<LearnSection>(section)) == kLearnShades[shade].colour);
            for (std::size_t other = 0; other < kLearnSectionCount; ++other) {
                if (other != section) {
                    CHECK(learn_section_colour(look, static_cast<LearnSection>(other)) ==
                          learn_shade_info(kLearnDefaultPicks[other]).colour);
                }
            }
        }
    }
    LookSettings shared;
    shared.learn_picks = {LearnShade::Lime, LearnShade::Lime, LearnShade::Lime, LearnShade::Lime};
    CHECK(learn_section_colour(shared, LearnSection::Scales) == learn_section_colour(shared, LearnSection::Ear));
}

TEST_CASE("learn palette: every shade is clear of both default hands and of the other shades") {
    const LookSettings look;
    for (std::size_t i = 0; i < kLearnShadeCount; ++i) {
        const RGBA c = kLearnShades[i].colour;
        CAPTURE(kLearnShades[i].key);
        CHECK(perceptual_distance(c, look.left_color) >= kLearnMinHandDistance);
        CHECK(perceptual_distance(c, look.right_color) >= kLearnMinHandDistance);
        for (std::size_t j = i + 1; j < kLearnShadeCount; ++j) {
            CHECK(perceptual_distance(c, kLearnShades[j].colour) >= kMinShadeDistance);
        }
    }
}

TEST_CASE("learn palette: every shade reads on the card and on the keyboard it lights") {
    const LookSettings look;
    for (const LearnShadeInfo& shade : kLearnShades) {
        const RGBA c = shade.colour;
        CAPTURE(shade.key);
        // On the card: as words and icons on the dark screen, card and tile.
        for (const RGBA backdrop : kBackdrops) {
            CHECK(contrast_ratio(c, backdrop) >= kLabelMinContrast);
        }
        // As a filled tab or chip: its words, and as the selected (white) tab's darkened icon.
        CHECK(contrast_ratio(label_color_for(c), c) >= kLabelMinContrast);
        CHECK(contrast_ratio(darken(c, kTabIconDarken), kWhite) >= kGraphicMinContrast);
        // On the keys: the shades it lights are its own (no neutral fall-back for the default
        // hands), each clear of the hands and of the unlit key of its kind.
        const LearnKeyColours keys = learn_key_colours(c, look.left_color, look.right_color);
        CHECK_FALSE(keys.neutral);
        for (const RGBA lit : {keys.white_root, keys.white_in, keys.black_root, keys.black_in}) {
            CHECK(perceptual_distance(lit, look.left_color) >= kLearnMinHandDistance);
            CHECK(perceptual_distance(lit, look.right_color) >= kLearnMinHandDistance);
        }
        CHECK(perceptual_distance(keys.white_root, kLearnWhiteOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(keys.white_in, kLearnWhiteOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(keys.black_root, kLearnBlackOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(keys.black_in, kLearnBlackOut) >= kLearnMinLitDistance);
        // The root is told from the rest of the scale.
        CHECK(perceptual_distance(keys.white_root, keys.white_in) > 0.0);
        CHECK(perceptual_distance(keys.black_root, keys.black_in) > 0.0);
    }
}

TEST_CASE("learn palette: no shade can be taken for an answer on the Ear card") {
    // Right, wrong, missed and the hint mean something on the Ear card's keys. (The first note's
    // lavender is a key with a speaker mark; a violet pick is near it by design and the Ear card
    // draws its colour only in the tab and the streak, never on a key.)
    for (const LearnShadeInfo& shade : kLearnShades) {
        CAPTURE(shade.key);
        for (const RGBA answer : {kEarRightColour, kEarWrongColour, kEarMissedColour, kEarHintColour}) {
            CHECK(perceptual_distance(shade.colour, answer) >= kLearnMinHandDistance);
        }
    }
}

TEST_CASE("learn palette: a pick lights the keys in its own shades, and Colour by Note still wins") {
    LookSettings look;
    look.learn_picks[static_cast<std::size_t>(LearnSection::Chords)] = LearnShade::Lime;
    const RGBA lime = learn_shade_info(LearnShade::Lime).colour;
    const RGBA card = learn_section_colour(look, LearnSection::Chords);
    CHECK(card == lime);
    const LearnKeyColours keys = learn_key_colours(lime, look.left_color, look.right_color);
    CHECK(learn_lit_colour(look, card, 62, true, false) == keys.white_root);
    CHECK(learn_lit_colour(look, card, 62, false, false) == keys.white_in);
    CHECK(learn_lit_colour(look, card, 61, true, true) == keys.black_root);
    CHECK(learn_lit_colour(look, card, 61, false, true) == keys.black_in);
    look.color_scheme = ColorScheme::ByPitchClass;
    CHECK(learn_lit_colour(look, card, 62, true, false) == note_color(look, 62, 80, Hand::Right));
}

TEST_CASE("learn palette: a shade value outside the enum reads the first shade, never past the list") {
    CHECK(&learn_shade_info(static_cast<LearnShade>(kLearnShades.size())) == &kLearnShades[0]);
    CHECK(&learn_shade_info(static_cast<LearnShade>(255)) == &kLearnShades[0]);
}

TEST_CASE("learn colours tabs: each tab (Scales, Chords, Ear, Melodies) shows its own section's pick") {
    LearnPicks picks = kLearnDefaultPicks;
    picks[0] = LearnShade::Lime;
    picks[1] = LearnShade::Jade;
    picks[2] = LearnShade::Magenta;
    picks[3] = LearnShade::Coral;
    CHECK(learn_tab_pick(picks, 0) == LearnShade::Lime);
    CHECK(learn_tab_pick(picks, 1) == LearnShade::Jade);
    CHECK(learn_tab_pick(picks, 2) == LearnShade::Magenta);
    CHECK(learn_tab_pick(picks, 3) == LearnShade::Coral);
    CHECK(learn_tab_section(static_cast<int>(LearnSection::Ear)) == static_cast<std::size_t>(LearnSection::Ear));
    // A tab outside the four reads the first, never past the array.
    CHECK(learn_tab_section(-1) == 0);
    CHECK(learn_tab_section(4) == 0);
}

TEST_CASE("learn colours tabs: a tap on a shade changes only the picked tab's section") {
    for (int tab = 0; tab < static_cast<int>(kLearnSectionCount); ++tab) {
        CAPTURE(tab);
        const LearnPicks before = kLearnDefaultPicks;
        const LearnPicks after = learn_pick_on_tab(before, tab, LearnShade::Lime);
        for (int s = 0; s < static_cast<int>(kLearnSectionCount); ++s) {
            CAPTURE(s);
            CHECK(after[static_cast<std::size_t>(s)] ==
                  (s == tab ? LearnShade::Lime : before[static_cast<std::size_t>(s)]));
        }
    }
}

TEST_CASE("learn colours tabs: Reset to default puts only the shown tab's section back, the other three "
          "keep their picks") {
    for (int tab = 0; tab < static_cast<int>(kLearnSectionCount); ++tab) {
        CAPTURE(tab);
        LookSettings look;
        for (int t = 0; t < static_cast<int>(kLearnSectionCount); ++t) {
            look.learn_picks = learn_pick_on_tab(look.learn_picks, t, LearnShade::Magenta);
        }
        const LearnPicks before = look.learn_picks;
        const LearnShade prior = reset_learn_tab(look.learn_picks, tab);
        CHECK(prior == LearnShade::Magenta); // what the toast's Undo puts back
        for (int s = 0; s < static_cast<int>(kLearnSectionCount); ++s) {
            CAPTURE(s);
            const auto i = static_cast<std::size_t>(s);
            CHECK(look.learn_picks[i] == (s == tab ? kLearnDefaultPicks[i] : before[i]));
        }
        // Undo: the exact earlier shade goes back.
        look.learn_picks[learn_tab_section(tab)] = prior;
        CHECK(look.learn_picks == before);
    }
}
