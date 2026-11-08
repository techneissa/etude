// SPDX-License-Identifier: GPL-3.0-or-later
// The words of the device check card for each step of the check.
#pragma once

#include "etude/device_check.hpp"

#include <string>

namespace etude {

// What the card's icon says: the step's mood.
enum class DeviceCheckTone : std::uint8_t {
    Waiting, // no keyboard yet (plug)
    Found,   // a keyboard, no note yet (keyboard)
    Good,    // a note inside the keys (check)
    Warn,    // a note off the keys (alert)
};

struct DeviceCheckWords {
    std::string title;  // the keyboard's name, or what to do
    std::string detail; // one line under it
    DeviceCheckTone tone = DeviceCheckTone::Waiting;
};

// `octave_offset` is the look's note numbering (LookSettings::octave_offset). A finished check
// reads as its last step.
DeviceCheckWords device_check_words(const DeviceCheck& check, int octave_offset = 0);

} // namespace etude
