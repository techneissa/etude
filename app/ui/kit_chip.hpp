// SPDX-License-Identifier: GPL-3.0-or-later
// The text chip.
#pragma once

#include "kit_core.hpp"

#include <imgui.h>

#include <string_view>

namespace etude::app::ui {

// A text chip: a pill with an optional icon, words and an optional trailing icon. The kit sizes it:
// a face `tok::kFace` tall in a `tok::kHit` hit area, at least `tok::kChipMinW` wide with words
// (`chip_width`; an icon alone is round, `tok::kHit` wide), the group centred in the face and every
// letter placed by its ink. The caller passes the position and the words; `ChipOpts` only says
// what is special about this one. Returns true on a tap.
using ChipIconFn = void (*)(const Kit& k, ImDrawList* dl, const char* icon, ImVec2 centre,
                            float size, ImVec4 colour, bool on);
struct ChipOpts {
    const char* icon = nullptr;
    const char* trail = nullptr; // a trailing icon (a sort's arrow)
    bool on = false;             // selected
    bool live = true;            // false: drawn but not pressable (a bar still fading in)
    float width = 0.0f;          // wider than its words want (a cell of a grid)
    float hit_h = 0.0f; // a bar's cell: its hit height and the face it leaves (0: the kit's)
    float face_h = 0.0f;
    ChipIconFn draw_icon = nullptr; // an icon the font does not have (the library's heart)
    // The words in the body size, bold: a panel of choices that are the point of its card. Give it
    // a `width` (`chip_width` measures the label size).
    bool large = false;
    float label_pt = 0.0f; // the words' size in design points (0: the kit's for the chip)
    // A colour of its own for the chip: its icon and words in it, a selected one lit in it (as a
    // hand's toggle is), where it is not the accent. Null: the kit's.
    const ImVec4* tint = nullptr;
    // With `tint`: a selected chip is filled white with dark words, and only its icon keeps the
    // tint (a little darker, to read on the white); unselected, it is as a tinted chip is.
    bool solid = false;
    // Exactly `width` wide, whatever the words: they take the room the cell leaves (a smaller side
    // pad, then an ellipsis) instead of growing the chip into its neighbours. A row of chips in a
    // fixed room.
    bool fixed = false;
    // A second line under the text (a long name wrapped at its space): both lines are centred
    // in the face, one over the other, each in its own half of it. Words only, no icon.
    std::string_view second = {};
};
float chip_width(const Kit& k, const char* icon, std::string_view text,
                 const char* trail = nullptr);
// The same for words at `label_pt` design points (0: the kit's size), so a chip of a larger popup
// holds its words whole.
float chip_width_at(const Kit& k, const char* icon, std::string_view text, float label_pt);
// A popup's button at `wide_step` `step` (tok::popup_type): the words a type step
// up, the face and hit area taller, wide enough for its words; step 0 is the kit's own chip.
ChipOpts popup_button(const Kit& k, int step, const char* icon, std::string_view text);
bool chip(Kit& k, const char* id, std::string_view text, ImVec2 pos, const ChipOpts& o = {});

} // namespace etude::app::ui
