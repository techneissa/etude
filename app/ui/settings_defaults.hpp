// SPDX-License-Identifier: GPL-3.0-or-later
// Where a settings row gets the default its reset button goes back to (reset_to.hpp): the setting's
// own, the member initialiser of `LookSettings` or `AppSettings`, so no row keeps a second copy of
// the number.
#pragma once

#include "etude/look.hpp"
#include "etude/session_state.hpp"

namespace etude::app::ui {

inline const LookSettings& look_defaults() {
    static const LookSettings defaults{};
    return defaults;
}

inline const AppSettings& app_defaults() {
    static const AppSettings defaults{};
    return defaults;
}

} // namespace etude::app::ui
