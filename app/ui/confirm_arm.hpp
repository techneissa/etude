// SPDX-License-Identifier: GPL-3.0-or-later
// The two-tap rows of Settings (a slot's clear, Reset all), with no ImGui in it. Rules:
// - the first tap only arms the row (and says so); the second, within 3 s, does the work;
// - a press anywhere but on the armed control disarms it, so a tap elsewhere in between makes the
//   next tap on it a first tap again;
// - an arm does not outlive the page it was made on: leaving the page, or closing the sheet and
//   opening it again, puts the rows back to unarmed, so a tap after returning is a first tap.
#pragma once

#include "etude/look.hpp"

#include <cstdint>

namespace etude::app::ui {

enum class TapStep { Arm, Fire };

// What a tap on Reset all does: arm it, or, when it was armed, put the look back to its defaults
// (the choices that are the player's own stay, `reset_look`).
inline TapStep reset_all_tap(LookSettings& look, bool armed) {
    if (!armed) {
        return TapStep::Arm;
    }
    look = reset_look(look);
    return TapStep::Fire;
}

// Whether an arm made while page `armed_page` was drawn on frame `last_frame` is still good on
// `frame` with `page` showing: the same page, drawn on the frame before (the sheet was not closed).
constexpr bool arm_kept(int armed_page, int last_frame, int page, int frame) {
    return page == armed_page && frame - last_frame <= 1;
}

// Whether an arm survives this frame's press: `armed` is the armed control's id (0: none armed),
// `clicked` that a press began this frame and `hovered` the id of the item under it (0: none, the
// empty page). Only a press on the armed control itself keeps it armed; one on a card, another
// button or nothing puts it back. Serves every armed button.
constexpr bool arm_kept_by_press(std::uint32_t armed, bool clicked, std::uint32_t hovered) {
    return armed == 0 || !clicked || hovered == armed;
}

} // namespace etude::app::ui
