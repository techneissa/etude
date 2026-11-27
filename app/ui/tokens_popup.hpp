// SPDX-License-Identifier: GPL-3.0-or-later
// Tokens of the popups, toasts, cards and crash screens: the wide-step ladder and the popup
// rules, and the sizes of the cards that have one of their own.
#pragma once

#include "tokens.hpp"

namespace etude::app::ui {

namespace tok {
inline constexpr float kIconXl = 32.0f; // an emblem on a wide window's notice (wide_step 1)
inline constexpr float kIconHero = 40.0f; // and on a 1920-wide window's (wide_step 2)
inline constexpr float kIconHuge = 56.0f; // the crash screen's emblem on a 1920-wide window
// The song-opened toast's icons, in ems of their line's text size.
inline constexpr float kToastClefEm = 1.3f;  // the treble clef's height beside the title
inline constexpr float kToastQuillEm = 0.95f; // the quill's box beside the composer
inline constexpr float kToastLineGap = kS1;   // extra space between the title and composer lines
inline constexpr float kMenuW = 272.0f; // the accompaniment menu's card
inline constexpr float kTransposeW = 288.0f; // the transposition card
inline constexpr float kOfferW = 344.0f;     // the octave-shift offer's card
inline constexpr float kDeviceCheckW = 480.0f; // the device check's card, centred in the note area
inline constexpr float kStripKeys = 16.0f;   // the fit strip's keys, tall
inline constexpr float kStripRow =
    14.0f; // one option's row under them: its label and bar of the song's range
inline constexpr float kStripBar = 8.0f;    // that bar, thick
inline constexpr float kStripLabel = 26.0f; // the label column at the rows' left
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
// A button's face and hit height over its own: `kWideGrow1` at both wide steps.
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

} // namespace etude::app::ui
