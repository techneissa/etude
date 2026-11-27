// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn screen's sizes (`learn_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The Learn screen's own sizes, in design points (learn_layout.hpp places them).
namespace learn_tok {
inline constexpr float kPad = tok::kS4;           // the screen's side and bottom margin
// A window larger than 1366x768 (its area under the top row 1366 by 711) grows the card's body
// by the smaller of the two ratios, up to kGrowMax.
inline constexpr float kGrowW = 1366.0f;
inline constexpr float kGrowH = 711.0f;
inline constexpr float kGrowMax = 2.5f;
inline constexpr float kSoonAlpha = 0.7f;        // a tab or tile of a card not built yet (it says "Soon"): faint, still readable in its colour
inline constexpr float kTitleRoom = 96.0f;        // the header keeps this much for the page's name beside the tabs
// The gallery: a 2x2 grid of tiles (a picture over a title row)
// and the Continue row under it.
inline constexpr int kGalleryColumns = 2;
inline constexpr float kTilePicGood = 120.0f;     // keys any shorter than this drop the Continue row, to give them the room
inline constexpr float kTilePicMin = 56.0f;       // a picture box any shorter than this over a title row is not drawn there: the tile is one row
inline constexpr float kSidePicShare = 0.45f;     // a one-row tile: its picture is this share of the tile's width at most
inline constexpr float kContinueH = 56.0f;
// The scale card.
inline constexpr float kRingMax = 300.0f;         // the picker's square, at most
inline constexpr float kTypeCellMin = 126.0f;     // a type's cell in the rows, at least: only a default (tests, no font); the app measures the longest name in the font
inline constexpr float kChordCellMin = 96.0f;     // the chord card's default cell, likewise
inline constexpr float kStepperW = 190.0f;        // the chord card's inversion stepper
inline constexpr float kBigScale = 1.3f;          // the types' chips and the foot row, over a hit tall
inline constexpr float kTypeLabelScale = 1.1f;    // the large chips' words, over the body size
inline constexpr float kStripFade = 28.0f;         // the fade at a strip's clipped edge, wide
inline constexpr float kStripWheel = 60.0f;        // a wheel notch moves a strip this far
inline constexpr int kMaxStrip = 24;
inline constexpr float kStripMargin = 20.0f;       // of the next chip kept showing by the selected one
inline constexpr float kKeyboardMinH = 120.0f;
inline constexpr float kKeyAspect = 3.0f;         // a white key's height over its width, at most: the compact card's, so a keyboard looks the same at every size
inline constexpr float kPreviewKeyAspect = kKeyAspect; // and on a gallery tile: the same natural keys, never stretched tall
inline constexpr float kBadge = tok::kChip;       // a finger's number badge, at most
inline constexpr float kBadgePulse = 0.22f;       // a badge swells this much of itself while its note sounds
inline constexpr float kBadgeLabelShare = 0.58f;  // the number in a badge, over the badge's width
inline constexpr float kBadgeShare = 0.46f;       // of a white key's width, the most: neighbours never touch
inline constexpr float kBlackShare = 0.6f;        // a black key's width over a white key's
inline constexpr float kBlackHeightShare = 0.62f; // and its height over a white key's
inline constexpr float kListenW = 140.0f;         // the foot row's Listen with its words,
inline constexpr float kPracticeW = 160.0f;       // and Practice
inline constexpr float kFootWordsMinW = 800.0f;   // a window narrower than this (design points: 800x480 at a scale above 1) has Listen and Practice as icons
inline constexpr float kFootIconShare = 1.5f;     // either icon-only, over the foot row's height wide
inline constexpr float kTempoValueW = 64.0f;      // the tempo stepper's number, between its - and +
inline constexpr float kTitleWordW = 84.0f;      // the page's word ("Scales") in the header, before the scale's name
inline constexpr float kTitleNeed = 190.0f;       // the header's room for the page's name and the scale's or chord's, when the column has no name row
inline constexpr float kOctWordsMin = 92.0f;      // a button of "1 Octave" / "2 Octaves", at least; narrower says "1 oct"
inline constexpr float kKindWordsMin = 108.0f;    // a button of its icon and "Scale" / "Arpeggio", at least; narrower is the icon
inline constexpr float kPopoverW = 252.0f;        // the drill settings' popover, at least
inline constexpr float kHandMark = 28.0f;        // the hand marks of the chord card's foot row
inline constexpr float kKeyNameShare = 0.45f;     // a key's name on a diagram, over a white key's width
// The Ear card (ear_layout.hpp).
inline constexpr float kEarSideMax = 300.0f;      // the left column (streak, disc, settings), at most
inline constexpr float kEarSideShare = 0.36f;     // and at most this much of the body's width
inline constexpr float kEarDiscMin = 120.0f;      // the replay disc's ring, at least, in the full form (else the short one)
inline constexpr float kEarDiscShare = 0.8f;      // the disc itself over the ring round it
inline constexpr float kEarDot = 8.0f;            // a streak dot,
inline constexpr float kEarDotGap = 5.0f;         // and the gap between two
inline constexpr float kEarPillShare = 0.82f;     // the interval name's pill over a hit tall (it is not pressable)
inline constexpr float kEarStreakShare = 1.0f;    // the streak's number row, over a hit
inline constexpr float kEarModeNeed = 96.0f;      // a mode chip's default width (tests, no font); the app measures the words
inline constexpr float kEarHintW = 140.0f;        // the foot row's Hint with its words,
inline constexpr float kEarNextW = 160.0f;        // and Next
inline constexpr float kEarKeyLabelShare = 0.42f; // a named key's letters, over its width
} // namespace learn_tok

} // namespace etude::app::ui
