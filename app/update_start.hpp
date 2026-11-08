// SPDX-License-Identifier: GPL-3.0-or-later
// How the history database stands at a start, for the update helper's ready file (core ReadyGate):
// a freshly installed version says it is ready only after its history has opened and migrated, so a
// migration that goes wrong counts as a failed start and the helper goes back to the old version.
#pragma once

#include "history_store.hpp"

#include "etude/update_handoff.hpp"

namespace etude::app {

inline HistoryOpen history_open_for(HistoryState state) {
    switch (state) {
    case HistoryState::Ready: // opened and migrated: the runs are in
        return HistoryOpen::Opened;
    case HistoryState::Unavailable: // no usable database this session
        return HistoryOpen::Failed;
    case HistoryState::ReadOnly: // another etude has it: this one only read it, and a migration needs
                                 // a write, so it has not run: no ready until the history is writable
    case HistoryState::Loading:  // still opening
    case HistoryState::Waiting:  // busy or not usable yet: retried, and no ready until it opens
        return HistoryOpen::Pending;
    }
    return HistoryOpen::Pending;
}

} // namespace etude::app
