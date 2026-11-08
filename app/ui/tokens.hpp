// SPDX-License-Identifier: GPL-3.0-or-later
// The touch UI's design tokens: sizes in design points (`Kit::u` turns them into ImGui points,
// core `ui_scale` decides `u`), type sizes and motion times. No ImGui here, so the layout tests can
// use the real values. Colours are in kit.hpp (`col::`).
//
// Rules:
// - Every gap and padding is one of the spacing steps; every hit area is at least `kHit` on both
//   axes (12 mm), no two hit areas are closer than `kHitGap` (2 mm) or overlap; every control's
//   drawn face is `kFace` tall (a smaller face only where its card is shorter, face_layout.hpp).
// - One corner radius per role: `kRadiusInner` inside a track, `kRadiusControl` for a face, tile or
//   rail row, `kRadiusCard` for popovers, bar groups and the dock, `kRadiusSheet` for the settings
//   sheet and the editor. Pills (h/2) are for text chips and toasts only; circles for swatches,
//   knobs and badges only.
// - Every state is drawn inside the control's face by one helper (`draw_state` in kit.hpp).
#pragma once

#include "etude/panel_size.hpp"
#include "etude/screen_layout.hpp"
#include "etude/touch_gesture.hpp"

namespace etude::app::ui {

namespace tok {
// Spacing scale.
inline constexpr float kS1 = 4.0f;
inline constexpr float kS2 = 8.0f;
inline constexpr float kS3 = 12.0f;
inline constexpr float kS4 = 16.0f;
inline constexpr float kS5 = 24.0f;
inline constexpr float kS6 = 32.0f;
inline constexpr float kInset = 3.0f;    // a thumb or pressed fill inside a track
inline constexpr float kHairline = 1.0f; // a divider, the selected line
// Type.
inline constexpr float kCaption = 13.0f; // captions, section heads, the section band's time
inline constexpr float kLabel = 14.0f;   // values, chips, segment text, toasts
inline constexpr float kBody = kStatusFontPt; // row labels and the status line
inline constexpr float kTitle = 20.0f;   // bold: page titles, the sheet's header
inline constexpr float kDisplay = 28.0f; // bold: the tempo number, results figures
inline constexpr float kMark = 11.5f;    // bold: the L or R in a hand toggle's corner
// Icons.
inline constexpr float kIconSm = 16.0f; // segments, chips, steppers
inline constexpr float kIcon = 20.0f;   // rows, the rail, the bar
inline constexpr float kIconLg = 24.0f; // the gear, the popover's beat note
inline constexpr float kIconXl = 32.0f; // an emblem on a wide window's notice (wide_step 1)
inline constexpr float kIconHero = 40.0f; // and on a 1920-wide window's (wide_step 2)
inline constexpr float kIconHuge = 56.0f; // the crash screen's emblem on a 1920-wide window
// The song-opened toast's icons, in ems of their line's text size.
inline constexpr float kToastClefEm = 1.3f;  // the treble clef's height beside the title
inline constexpr float kToastQuillEm = 0.95f; // the quill's box beside the composer
inline constexpr float kToastLineGap = kS1;   // extra space between the title and composer lines
// Controls.
inline constexpr float kHit = kMinHitPt; // smallest hit area, both axes: 12 mm
inline constexpr float kHitGap = kMinHitGapPt; // least dead gap between hit areas: 2 mm
inline constexpr float kCell = kHit + kHitGap; // the pitch of a row of controls: a hit area, a dead gap
inline constexpr float kFace = 36.0f;   // the drawn face of a button, toggle, segment track, stepper
inline constexpr float kChip = 28.0f;   // a badge inside a face (the speed, a count)
inline constexpr float kChipMinW = 56.0f; // a text chip's least width (a short word like "All")
inline constexpr float kRow = kHit;     // a settings row
inline constexpr float kMenuW = 272.0f; // the accompaniment menu's card
inline constexpr float kBand = 52.0f;   // the section band under the bar
inline constexpr float kTransposeW = 288.0f; // the transposition card
inline constexpr float kOfferW = 344.0f;     // the octave-shift offer's card
inline constexpr float kDeviceCheckW = 480.0f; // the device check's card, centred in the note area
inline constexpr float kStripKeys = 16.0f;   // the fit strip's keys, tall
inline constexpr float kStripRow =
    14.0f; // one option's row under them: its label and bar of the song's range
inline constexpr float kStripBar = 8.0f;    // that bar, thick
inline constexpr float kStripLabel = 26.0f; // the label column at the rows' left
inline constexpr float kSwitchW = 40.0f;
inline constexpr float kSwitchH = 24.0f;
inline constexpr float kKnob = 18.0f;   // a slider's knob, across
inline constexpr float kKnobHeld = 21.0f; // and while a finger drags it
inline constexpr float kTrack = 4.0f;   // a slider's track, thick
inline constexpr float kFocusRing = 2.0f; // the focus ring's width, inset `kHairline` in the face
// Radii.
inline constexpr float kRadiusInner = 7.0f;   // thumbs, a pressed fill inside a track
inline constexpr float kRadiusControl = 10.0f; // every face, tile and rail row
inline constexpr float kRadiusCard = 16.0f;   // popovers, bar groups, the dock, cards
inline constexpr float kRadiusSheet = 20.0f;  // the settings sheet, the editor
// The names the settings pages and the editor still use, for the roles above.
inline constexpr float kRadiusSm = kRadiusInner;
inline constexpr float kRadiusMd = kRadiusControl;
inline constexpr float kRadiusLg = kRadiusCard;
inline constexpr float kControl = kFace;
// State strengths (alpha of white over the face, or of the accent).
inline constexpr float kFillAlpha = 0.055f;    // the idle face of a filled control
inline constexpr float kHoverAlpha = 0.085f;   // a mouse over it, never a finger
inline constexpr float kPressedAlpha = 0.14f;  // a finger or button down, from the first frame
inline constexpr float kPressedInk = 0.3f;     // pressed content mixed this far to white
inline constexpr float kSelectedFill = 0.18f;  // selected: the accent this strong over the face
inline constexpr float kSelectedLine = 0.45f;  // and a hairline of it round the face
inline constexpr float kHandFill = 0.26f;      // a lit hand: its colour, no line
inline constexpr float kDisabledAlpha = 0.4f;  // a disabled control's content
// Motion, seconds. Ease-out, nothing loops at idle; with reduced effects every move is instant.
inline constexpr float kInstant = 0.0f; // pressed in
inline constexpr float kFast = 0.12f;   // hover, toggles, thumbs, pressed out
inline constexpr float kMove = 0.16f;   // page push and pop, popovers, the chevron
inline constexpr float kSheet = 0.20f;  // the sheet, the band, the bar reveal
inline constexpr float kPillSlide = 0.20f; // the chosen pill sliding between cells, and the words' fade
inline constexpr float kLongPress = static_cast<float>(kLongPressS);
// The count band's pulse: a new beat or second comes in this much larger and settles over kSheet.
inline constexpr float kCountPulseGrow = 0.15f;

// One rule for a screen that grows its type, icon and buttons with the window's width: the step is
// 0 under `kWideStep1W` points wide, 1 from there and 2 from `kWideStep2W`. The window's own width,
// never the density-scaled unit: the unit already grows the design points, the step picks the larger
// type steps on top of it. The crash notice and the crash screen use it (crash_layout.hpp,
// crash_screen_layout.hpp); a screen picks its sizes from the ladder above by the step.
inline constexpr float kWideStep1W = 1366.0f;
inline constexpr float kWideStep2W = 1920.0f;
inline constexpr float kWideGrow1 = 1.3f; // a button's face and hit height at step 1, over its own
inline constexpr float kWideGrow2 = 1.6f; // and at step 2
inline constexpr int wide_step(float window_w) {
    return window_w >= kWideStep2W ? 2 : window_w >= kWideStep1W ? 1 : 0;
}
inline constexpr float wide_grow(int step) {
    return step >= 2 ? kWideGrow2 : step == 1 ? kWideGrow1 : 1.0f;
}

// One rule for a popup or message drawn over the app (a toast, the info bubble, a confirm, a card).
// From `wide_step` 1 (1366 points) its words take the sizes of the crash notice at that step, and
// step 2 (1920) keeps the same design steps, the UI scale making them larger on screen: a title is
// kDisplay, a sentence kTitle, a secondary line and a button's words kBody, a button is
// `kWideGrow1` taller, and a card is wider (`popup_width`). Step 0 (under 1366 points: the 7 inch
// panel's 800x480) is never changed. A popup takes its sizes from these helpers, never from
// numbers of its own.
enum class PopupText : unsigned char {
    Title,  // a card's heading: kDisplay
    Text,   // a sentence, the words of a toast or the info bubble: kTitle
    Detail, // a secondary line under one of those: kBody
    Button, // the words of a button: kBody
};
inline constexpr float kPopupWidth1 = 1.5f; // a card's width, over its own, at step 1
inline constexpr float kPopupWidth2 = 1.7f; // and at step 2, where the card would look lost
inline constexpr float kPopupShare = 0.6f;  // a card is at most this share of the window's width
inline constexpr float kPopupMaxW = 760.0f; // and at most this wide, in design points
inline constexpr float kPopupLineChars = 60.0f; // a line of reading text, in characters
inline constexpr float kInfoBubbleW = 260.0f;   // the info bubble's wrap width at step 0, design points
// A size for `role`: `pt` itself at step 0, else the role's step of the ladder (never smaller).
inline constexpr float popup_type(float pt, int step, PopupText role) {
    if (step <= 0) {
        return pt;
    }
    const float to = role == PopupText::Title ? kDisplay : role == PopupText::Text ? kTitle : kBody;
    return pt > to ? pt : to;
}
// A button's face and hit height over its own: `wide_grow(1)` at both wide steps.
inline constexpr float popup_grow(int step) { return step <= 0 ? 1.0f : kWideGrow1; }
// A card's width in design points: its own `base` at step 0, else `base` grown for the step, kept
// under `kPopupMaxW` and `kPopupShare` of the window (`window_w`, in design points), and never
// under `base`.
inline constexpr float popup_width(float base, int step, float window_w) {
    if (step <= 0) {
        return base;
    }
    const float w = base * (step >= 2 ? kPopupWidth2 : kPopupWidth1);
    const float cap = kPopupShare * window_w < kPopupMaxW ? kPopupShare * window_w : kPopupMaxW;
    const float capped = w < cap ? w : cap;
    return capped > base ? capped : base;
}
// The info bubble's step: the Settings sheet's bubbles keep step 0 whatever the window, every other
// one takes the window's `step`.
inline constexpr int info_bubble_step(int step, bool in_settings) { return in_settings ? 0 : step; }
// Its words' size and the width they wrap to, in design points, at that step.
inline constexpr float info_bubble_text(int bubble_step) {
    return popup_type(kLabel, bubble_step, PopupText::Text);
}
// The width a line of `text_pt` words wraps to, for about `kPopupLineChars` characters (an average
// character is half an em), under the same share of the window.
inline constexpr float popup_line_w(float text_pt, float window_w) {
    const float w = kPopupLineChars * text_pt * 0.5f;
    const float cap = kPopupShare * window_w < kPopupMaxW ? kPopupShare * window_w : kPopupMaxW;
    return w < cap ? w : cap;
}
inline constexpr float info_bubble_wrap(int bubble_step, float window_w) {
    return bubble_step > 0 ? popup_line_w(info_bubble_text(bubble_step), window_w) : kInfoBubbleW;
}
} // namespace tok

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
inline constexpr int kMenuRowsMax = 7; // a list popover scrolls past this many rows
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
inline constexpr float kFootIcon = tok::kHit;     // the footer's icon chip
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

// The on-screen keyboard and the name card above it (osk_layout.hpp places them), in design points.
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
inline constexpr float kDeleteGap = 48.0f;  // the least between Delete and Cancel (a few fingers' widths)
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
inline constexpr int kConfirmLinesTight = 2; // ... and without it, over a keyboard: one of explanation, then the warning
inline constexpr float kConfirmFieldH = 48.0f; // the typed-name field, as the sheet's name field
inline constexpr float kConfirmLineRatio = 1.2f; // a line's height over its size: close, so two lines and the field fit over a keyboard
inline constexpr float kConfirmLineH = kConfirmText * kConfirmLineRatio;
inline constexpr int kGoalStepMin = 5;      // the goal's stepper moves by this
inline constexpr int kGoalMin = 0;          // 0 turns the goal off
inline constexpr int kGoalMax = 720;
} // namespace profile_tok

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
