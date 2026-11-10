// SPDX-License-Identifier: GPL-3.0-or-later
// The pressed fade of a control is kept in ImGui's state storage, written only while it moves
// (`item_state` in kit.cpp). A control that is not drawn for a while (a row on a page that was
// left) keeps what it held, so the stored value carries the frame it was written on and counts for
// nothing once the control has missed a frame. No ImGui here.
#pragma once

namespace etude::app::ui {

// How many frames after its stamp a stored pressed fade still counts: the control is drawn every
// frame it is on screen, so the next frame is the latest it can come back on.
inline constexpr int kPressedFadeLiveFrames = 1;

// The fade to continue from: `stored` when it was written on the previous frame or this one, else 0.
// A frame number from before the stamp (the counter was reset) counts as stale too.
inline float live_pressed_fade(float stored, int stamp, int frame) {
    const int age = frame - stamp;
    return age >= 0 && age <= kPressedFadeLiveFrames ? stored : 0.0f;
}

} // namespace etude::app::ui
