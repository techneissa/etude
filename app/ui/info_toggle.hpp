// SPDX-License-Identifier: GPL-3.0-or-later
// What a press and a tap do to the open (i) bubble, with no ImGui in it. `open` is the id of the (i) whose bubble is open, 0 for none.
//
// Rules:
// - A tap on a closed (i) opens its bubble.
// - A tap on the open (i) closes it.
// - A tap on another (i) opens that one's bubble in the same tap: the open one closes, the new one
//   opens, and there is never a frame with neither.
// - A press anywhere else closes the bubble. A press on an (i) leaves it to the tap that follows
//   (the tap fires on release), so an open bubble does not close on the press and open again on the
//   release.
#pragma once

#include <cstdint>

namespace etude::app::ui {

// The open bubble after a tap on the (i) `tapped`.
constexpr std::uint32_t info_after_tap(std::uint32_t open, std::uint32_t tapped) {
    return open == tapped ? 0u : tapped;
}

// The open bubble after a press: it stays while the press is on an (i), else it closes.
constexpr std::uint32_t info_after_press(std::uint32_t open, bool on_info_button) {
    return on_info_button ? open : 0u;
}

} // namespace etude::app::ui
