// SPDX-License-Identifier: GPL-3.0-or-later
// What the Learn screen draws that is not a control: the keys of a scale diagram, a finger's badge,
// a scale's name, and the pictures on the gallery's tiles. Defined in learn_keys.cpp.
#pragma once

#include "icons.hpp"
#include "kit.hpp"
#include "learn_layout.hpp"
#include "learn_lit.hpp"

#include "etude/learn_colours.hpp"
#include "etude/look.hpp"
#include "etude/scale_diagram.hpp"

#include <array>

namespace etude::app::ui {

// What the keys of a diagram are painted from: the player's colours (the hands', and the scheme
// Settings > Colour by chooses) and the card's own colour, which the keys of its scale or chord are
// lit in (core learn_colours.hpp: `learn_section_colour` of the card's section). Without `look` the default look.
struct LearnKeyPaint {
    const LookSettings* look = nullptr;
    RGBA card{};
};
namespace learn_col {
// The unlit keys (core learn_colours.hpp: the lit shades are tested against them).
inline constexpr ImVec4 kWhiteOut{kLearnWhiteOut.r / 255.0f, kLearnWhiteOut.g / 255.0f,
                                  kLearnWhiteOut.b / 255.0f, 1.0f};
inline constexpr ImVec4 kBlackOut{kLearnBlackOut.r / 255.0f, kLearnBlackOut.g / 255.0f,
                                  kLearnBlackOut.b / 255.0f, 1.0f};
inline constexpr ImVec4 kBlackLine{1.0f, 1.0f, 1.0f, 0.10f};
} // namespace learn_col

// The keys of `d` in `keyboard`, tinted for the scale. `names`: each note of the scale named inside
// its key, at the foot of a white key and a black key.
// A key Listen is sounding is painted as a key the player presses is (`key_light_color`, in the
// hands' colours or the scheme's), over the scale's tint; both hands on one key share it.
void learn_draw_keys(const Kit& k, ImDrawList* dl, const Box& keyboard, const ScaleDiagram& d,
                     bool names, const LearnKeyPaint& paint, const LearnLit* lit = nullptr);

// A finger's badge in `box` (a circle across it): the number in its hand's colour, `alpha` fading
// it when the hand is not the one playing, and `pulse` (0..1) swelling it while its note sounds.
void learn_draw_badge(const Kit& k, ImDrawList* dl, const Box& box, int finger, ImVec4 colour,
                      float alpha, float pulse = 0.0f);

// "G major" in `slot`, left aligned (or centred across it): the key's name as note_name_at draws it,
// then the type in words, bold, at `size_pt` (a type token).
void learn_draw_scale_name(const Kit& k, ImDrawList* dl, const Box& slot, int root_pc,
                           ScaleType type, float size_pt, bool centre = false);

// What a cell of the two rows of type chips needs to hold the widest of `count` words (`word(i)`) in
// the chips' bold font at the body size, in design points of `unit` (the window's unit grown for
// the card): the words, the chip's side pads and a point to spare. Measured in the font, so the
// rows are used only where no label is cut.
float learn_chip_need_pt(const Kit& k, float unit, int count, const char* (*word)(int));

// The picture on gallery tile `card` (0 Scales, 1 Chords, 2 Ear, 3 Melodies), inside `box`.
void learn_draw_preview(Kit& k, ImDrawList* dl, const Box& box, int card, const LookSettings& look);

// The tabs of the cards: what each is called and drawn with, and its own colour.
struct LearnTabInfo {
    const char* id;
    const char* label;
    const char* icon;
    LearnSection section; // its colour is the player's pick: learn_section_colour(look, section)
};
inline constexpr std::array<LearnTabInfo, kLearnTabs> kLearnTabInfo{{
    {"##learn-tab-scales", "Scales", icon::kScales, LearnSection::Scales},
    {"##learn-tab-chords", "Chords", icon::kLayers, LearnSection::Chords},
    {"##learn-tab-ear", "Ear", icon::kEar, LearnSection::Ear},
    {"##learn-tab-melodies", "Melodies", icon::kMelodies, LearnSection::Melodies},
}};

// The header's tabs with their widths measured in the font (an icon, the name beside it, the pads)
// and `selected` the card shown.
LearnTabs learn_measure_tabs(const Kit& k, int selected);

} // namespace etude::app::ui
