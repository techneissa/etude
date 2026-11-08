// SPDX-License-Identifier: GPL-3.0-or-later
// What the device check card's buttons, and Settings > Input's row, ask for.
#pragma once

#include <cstdint>

namespace etude::app::ui {

// Whether the card is drawn: the check is open and nothing covers it (a panel, a card, a popover)
// and no song plays. Escape goes to the card only then; otherwise it does what it did without it.
inline constexpr bool device_check_wanted(bool open, bool covered, bool playing) {
    return open && !covered && !playing;
}

enum class DeviceCheckChoice : std::uint8_t {
    None,
    Skip, // leave the check
    Done, // the note looked right
    Run,  // Settings > Input: run it again
};

} // namespace etude::app::ui
