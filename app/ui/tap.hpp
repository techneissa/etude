// SPDX-License-Identifier: GPL-3.0-or-later
// The one tap rule every control reads (core tap_rule.hpp): it acts when the pointer is released,
// the press stayed inside the touch slop and was not taken away, and a hold past the long-press
// time is no tap. The host feeds the current press once a frame; a control asks about it.
#pragma once

#include "etude/tap_rule.hpp"

#include <imgui.h>

namespace etude::app::ui {

// Called by the host at the start of each frame with the unit (ImGui points per design point) and
// ImGui's mouse state of that frame: starts, follows and ends the press.
void tap_frame(float unit);
// The touch was taken away from the press under the pointer: a bar swipe claimed it, or a second
// finger landed. Nothing more comes of it.
void tap_cancel();
// A second finger landed: the press under the first is cancelled, and so is one that starts in
// the same frame (the tracker begins a press after the events of the frame came in).
void tap_extra_finger();
// No finger is down any more: a second finger no longer cancels the next press.
void tap_fingers_clear();
// The press under the pointer was taken away (`tap_cancel`).
bool tap_cancelled();
// The number of the press in force, or the last one.
std::uint64_t tap_serial();

// A control's release: `pressed` is what its ImGui button returned (the release over it), and the
// result is true when the press also counts as a tap. A button pressed by the keyboard or the
// navigation has no pointer release behind it and counts as it is.
bool tapped(bool pressed);

// The pointer held on the control just drawn: how many repeats fall in this frame (see
// TapRuleConfig::repeat_s), and whether it has been held past the long-press time without
// leaving the slop. Both are 0 / false once the press moved off, was cancelled or lifted.
int hold_repeats_now(double repeat_s);
bool held_still();

// What the item just drawn was pressed for: a tap, or a hold (a press held still past the
// long-press time, or a right click).
struct Press {
    bool tap = false;   // released without moving or holding long
    bool hold = false;  // the long-press time passed (once), or a right click
    bool right = false; // the hold was a right click
};

// The last item's tap or long press. A press held still past the long-press time reports `hold`
// once, and its release is then no tap; a press that moved past the slop (a scroll took it) is
// neither (the tap rule, core tap_rule.hpp). `pressed` is what the item's ImGui button returned.
Press press_of(bool pressed);

// A tap that began and was released outside `min`..`max` this frame, without movement: an overlay
// should close. The press's start is `tap_start()`. False while any ImGui popup is open.
bool tapped_away_from(ImVec2 min, ImVec2 max);
ImVec2 tap_start();

// One overlay's close-by-outside-tap: `away` is `tapped_away_from` for a press that began after
// the overlay opened (core OutsideClose), so the release that opened it, on its opener outside
// the card, never closes it. Call it every frame the overlay is drawn, shown or fading out.
class OutsideTap {
  public:
    bool away(bool shown, ImVec2 min, ImVec2 max);

  private:
    OutsideClose close_;
    int seen_ = -2; // the frame of the last call
};

} // namespace etude::app::ui
