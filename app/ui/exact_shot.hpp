// SPDX-License-Identifier: GPL-3.0-or-later
// A screenshot depends on its arguments only (issue #326): while this is set, nothing the UI draws
// reads the wall clock. The bench sets it for --screenshot and the combo demos; the app never does.
#pragma once

namespace etude::app::ui {

inline bool& exact_shot() {
    static bool exact = false;
    return exact;
}

} // namespace etude::app::ui
