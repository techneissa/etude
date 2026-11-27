// SPDX-License-Identifier: GPL-3.0-or-later
// The kit's colours and the colour helpers: `col::`, `with_alpha`, `mix`, `u32`.
#pragma once

#include "tokens.hpp"

#include <imgui.h>

namespace etude::app::ui {

// Tokens (sizes, type, motion) are in tokens.hpp.

// Colours: two surface tones, the state fills, one accent, three text tones, one warning. The
// selected state is the accent with a hairline round it, so it never reads as a lit right hand (whose
// default colour is the same blue): a lit hand is its own colour with no line and its L or R mark.
namespace col {
inline constexpr ImVec4 kScrim{0.016f, 0.024f, 0.04f, 0.46f};
// A screen of its own (the library, Learn): the scene's darkness, opaque, so nothing of the notes
// shows through.
inline constexpr ImVec4 kScreenBg{0.035f, 0.043f, 0.065f, 1.0f};
inline constexpr ImVec4 kSurface{0.086f, 0.098f, 0.133f, 0.975f}; // the sheet, the bar, cards
inline constexpr ImVec4 kRaised{0.125f, 0.141f, 0.188f, 1.0f};    // toasts, tiles, popover rows
inline constexpr ImVec4 kFill{1.0f, 1.0f, 1.0f, tok::kFillAlpha}; // the idle face, tracks
inline constexpr ImVec4 kHover{1.0f, 1.0f, 1.0f, tok::kHoverAlpha};     // a mouse over it
inline constexpr ImVec4 kPressed{1.0f, 1.0f, 1.0f, tok::kPressedAlpha}; // a finger on it
inline constexpr ImVec4 kHairline{1.0f, 1.0f, 1.0f, 0.08f};
inline constexpr ImVec4 kAccent{0.353f, 0.667f, 1.0f, 1.0f}; // #5AAAFF: selection, primary, focus
inline constexpr ImVec4 kSelected{0.353f, 0.667f, 1.0f, tok::kSelectedFill};
inline constexpr ImVec4 kSelectedLine{0.353f, 0.667f, 1.0f, tok::kSelectedLine};
inline constexpr ImVec4 kAccentInk{0.031f, 0.055f, 0.094f, 1.0f}; // content on the accent
inline constexpr ImVec4 kText{0.933f, 0.949f, 0.98f, 1.0f};
inline constexpr ImVec4 kText2{0.604f, 0.643f, 0.722f, 1.0f};
inline constexpr ImVec4 kText3{0.42f, 0.455f, 0.533f, 1.0f};
inline constexpr ImVec4 kWarning{1.0f, 0.45f, 0.45f, 1.0f};
// The names the settings pages and the editor still use, for the roles above.
inline constexpr ImVec4 kSurfaceHi = kRaised;
inline constexpr ImVec4 kDanger = kWarning;
} // namespace col

ImVec4 with_alpha(ImVec4 c, float a);
ImVec4 mix(ImVec4 a, ImVec4 b, float t);
ImU32 u32(ImVec4 c); // multiplied by the current style alpha

} // namespace etude::app::ui
