// SPDX-License-Identifier: GPL-3.0-or-later
// The Add songs page's sizes (`phone_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The "Add songs" page's own sizes, in design points (add_songs_layout.hpp places them). Everything
// a finger may touch is at least `tok::kHit`; the code, the address and the PIN take the width the
// card has.
namespace phone_tok {
// The Phone row: the title over its state, and the switch right after them.
inline constexpr float kRowH = tok::kHit + tok::kS2;
inline constexpr float kStatusW = 200.0f;     // the text column, one width for every state
inline constexpr float kSwitchW = 64.0f;      // the switch: a touch target wide and tall
inline constexpr float kSwitchH = tok::kFace; // its drawn track
// The card: the code on the left, this share of the card's width (or what the height leaves), and
// at least `kQrMin`; the quiet zone is `kQuietModules` light modules inside its white tile.
inline constexpr float kQrShare = 0.40f;
inline constexpr float kQrMin = 120.0f;
inline constexpr int kQuietModules = 2;
// The card's right side: least width before the code goes above it, the width from which the
// address is set at display size, and the PIN's boxes.
inline constexpr float kInfoMinW = 232.0f;
// The address is set as large as the card's right side holds, between title size and
// `kAddressSize`: it is `kAddressChars` characters of bold type, each about `kBoldAdvance` of its
// size.
inline constexpr float kAddressSize = 34.0f;
inline constexpr float kAddressChars = 18.0f; // "etude.local:8080"
inline constexpr float kBoldAdvance = 0.6f;
inline constexpr float kLineRatio = 1.25f; // a line's height in its type size
inline constexpr float kPinLabelW = 40.0f;
inline constexpr float kPinMaxW = 72.0f;
inline constexpr float kPinMinH = tok::kHit + tok::kS2;
inline constexpr float kPinMaxH = 96.0f;
inline constexpr float kStatusH = tok::kHit + tok::kS2; // the chips, or the file coming in
inline constexpr float kBarH = 6.0f;                    // the progress bar of the file coming in
inline constexpr float kStackedQrMax = 200.0f;          // the code over the text, in a narrow card
// The tiles under the card: the network folder and the USB stick.
inline constexpr float kTileH = tok::kHit + tok::kS1;
// The views that replace the Phone row and the tiles (the network folder's sheet, the USB list): a
// head with Back, the title and one action, then the body. A row of the USB list holds the song's
// name, its size and the button that imports it, a hit area tall.
inline constexpr float kBackW = 112.0f;
inline constexpr float kActionW = 152.0f;
inline constexpr float kUsbRowH = tok::kHit + tok::kS1;
inline constexpr float kUsbButtonW = 104.0f;
inline constexpr float kUsbSizeW = 72.0f;
} // namespace phone_tok

} // namespace etude::app::ui
