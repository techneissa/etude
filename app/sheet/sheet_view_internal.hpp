// SPDX-License-Identifier: GPL-3.0-or-later
// What the files of sheet_view.cpp share. Not part of the app's interface.
#pragma once

#include "sheet_view.hpp"

#include "../gfx.hpp"
#include "../touch_device.hpp"
#include "sheet_name_fit.hpp"

#include "etude/hands_present.hpp"
#include "etude/note_colours.hpp"
#include "etude/screen_layout.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>

namespace etude::app {

// The paper and what is drawn on it.
inline constexpr SDL_FColor kPaper{1.0f, 0.996f, 0.973f, 1.0f};

// Note mark colours, by index: still to play (the score's ink), still to play by the hand not
// practised (faint, as on the falling notes), played, missed (a soft amber, never red: a miss is
// not a wrong key).
enum Colour : std::uint8_t { kInk, kOtherHand, kPlayed, kMissed };

inline constexpr std::array<SDL_FColor, 4> kMarkColours{{
    {0.0f, 0.0f, 0.0f, 1.0f},
    {0.0f, 0.0f, 0.0f, 0.38f},
    {0.098f, 0.588f, 0.243f, 1.0f},
    {0.851f, 0.549f, 0.200f, 0.9f},
}};

} // namespace etude::app
