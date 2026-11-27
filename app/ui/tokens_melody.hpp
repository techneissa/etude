// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's Melodies page sizes (`melody_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The Melodies page of the Learn screen (learn_melodies_layout.hpp places it), in design points: a row
// of tiles for each tutorial path, each tile a piece with its progress as a border and a ring round
// Play. The tile is one pressable (it opens the piece), so every size here is what is drawn.
namespace melody_tok {
inline constexpr float kTileMinW = 184.0f;  // a tile's least width: a path whose tiles do not fit across scrolls sideways
inline constexpr float kTileMaxW = 240.0f;  // and the widest one grows, so a path of two or three is not stretched
inline constexpr float kTileMinH = 90.0f;   // the compact tile: its title (two lines), then the hands, the stars and Play
inline constexpr float kTileGoalH = 126.0f; // from this tall a tile also has its goal line
inline constexpr float kTileMaxH = 164.0f;
inline constexpr float kTilePad = 10.0f;    // inside the tile's border
inline constexpr float kTileGap = tok::kS2; // between tiles, and between a row and the next path's header: over the dead gap
inline constexpr float kBorder = 2.0f;      // the tile's border; the progress fills along it
inline constexpr float kBadge = tok::kChip; // the number (or the check) at a tile's top left
inline constexpr float kPlay = 32.0f;       // the Play disc
inline constexpr float kPlayGap = 3.0f;     // between the disc and the ring round it
inline constexpr float kRing = 2.5f;        // the ring's line
inline constexpr float kHandCell = 24.0f;   // a hand's glyph with its key count under it
inline constexpr float kHandIcon = 18.0f;   // the hand glyph
inline constexpr float kCountPt = tok::kCaption - 1.0f; // the key count under a hand
inline constexpr float kHandMark = 10.0f;   // the L or R beside a hand: the hand is not told by its colour alone
inline constexpr float kStar = 4.8f;        // a difficulty star's outer radius
inline constexpr float kStarGap = 1.5f;     // between two stars
inline constexpr float kStarLine = 1.0f;    // an empty star's outline
inline constexpr float kGoalLine = 16.0f;   // the goal's line pitch (tok::kCaption)
inline constexpr float kHeadIcon = tok::kFace; // a path's icon chip
inline constexpr float kHeadNameShare = 0.76f;  // the path's name is centred in this share of the header's height from the top,
inline constexpr float kHeadCountShare = 0.6f;  // and its count in this share from the bottom (the two lines are a little over a body apart)
inline constexpr float kHeadTitleW = 150.0f;   // the path's title and its count, at most (the words are cut to it)
inline constexpr float kNextMinW = 90.0f;      // under this the "Next" words are left out
inline constexpr float kButtonW = 132.0f;      // Continue, Start
inline constexpr float kButtonMinW = tok::kHit; // the button as its icon alone, where the row is too narrow
inline constexpr float kTagPt = tok::kCaption - 2.0f; // the version tag under a title: a muted pill, not a control
inline constexpr float kTagH = 16.0f;                  // its height, at most the title's second line
inline constexpr float kNextWords = tok::kLabel; // "Next · Oh! Susanna: small leaps"
} // namespace melody_tok

} // namespace etude::app::ui
