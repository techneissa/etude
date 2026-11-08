// SPDX-License-Identifier: GPL-3.0-or-later
// When the Progress page asks again, and what it shows while it has no numbers.
#include "ui/progress_ask.hpp"

#include <doctest/doctest.h>

using namespace etude::app::ui;

TEST_CASE("progress ask: due when the page opens, and asked again for a new profile, goal or day") {
    ProgressAsked asked;
    CHECK(asked.due(1, 15, 100, 2)); // just opened
    asked.note(1, 15, 100, 2);
    CHECK_FALSE(asked.due(1, 15, 100, 2));
    CHECK(asked.due(2, 15, 100, 2)); // another profile in use
    CHECK(asked.due(1, 20, 100, 2)); // the goal changed
    CHECK(asked.due(1, 15, 101, 2)); // past midnight
    asked.note(1, 15, 101, 2);
    CHECK_FALSE(asked.due(1, 15, 101, 2));
    CHECK(asked.due(1, 15, 101, 1)); // a profile was deleted
    asked.note(1, 15, 101, 1);
    CHECK(asked.due(1, 15, 101, 2)); // or added
    asked.note(1, 15, 101, 2);
    asked.reopen(); // the page was closed and opened again
    CHECK(asked.due(1, 15, 101, 2));
}

TEST_CASE("progress ask: with no numbers the page says it is working, or that it could not read them") {
    CHECK(progress_status(true, false) == ProgressStatus::Ready);
    CHECK(progress_status(true, true) == ProgressStatus::Ready); // numbers in hand are shown
    CHECK(progress_status(false, false) == ProgressStatus::Loading);
    CHECK(progress_status(false, true) == ProgressStatus::Failed);
}
