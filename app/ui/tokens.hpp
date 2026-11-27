// SPDX-License-Identifier: GPL-3.0-or-later
// The touch UI's design tokens: sizes in design points (`Kit::u` turns them into ImGui points,
// core `ui_scale` decides `u`), type sizes and motion times. No ImGui here, so the layout tests can
// use the real values. Colours are in kit_colour.hpp (`col::`).
//
// Rules:
// - Every gap and padding is one of the spacing steps; every hit area is at least `kHit` on both
//   axes (12 mm), no two hit areas are closer than `kHitGap` (2 mm) or overlap; every control's
//   drawn face is `kFace` tall (a smaller face only where its card is shorter, face_layout.hpp).
// - One corner radius per role: `kRadiusInner` inside a track, `kRadiusControl` for a face, tile or
//   rail row, `kRadiusCard` for popovers, bar groups and the dock, `kRadiusSheet` for the settings
//   sheet and the editor. Pills (h/2) are for text chips and toasts only; circles for swatches,
//   knobs and badges only.
// - Every state is drawn inside the control's face by one helper (`draw_state` in kit_state.hpp).
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
// Controls.
inline constexpr float kHit = kMinHitPt; // smallest hit area, both axes: 12 mm
inline constexpr float kHitGap = kMinHitGapPt; // least dead gap between hit areas: 2 mm
inline constexpr float kCell = kHit + kHitGap; // the pitch of a row of controls: a hit area, a dead gap
inline constexpr float kFace = 36.0f;   // the drawn face of a button, toggle, segment track, stepper
inline constexpr float kChip = 28.0f;   // a badge inside a face (the speed, a count)
inline constexpr float kChipMinW = 56.0f; // a text chip's least width (a short word like "All")
inline constexpr float kBand = 52.0f;   // the section band under the bar
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

} // namespace tok

} // namespace etude::app::ui
