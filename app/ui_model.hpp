// SPDX-License-Identifier: GPL-3.0-or-later
// The state the settings screens edit and the settings file keeps. Plain data, no SDL.
#pragma once

#include "etude/look.hpp"
#include "etude/sections.hpp"
#include "etude/session_state.hpp"

namespace etude::app {

struct UiModel {
    LookSettings look;
    UserPresets user_presets;
    AppSettings app;
    SessionState session; // the keyboard choice is the only part free play edits
    SectionLibrary sections; // the named sections and practice lists of every song marked

    friend bool operator==(const UiModel&, const UiModel&) = default;
};

} // namespace etude::app
