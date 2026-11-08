// SPDX-License-Identifier: GPL-3.0-or-later
// The window mode: opens the window and runs the app in it until it quits.
#pragma once

#include "window_options.hpp"

namespace etude::app {

struct WindowHandles;

// Opens the window and its renderer, paced by the display: false when SDL or the window cannot be
// made. Shared by the window run and the crash screen's.
bool open_window(WindowOptions& opt, WindowHandles& handles);

// Returns the exit code.
int run_window(WindowOptions& opt);

} // namespace etude::app
