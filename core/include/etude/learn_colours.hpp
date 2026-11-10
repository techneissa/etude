// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's colours, one meaning each. The hand colours (`LookSettings::left_color` and
// `right_color`, so the player's own apply) are for the hands and their finger badges only. Each
// card has a colour of its own: its tab, and the keys it lights (a strong shade for the root, a
// lighter one for the rest of the scale or chord). A card colour too near a hand colour could be
// taken for that hand, so the keys then use a neutral highlight (`learn_key_colours`). With
// Settings > Colour by set to Note, a lit key takes the note's own colour instead
// (`learn_lit_colour`), as a key the player presses does.
#pragma once

#include "etude/color.hpp"
#include "etude/learn_palette.hpp"
#include "etude/look.hpp"

namespace etude {

// The cards' colours are the player's picks (learn_palette.hpp; teal, pink, coral and violet until
// they pick), the same on every Learn screen: `learn_section_colour` is the one way to ask for one.
// The hands' own colours are for the hands, so no shade is near either default hand.
RGBA learn_section_colour(const LookSettings& look, LearnSection section);

// The Ear card's keys: a right key is a dark green with a check mark in it, a wrong tap a dark wine
// red (apart from the Ear coral) with a cross, the right key a wrong tap missed a grey (clear of the
// unlit white and black keys) with a target mark, and the note the question started from the Ear coral,
// so no hand's colour is used for a key's meaning. Each is clear of both default hands and of the
// others.
inline constexpr RGBA kEarRightColour{26, 128, 72, 255};   // dark green
inline constexpr RGBA kEarWrongColour{124, 24, 56, 255};   // wine red
inline constexpr RGBA kEarMissedColour{130, 134, 142, 255}; // grey
// The note an interval started from ("this is what you heard first") is a lavender with a speaker
// mark, and the key a hint shows an amber with the lightbulb the Hint button has. Neither is a hand's
// colour, the Ear card's colour (the tab's) or any answer's. A lavender near #9C8CF2 comes within 0.1 of the
// right hand's blue, so this one leans toward violet (at least 0.15 from every other colour here); an
// amber is the orange family the left hand lives in, so this one is the deepest gold that stays clear
// of that orange.
inline constexpr RGBA kEarStartColour{184, 120, 232, 255}; // lavender
inline constexpr RGBA kEarHintColour{200, 150, 10, 255};   // amber

// How far, in `perceptual_distance`, a card colour must be from both hand colours to be used.
inline constexpr double kLearnMinHandDistance = 0.10;

// The keys of a diagram that are not lit: a white key and a black key.
inline constexpr RGBA kLearnWhiteOut{209, 214, 224, 255};
inline constexpr RGBA kLearnBlackOut{22, 25, 34, 255};
// How far, in `perceptual_distance`, a lit key must be from the same kind of key unlit, so a lit
// key is seen at a glance.
inline constexpr double kLearnMinLitDistance = 0.10;

// The slate a lit key falls back to when a card colour, or a sounding colour, cannot be told from a
// hand's, and the lit white key of the neutral highlight: mid-dark, so it stands out on a pale white key.
inline constexpr RGBA kLearnSlate{128, 140, 178, 255};

// The colours a card lights its keys in: a white key and a black key, the root and the others.
struct LearnKeyColours {
    RGBA white_root;
    RGBA white_in;
    RGBA black_root;
    RGBA black_in;
    bool neutral = false; // a shade of the card's colour was too near a hand's: a neutral highlight
};
LearnKeyColours learn_key_colours(RGBA card, RGBA left_hand, RGBA right_hand);

// The colour of one lit key of a card whose colour is `card`: its note colour (`note_color`, a black
// key's darkened) when the scheme is Note, else the card's, by `learn_key_colours`
// for these hand colours. `note` is the key's MIDI note.
RGBA learn_lit_colour(const LookSettings& look, RGBA card, int note, bool root, bool black);

// How far a key Listen is sounding blends toward white (a held key on the player keyboard does the
// same, `held_highlight`), so it stands out from the scale's own tint under every scheme.
inline constexpr float kLearnSoundingLift = 0.4f;

// The colour of a key while its note sounds: the colour a pressed key lights in for the scheme
// (`key_light_color`: the hand's, or the note's), lifted by `kLearnSoundingLift` at least. Under a
// scheme that is not the hands' it is lifted further until it is clear of both hand colours, and
// is `kLearnSlate` where no lift clears them.
RGBA learn_sounding_colour(const LookSettings& look, int note, Hand hand);

} // namespace etude
