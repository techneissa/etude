// SPDX-License-Identifier: GPL-3.0-or-later
// Which history states let a freshly installed version say it is ready.
#include "../app/update_start.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

TEST_CASE("update start: only a history that opened and migrated is open") {
    CHECK(history_open_for(HistoryState::Ready) == HistoryOpen::Opened);
    // Read only means another etude holds the database: nothing was migrated by this one.
    CHECK(history_open_for(HistoryState::ReadOnly) == HistoryOpen::Pending);
    CHECK(history_open_for(HistoryState::ReadOnly) != HistoryOpen::Opened);
    CHECK(history_open_for(HistoryState::Loading) == HistoryOpen::Pending);
    CHECK(history_open_for(HistoryState::Waiting) == HistoryOpen::Pending);
    CHECK(history_open_for(HistoryState::Unavailable) == HistoryOpen::Failed);
}

TEST_CASE("update start: no ready is written while the history is only read") {
    ReadyGate gate;
    CHECK_FALSE(gate.update(true, history_open_for(HistoryState::ReadOnly))); // first frame shown
    CHECK_FALSE(gate.settled());
    CHECK_FALSE(gate.update(true, history_open_for(HistoryState::Waiting)));
    CHECK(gate.update(true, history_open_for(HistoryState::Ready))); // writable and migrated: now
}
