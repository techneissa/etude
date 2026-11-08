// SPDX-License-Identifier: GPL-3.0-or-later
// The device check card: a small card centred in the note area that shows the keyboard plugged in
// and waits for a note. Its steps and words are core's (device_check.hpp, device_check_text.hpp);
// this draws one step and says what was tapped.
//
//   waiting   a plug, "Plug in your keyboard", and a keyboard icon that leaves with the on-screen
//             keys
//   found     the keyboard's name, "Recognised", its range, "play a key"; a cross leaves
//   note      the note's name and "looks right" with Done, or, off the keys, the octave the
//             keyboard may be shifted by
// It sits in the note area only, never over the keys it tests.
#pragma once

#include "device_check_choice.hpp"
#include "kit.hpp"

#include "etude/device_check.hpp"


namespace etude::app::ui {

class DeviceCheckCard {
  public:
    // Draws `check` (null: none) centred in `area`, the note area in ImGui points, and returns what
    // was tapped this frame. `allowed` false (a panel, a popover or a card of the run is up) hides
    // it without ending the check. `octave_offset` is the look's note numbering.
    DeviceCheckChoice draw(Kit& k, const etude::DeviceCheck* check, const RectF& area,
                           int octave_offset, bool allowed);
    bool shown() const { return shown_p_ > 0.0f; }
    // Drawn and wanted: a card fading out, or hidden by play or a panel, is not up.
    bool up() const { return wanted_ && shown_p_ > 0.0f; }
    // Where it was drawn in the last frame (points), empty when it is not shown.
    const RectF& rect() const { return rect_; }

  private:
    RectF rect_;
    float shown_p_ = 0.0f;
    bool wanted_ = false;
};

} // namespace etude::app::ui
