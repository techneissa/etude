// SPDX-License-Identifier: GPL-3.0-or-later
// What the settings check's sweeps drive (settings_check.hpp): the UI, its model, a way to draw a
// frame and a way to send the mouse. `check_taps` (settings_check_taps.cpp) taps the real buttons.
#pragma once

#include "ui.hpp"
#include "ui_model.hpp"

#include <SDL3/SDL.h>

#include <functional>

namespace etude::app {

struct CheckRig {
    Ui& ui;
    UiModel& model;
    std::function<void()> draw;
    std::function<void(SDL_EventType, float, float)> mouse;
};

// Taps the reset buttons and the (i) buttons the pages really draw and prints what is wrong, one
// line each; the number of problems.
int check_taps(const CheckRig& rig);

} // namespace etude::app
