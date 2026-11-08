// SPDX-License-Identifier: GPL-3.0-or-later
// A pretend keyboard's doing, for the device check: the developer's --fake-keyboard options make
// them, the window hands them to the check.
#pragma once

#include <cstdint>
#include <string>

namespace etude::app {

struct DevKeyboardEvent {
    enum class Kind : std::uint8_t { OpenCheck, Plug, Unplug, NoteOn, NoteOff } kind = Kind::Plug;
    std::string name;
    int pitch = 0;
};

} // namespace etude::app
