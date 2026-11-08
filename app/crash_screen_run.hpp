// SPDX-License-Identifier: GPL-3.0-or-later
// The crash screen's window run (`etude --crash-screen`): a window, the screen, and nothing else. It
// opens no history, no audio, no MIDI and no song, and reads no settings.
#pragma once

#include "window_options.hpp"

namespace etude::app {

// Exit code 0 when the player chose to restart the app (systemd then starts it), 1 for anything else
// (the window closed, a stop from outside, a window that could not be made): a crash screen that
// cannot draw just stops.
int run_crash_screen(WindowOptions& opt);

} // namespace etude::app
