// SPDX-License-Identifier: GPL-3.0-or-later
// The song library's sizes (`lib_tok`).
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

// The song library's own sizes, in design points (library_layout.hpp places them).
namespace lib_tok {
// Cards.
inline constexpr float kCardMinW = 180.0f; // a card's least width: the grid fits as many as it can
inline constexpr float kCardH = 116.0f;
inline constexpr float kCardGap = tok::kS3;
inline constexpr float kSidePad = tok::kS4;
inline constexpr float kBand = 4.0f;       // the colour band along a card's top
inline constexpr float kHeadH = 36.0f;     // a group's header
inline constexpr float kLevelStar = 5.5f;     // a level star's outer radius
inline constexpr float kLevelStarGap = 1.5f;  // between two level stars
inline constexpr float kLevelStarLine = 1.0f; // an empty star's outline
inline constexpr float kHand = 13.0f;      // a hand glyph on a card
// The two hands side by side: their glyphs reach a little past their size, so this share of it
// is left between them, and the pair is kHandPairShare sizes wide.
inline constexpr float kHandGapShare = 0.3f;
inline constexpr float kHandPairShare = 2.0f + kHandGapShare;
inline constexpr float kRing = 2.5f;       // the best-score ring's width
inline constexpr float kHeart = 9.0f;      // the favourite heart's half width
inline constexpr float kHeartLine = 1.6f;  // its outline
inline constexpr float kTitleLine = 20.0f; // a card title's line pitch (tok::kBody bold)
// Popovers and the details card.
inline constexpr float kPopoverW = 272.0f;
inline constexpr float kDetailsW = 560.0f;
inline constexpr float kDetailsH = 540.0f;
inline constexpr float kDetailsStackW = 156.0f;   // the details' actions, stacked on the right
inline constexpr float kDetailsLeastLeftW = 240.0f; // what the stack leaves the left, at least
inline constexpr float kShortScreen = 560.0f;     // a screen under this many points tall is short
inline constexpr float kLeastResultsShare = 0.6f; // of a card: what the keyboard leaves the results
// The List view's rows: the title over the composer, the length over the stars. Two rows sit side
// by side where they fit (`kListRowMinW` each), one where they do not.
inline constexpr float kRowH = 64.0f;
inline constexpr float kListGap = tok::kS2;   // between rows, across and down
inline constexpr float kListRowMinW = 340.0f; // a row's least width: two columns need two of these
inline constexpr int kListColumnsMax = 2;
inline constexpr float kListFactsW = 64.0f;      // the length over the level's stars
inline constexpr float kListLeastTextW =
    140.0f; // the title keeps this; the hands, then the facts, go first
// The genre's colour as a line round the whole row, faint so it never competes with the words.
inline constexpr float kRowOutline = 1.5f;
inline constexpr float kRowOutlineAlpha = 0.4f;
// The Shelves view: a shelf's cards are this share of the grid's, so the next one peeks in.
inline constexpr float kShelfCardShare = 0.92f;
// Versions of one piece (#164): a card that has versions grows by a row of chips, under the title
// and over Play, and the grid makes its cards wider while some piece has versions, so three chips
// fit across.
inline constexpr float kVersionsH = tok::kHit + tok::kHitGap; // the row and its gap above Play
inline constexpr float kVersionsCardMinW = 216.0f;
// The A to Z index along the grid's right edge: one drag control a hit wide.
inline constexpr float kAlphaBubble = 56.0f; // the letter shown beside the finger
inline constexpr float kAlphaPitch = 1.15f;  // a letter's least pitch, in its size
// A sideways row's scroll for one notch of a trackpad's or wheel's sideways scroll.
inline constexpr float kSideWheel = 48.0f;
inline constexpr float kPick = 10.5f; // the radius of a picked song's check
// Popovers that hold more than a list.
inline constexpr float kFiltersW = 320.0f;
inline constexpr float kLevelChipW = 52.0f;
inline constexpr float kConfirmW = 420.0f;
// The opening bars in the details.
inline constexpr float kPreviewH = 132.0f;
// Sheet music in the preview: staff spaces in its height, and the least space, in points.
inline constexpr float kPreviewSheetSpaces = 24.0f;
inline constexpr float kPreviewSheetLeastSpace = 3.0f;
// Get songs: a catalogue song's row, its catalogue's name and licence beside
// the button where the row is wide enough, and the credits card.
inline constexpr float kGetSourceW = 132.0f;
// A Get songs row (title over composer, the round Download at the end) and the space between two.
inline constexpr float kGetRowH = 56.0f;
inline constexpr float kGetRowGap = tok::kHitGap;
// A result with versions: its row of chips under the text (a chip is tok::kHit tall).
inline constexpr float kGetChipsH = tok::kHit + tok::kHitGap; // the version chips and the dead gap above them
inline constexpr float kGetLeastTextW = 200.0f;
inline constexpr float kGetProgress = 3.0f; // the download's bar along the row's bottom
inline constexpr float kCreditsW = 520.0f;
// A catalogue song's card is its preview: this share of the window across and down, never smaller
// than the details card; the sheet music's staff spaces at most this many points there.
inline constexpr float kGetCardShareW = 0.8f;
inline constexpr float kGetCardShareH = 0.75f;
inline constexpr float kPreviewSheetMostSpace = 8.0f;
inline constexpr float kPreviewLeastH = 72.0f; // shorter than this, it takes the runs' place
} // namespace lib_tok

} // namespace etude::app::ui
