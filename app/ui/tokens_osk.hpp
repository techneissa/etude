// SPDX-License-Identifier: GPL-3.0-or-later
// The on-screen keyboard's sizes (`osk_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The on-screen keyboard and the name card above it (osk_layout.hpp places them), in design points.
namespace osk_tok {
inline constexpr float kKeyPitchMin = tok::kCell; // a key's cell: its hit area and the dead gap
inline constexpr float kKeyPitchMax = 72.0f;
inline constexpr float kKeyPitchShare = 0.052f; // of the window's width: wider screens, bigger keys
inline constexpr float kKeyPad = 6.0f;
inline constexpr float kKeyGap = tok::kHitGap; // between two keys: the dead gap
inline constexpr float kKeyLetter = 19.0f; // the letters on the keys, at the least pitch
inline constexpr float kKeyLetterMax = 28.0f; // ... and on the tallest keys
inline constexpr float kKeyLetterShare = 0.38f; // of the key's height
inline constexpr float kKeyWordMax = 20.0f;     // "Clear", "123" on the tallest keys
inline constexpr float kKeyIconMax = 30.0f;     // the backspace and enter glyphs on the tallest
inline constexpr int kKeyRows = 4;         // the letters' and the symbols' layers
inline constexpr float kKeyUnits = 10.0f;  // key widths across a row: every row fills the width
inline constexpr float kTextMax = 64.0f;   // bytes a field takes
inline constexpr float kCardW = 380.0f;    // a name card's width, at most
inline constexpr int kQuickPerRow = 3;     // one-tap names across a name card
} // namespace osk_tok

} // namespace etude::app::ui
