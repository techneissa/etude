// SPDX-License-Identifier: GPL-3.0-or-later
// The profile switcher's sizes (`profile_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The profile switcher's own sizes (profile_layout.hpp places them), in design points. Everything a
// finger may touch is at least `tok::kHit`.
namespace profile_tok {
inline constexpr float kChipAvatar = 24.0f; // the avatar disc inside the top row's chip
inline constexpr float kChipMinW = 104.0f;  // the chip's least width (a short name)
inline constexpr float kChipTextMax = 112.0f; // the name's room in the chip; a longer one is cut
inline constexpr float kPanelW = 580.0f;    // "Who's playing", at most
inline constexpr float kPanelPad = tok::kS3;
inline constexpr float kHeadH = tok::kHit;  // the panel's title row, with the Edit button
inline constexpr float kEditW = 96.0f;
inline constexpr float kTileMinW = 88.0f;   // a tile of the grid
inline constexpr float kAvatarBig = 56.0f;
inline constexpr float kAvatarShort = 44.0f;
inline constexpr float kAvatarFlat = 36.0f;
inline constexpr float kTileNameH = tok::kBody * 1.3f;     // the name's line in a big tile
inline constexpr float kTileStreakH = tok::kCaption * 1.3f; // the flame and the days under it
inline constexpr float kTileTitleH = tok::kCaption * 1.3f;  // the player's title (Soloist) under the name
inline constexpr float kTileMedal = 28.0f;                  // its small medal, at the avatar's lower right
// A big tile: even padding, the avatar, the name, the title, the streak, even padding; so the streak
// row ends a padding above the tile's bottom edge whatever the avatar's size.
constexpr float tile_big_h(float avatar) {
    return 2.0f * tok::kS3 + avatar + tok::kS2 + kTileNameH + kTileTitleH + tok::kS1 + kTileStreakH;
}
inline constexpr float kTileH = tile_big_h(kAvatarBig);        // big: the avatar over the name over the streak
inline constexpr float kTileShortH = tile_big_h(kAvatarShort); // big, on a short window
inline constexpr float kTileFlatH = 52.0f;  // flat: the avatar beside the name, for a big household
inline constexpr float kTileFlatMinW = 150.0f;
inline constexpr float kRowH = 48.0f;       // the "Progress and household" row
inline constexpr float kSheetW = 780.0f;    // the profile sheet, at most: fourteen colours, a hit and a gap each, fit its row
inline constexpr float kSheetNeedW = 520.0f; // the room the goal stepper and both buttons take in a row
inline constexpr float kGoalLabelW = 84.0f; // "Daily goal"
inline constexpr float kAvatarSheet = 64.0f;
inline constexpr float kSwatch = 36.0f;     // the drawn colour disc, in a `tok::kHit` cell
inline constexpr float kGoalValueW = 72.0f; // "15 min" between the stepper's buttons
inline constexpr float kNameText = 24.0f;   // the name typed in the sheet's field (48 tall)
inline constexpr float kButtonW = 108.0f;   // Cancel
inline constexpr float kSaveW = 124.0f;
inline constexpr float kDeleteW = 108.0f;   // Delete, at the left end of the colours' row
inline constexpr float kRemoveW = 124.0f;   // the confirm's Delete, at the far end from Cancel
inline constexpr float kConfirmText = 18.0f; // what the confirm says is lost: over body size, across the card
inline constexpr int kConfirmLines = 3;      // its lines at most with the title row: two of explanation, then the warning
inline constexpr float kConfirmFieldH = 48.0f; // the typed-name field, as the sheet's name field
inline constexpr float kConfirmLineRatio = 1.2f; // a line's height over its size: close, so two lines and the field fit over a keyboard
inline constexpr int kGoalStepMin = 5;      // the goal's stepper moves by this
inline constexpr int kGoalMin = 0;          // 0 turns the goal off
inline constexpr int kGoalMax = 720;
} // namespace profile_tok

} // namespace etude::app::ui
