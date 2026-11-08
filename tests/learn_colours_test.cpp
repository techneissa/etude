// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/color.hpp"
#include "etude/learn_colours.hpp"
#include "etude/look.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace etude;

namespace {

// The four sections' own colours, the defaults (learn_palette.hpp).
const RGBA kLearnScalesColour = learn_section_colour(LookSettings{}, LearnSection::Scales);
const RGBA kLearnChordsColour = learn_section_colour(LookSettings{}, LearnSection::Chords);
const RGBA kLearnEarColour = learn_section_colour(LookSettings{}, LearnSection::Ear);
const RGBA kLearnMelodiesColour = learn_section_colour(LookSettings{}, LearnSection::Melodies);
const std::array<RGBA, 4> kCards{kLearnScalesColour, kLearnChordsColour, kLearnEarColour,
                                 kLearnMelodiesColour};

} // namespace

TEST_CASE("learn colours: the card colours are clear of the default hand colours and of "
          "each other") {
    const LookSettings look;
    for (std::size_t i = 0; i < kCards.size(); ++i) {
        CAPTURE(i);
        CHECK(perceptual_distance(kCards[i], look.left_color) >= kLearnMinHandDistance);
        CHECK(perceptual_distance(kCards[i], look.right_color) >= kLearnMinHandDistance);
        for (std::size_t j = i + 1; j < kCards.size(); ++j) {
            CHECK(perceptual_distance(kCards[i], kCards[j]) >= kLearnMinHandDistance);
        }
        CHECK_FALSE(learn_key_colours(kCards[i], look.left_color, look.right_color).neutral);
    }
}

TEST_CASE("learn colours: the Ear card's answers are clear of both default hands, of the Ear coral "
          "that marks its starting note, and of each other") {
    const LookSettings look;
    const std::array<RGBA, 3> answers{kEarRightColour, kEarWrongColour, kEarMissedColour};
    for (std::size_t i = 0; i < answers.size(); ++i) {
        CAPTURE(i);
        CHECK(perceptual_distance(answers[i], look.left_color) >= kLearnMinHandDistance);
        CHECK(perceptual_distance(answers[i], look.right_color) >= kLearnMinHandDistance);
        CHECK(perceptual_distance(answers[i], kLearnEarColour) >= kLearnMinHandDistance);
        for (std::size_t j = i + 1; j < answers.size(); ++j) {
            CHECK(perceptual_distance(answers[i], answers[j]) >= kLearnMinHandDistance);
        }
    }
    // The wine red is well apart from the coral the Ear tab and the starting note wear.
    CHECK(perceptual_distance(kEarWrongColour, kLearnEarColour) >= 0.2);
    // And the starting note's coral is clear of the hands too.
    CHECK(perceptual_distance(kLearnEarColour, look.left_color) >= kLearnMinHandDistance);
    CHECK(perceptual_distance(kLearnEarColour, look.right_color) >= kLearnMinHandDistance);
}

TEST_CASE("learn colours: the Ear card's first note is a lavender and its hint an amber, each clear of "
          "the hands, the coral, the answers, the unlit keys and each other") {
    const LookSettings look;
    const std::array<RGBA, 3> answers{kEarRightColour, kEarWrongColour, kEarMissedColour};
    // The lavender: at least 0.15 from every other colour of the card (0.2 is not reachable near
    // #9C8CF2, which is 0.1 from the right hand's blue).
    for (const RGBA other : {look.left_color, look.right_color, kLearnEarColour, kEarRightColour,
                             kEarWrongColour, kEarMissedColour, kEarHintColour, kLearnWhiteOut,
                             kLearnBlackOut}) {
        CAPTURE(other.r);
        CHECK(perceptual_distance(kEarStartColour, other) >= 0.15);
    }
    // The amber is the orange family, where the left hand lives: the least the hands' rule asks of
    // it from the hands, the coral and the grey; the rest well apart.
    for (const RGBA hand : {look.left_color, look.right_color}) {
        CHECK(perceptual_distance(kEarHintColour, hand) >= kLearnMinHandDistance);
    }
    CHECK(perceptual_distance(kEarHintColour, kLearnEarColour) >= kLearnMinHandDistance);
    CHECK(perceptual_distance(kEarHintColour, kEarMissedColour) >= kLearnMinHandDistance);
    CHECK(perceptual_distance(kEarHintColour, kEarRightColour) >= 0.2);
    CHECK(perceptual_distance(kEarHintColour, kEarWrongColour) >= 0.2);
    CHECK(perceptual_distance(kEarHintColour, kLearnWhiteOut) >= 0.2);
    CHECK(perceptual_distance(kEarHintColour, kLearnBlackOut) >= 0.2);
    // The coral is the Ear tab's: the first note is not it, and no answer is it.
    CHECK(kEarStartColour != kLearnEarColour);
    for (const RGBA answer : answers) {
        CHECK(kEarHintColour != answer);
    }
    // Names (and the marks) read on both.
    for (const RGBA fill : {kEarStartColour, kEarHintColour}) {
        CHECK(contrast_ratio(label_color_for(fill), fill) >= kLabelMinContrast);
    }
}

