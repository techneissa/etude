// SPDX-License-Identifier: GPL-3.0-or-later
// One open MIDI input as the rest of the app sees it.
#pragma once

#include "etude/keyboard.hpp"

#include <cstddef>
#include <string>

namespace etude::app {

struct MidiInputInfo {
    std::size_t slot = 0; // the index `MidiInput::drain` passes to a callback that takes one
    std::string name;
    const KeyboardProfile* profile = nullptr; // what it matched; nullptr: unknown
};

} // namespace etude::app
