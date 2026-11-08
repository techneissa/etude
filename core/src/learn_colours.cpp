// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/learn_colours.hpp"

#include <algorithm>

namespace etude {

namespace {

bool clear_of(RGBA shade, RGBA left_hand, RGBA right_hand) {
    return perceptual_distance(shade, left_hand) >= kLearnMinHandDistance &&
           perceptual_distance(shade, right_hand) >= kLearnMinHandDistance;
}

// The rest of the scale's white keys: the card's colour lifted toward white, as far as it can go
// (up to 0.30) and still be clear of both hands and of the pale unlit white key; less lift keeps
// it a colour. False when no lift does.
bool white_in_shade(RGBA card, RGBA left_hand, RGBA right_hand, RGBA& out) {
    for (float lift = 0.30f; lift >= 0.10f - 1e-4f; lift -= 0.05f) {
        const RGBA shade = lighten(card, lift);
        if (clear_of(shade, left_hand, right_hand) &&
            perceptual_distance(shade, kLearnWhiteOut) >= kLearnMinLitDistance) {
            out = shade;
            return true;
        }
    }
    return false;
}

} // namespace

RGBA learn_section_colour(const LookSettings& look, LearnSection section) {
    return learn_shade_info(look.learn_picks[static_cast<std::size_t>(section)]).colour;
}

LearnKeyColours learn_key_colours(RGBA card, RGBA left_hand, RGBA right_hand) {
    LearnKeyColours c;
    c.white_root = card;
    c.black_root = darken(card, 0.30f);
    c.black_in = darken(card, 0.12f);
    const bool clear = white_in_shade(card, left_hand, right_hand, c.white_in) &&
                       clear_of(c.white_root, left_hand, right_hand) &&
                       clear_of(c.black_root, left_hand, right_hand) &&
                       clear_of(c.black_in, left_hand, right_hand);
    if (clear) {
        return c;
    }
    // A shade of the card's colour could be taken for a hand: a slate highlight, dark on the pale
    // white keys, light on the dark black keys.
    c.neutral = true;
    c.white_root = {58, 68, 102, 255};
    c.white_in = kLearnSlate;
    c.black_root = {205, 210, 225, 255};
    c.black_in = {130, 138, 160, 255};
    return c;
}

RGBA learn_lit_colour(const LookSettings& look, RGBA card, int note, bool root, bool black) {
    if (look.color_scheme == ColorScheme::ByPitchClass) {
        return note_color(look, note, 80, Hand::Right);
    }
    const LearnKeyColours c = learn_key_colours(card, look.left_color, look.right_color);
    if (black) {
        return root ? c.black_root : c.black_in;
    }
    return root ? c.white_root : c.white_in;
}

RGBA learn_sounding_colour(const LookSettings& look, int note, Hand hand) {
    const RGBA base = key_light_color(look, note, 80, hand);
    RGBA lit = lighten(base, kLearnSoundingLift);
    if (look.color_scheme == ColorScheme::ByHand) {
        return lit; // there the hand's own colour is the point: it says which hand plays
    }
    // Under the other schemes a colour that reads as a hand's would say a hand plays: lift it
    // further toward white until it is clear of both, and where no lift clears them (a white hand
    // colour) the neutral slate the card falls back to.
    for (float lift = kLearnSoundingLift; lift < 1.0f + 1e-4f; lift += 0.1f) {
        lit = lighten(base, std::min(lift, 1.0f));
        if (clear_of(lit, look.left_color, look.right_color)) {
            return lit;
        }
    }
    return kLearnSlate;
}

} // namespace etude