TEST_CASE("learn colours: the Ear card's answers stand clear of the unlit white and black keys, and "
          "the grey missed key most of all") {
    for (const RGBA answer : {kEarRightColour, kEarWrongColour, kEarMissedColour, kLearnEarColour}) {
        CAPTURE(answer.r);
        CHECK(perceptual_distance(answer, kLearnWhiteOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(answer, kLearnBlackOut) >= kLearnMinLitDistance);
    }
    // The grey is a neutral grey, between the two kinds of key and clear of both by a good margin.
    CHECK(perceptual_distance(kEarMissedColour, kLearnWhiteOut) >= 0.2);
    CHECK(perceptual_distance(kEarMissedColour, kLearnBlackOut) >= 0.2);
    // Names read on every answer: the ink is dark on the light fills and light on the dark ones, at
    // the contrast labels are held to.
    for (const RGBA answer : {kEarRightColour, kEarWrongColour, kEarMissedColour, kLearnEarColour}) {
        CAPTURE(answer.r);
        CHECK(contrast_ratio(label_color_for(answer), answer) >= kLabelMinContrast);
    }
}

TEST_CASE("learn colours: every lit shade of every card is clear of both hands at the full "
          "threshold, custom and pale hand colours included") {
    // The defaults, the card colours themselves, a pale custom pair and a near-white one.
    const std::array<std::array<RGBA, 2>, 6> hands{{{{{255, 169, 77, 255}, {90, 170, 255, 255}}},
                                                    {{{86, 210, 152, 255}, {204, 159, 255, 255}}},
                                                    {{{255, 255, 255, 255}, {20, 20, 20, 255}}},
                                                    {{{252, 144, 188, 255}, {90, 170, 255, 255}}},
                                                    {{{250, 248, 225, 255}, {232, 240, 250, 255}}},
                                                    {{kLearnScalesColour, kLearnChordsColour}}}};
    for (const auto& h : hands) {
        for (const RGBA card : kCards) {
            const LearnKeyColours c = learn_key_colours(card, h[0], h[1]);
            for (const RGBA shade : {c.white_root, c.white_in, c.black_root, c.black_in}) {
                for (const RGBA hand : h) {
                    CAPTURE(card.r);
                    CAPTURE(hand.r);
                    CAPTURE(shade.r);
                    CHECK(perceptual_distance(shade, hand) >= kLearnMinHandDistance);
                }
            }
        }
    }
}

TEST_CASE("learn colours: every lit shade stands clear of the unlit key of its kind") {
    const LookSettings look;
    const auto check = [&](const LearnKeyColours& c) {
        CHECK(perceptual_distance(c.white_root, kLearnWhiteOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(c.white_in, kLearnWhiteOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(c.black_root, kLearnBlackOut) >= kLearnMinLitDistance);
        CHECK(perceptual_distance(c.black_in, kLearnBlackOut) >= kLearnMinLitDistance);
    };
    for (const RGBA card : kCards) {
        CAPTURE(card.r);
        check(learn_key_colours(card, look.left_color, look.right_color));
    }
    // The neutral highlight too.
    LookSettings near = look;
    near.right_color = kLearnScalesColour;
    const LearnKeyColours neutral =
        learn_key_colours(kLearnScalesColour, near.left_color, near.right_color);
    REQUIRE(neutral.neutral);
    check(neutral);
}

TEST_CASE("learn colours: the sounding colours under a scheme that is not the hands' stay clear of "
          "the hands' colours") {
    LookSettings look;
    for (const ColorScheme scheme :
         {ColorScheme::ByPitchClass, ColorScheme::ByOctave, ColorScheme::ByVelocity, ColorScheme::Single}) {
        look.color_scheme = scheme;
        for (int note = 48; note <= 84; ++note) {
            for (const Hand hand : {Hand::Left, Hand::Right}) {
                const RGBA s = learn_sounding_colour(look, note, hand);
                CAPTURE(static_cast<int>(scheme));
                CAPTURE(note);
                CHECK(perceptual_distance(s, look.left_color) >= kLearnMinHandDistance);
                CHECK(perceptual_distance(s, look.right_color) >= kLearnMinHandDistance);
            }
        }
    }
}

TEST_CASE("learn colours: a hand colour near the card's turns the lit keys neutral") {
    LookSettings look;
    look.right_color = kLearnScalesColour;
    const LearnKeyColours c = learn_key_colours(kLearnScalesColour, look.left_color, look.right_color);
    CHECK(c.neutral);
    CHECK(c.white_root != kLearnScalesColour);
    // The other cards are not touched by it.
    CHECK_FALSE(learn_key_colours(kLearnChordsColour, look.left_color, look.right_color).neutral);
    // A hand close but not the same colour counts too.
    look.right_color = {90, 205, 150, 255};
    CHECK(learn_key_colours(kLearnScalesColour, look.left_color, look.right_color).neutral);
}

TEST_CASE("learn colours: the root is stronger than the rest of the scale, black keys darker "
          "than white") {
    const LookSettings look;
    const LearnKeyColours c = learn_key_colours(kLearnScalesColour, look.left_color, look.right_color);
    CHECK(c.white_in != c.white_root);
    CHECK(c.black_in != c.black_root);
    CHECK(c.black_in != c.white_in);
}

TEST_CASE("learn colours: the scheme switch, any scheme but Note keeps the card's colours, Note "
          "takes the note's own") {
    LookSettings look;
    for (const ColorScheme scheme : {ColorScheme::ByHand, ColorScheme::Single, ColorScheme::ByVelocity,
                                     ColorScheme::ByOctave}) {
        look.color_scheme = scheme;
        CHECK(learn_lit_colour(look, kLearnScalesColour, 62, true, false) ==
              learn_key_colours(kLearnScalesColour, look.left_color, look.right_color).white_root);
        CHECK(learn_lit_colour(look, kLearnChordsColour, 61, false, true) ==
              learn_key_colours(kLearnChordsColour, look.left_color, look.right_color).black_in);
    }
    look.color_scheme = ColorScheme::ByPitchClass;
    CHECK(learn_lit_colour(look, kLearnScalesColour, 62, false, false) ==
          note_color(look, 62, 80, Hand::Right));
    CHECK(learn_lit_colour(look, kLearnScalesColour, 62, true, false) ==
          learn_lit_colour(look, kLearnChordsColour, 62, false, false));
    // Two different notes differ.
    CHECK(learn_lit_colour(look, kLearnScalesColour, 62, false, false) !=
          learn_lit_colour(look, kLearnScalesColour, 64, false, false));
}

TEST_CASE("learn colours: a sounding key is the pressed key's colour for the scheme, lifted, and "
          "differs from the scale's tint under every scheme") {
    LookSettings look;
    for (const ColorScheme scheme : {ColorScheme::ByHand, ColorScheme::ByPitchClass,
                                     ColorScheme::ByOctave, ColorScheme::ByVelocity, ColorScheme::Single}) {
        look.color_scheme = scheme;
        CAPTURE(static_cast<int>(scheme));
        for (const Hand hand : {Hand::Left, Hand::Right}) {
            const RGBA sounding = learn_sounding_colour(look, 62, hand);
            if (scheme == ColorScheme::ByHand) {
                CHECK(sounding == lighten(key_light_color(look, 62, 80, hand), kLearnSoundingLift));
            } else {
                // At least the lift, and more only to stay clear of a hand's colour: every channel is
                // at least what the floor lift gives (default hands are always cleared by a lift).
                const RGBA floor = lighten(key_light_color(look, 62, 80, hand), kLearnSoundingLift);
                CHECK(sounding.r >= floor.r);
                CHECK(sounding.g >= floor.g);
                CHECK(sounding.b >= floor.b);
            }
            for (const bool root : {true, false}) {
                CHECK(sounding != learn_lit_colour(look, kLearnScalesColour, 62, root, false));
            }
        }
    }
    // With the hands as the scheme the two hands sound in their own colours.
    look.color_scheme = ColorScheme::ByHand;
    CHECK(learn_sounding_colour(look, 62, Hand::Left) != learn_sounding_colour(look, 62, Hand::Right));
}

TEST_CASE("learn colours: where no lift clears a white hand colour the sounding key is the neutral "
          "slate, never a colour that reads as a hand") {
    // A white custom hand, a near-white one, and one of each beside a default hand.
    const std::array<std::array<RGBA, 2>, 3> hands{{{{{255, 255, 255, 255}, {248, 250, 252, 255}}},
                                                    {{{255, 255, 255, 255}, {90, 170, 255, 255}}},
                                                    {{{255, 169, 77, 255}, {250, 250, 250, 255}}}}};
    for (const auto& h : hands) {
        LookSettings look;
        look.left_color = h[0];
        look.right_color = h[1];
        for (const ColorScheme scheme : {ColorScheme::ByPitchClass, ColorScheme::ByOctave,
                                         ColorScheme::ByVelocity, ColorScheme::Single}) {
            look.color_scheme = scheme;
            for (int note = 48; note <= 84; ++note) {
                const RGBA s = learn_sounding_colour(look, note, Hand::Right);
                CAPTURE(static_cast<int>(scheme));
                CAPTURE(note);
                CHECK(perceptual_distance(s, h[0]) >= kLearnMinHandDistance);
                CHECK(perceptual_distance(s, h[1]) >= kLearnMinHandDistance);
            }
        }
    }
    // A white hand and a hand the note's own lift passes through leave no lift clear: the slate.
    LookSettings both;
    both.color_scheme = ColorScheme::ByPitchClass;
    const RGBA base = key_light_color(both, 60, 80, Hand::Right);
    both.left_color = lighten(base, 0.6f);
    both.right_color = {255, 255, 255, 255};
    CHECK(learn_sounding_colour(both, 60, Hand::Right) == kLearnSlate);
    // The slate is itself clear of white hands and of the unlit white key.
    CHECK(perceptual_distance(kLearnSlate, RGBA{255, 255, 255, 255}) >= kLearnMinHandDistance);
    CHECK(perceptual_distance(kLearnSlate, kLearnWhiteOut) >= kLearnMinLitDistance);
}
