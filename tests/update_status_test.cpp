// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/update_status.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;
using ui::UpdatePhase;
using ui::UpdateView;

namespace {

UpdateView in(UpdatePhase phase) {
    UpdateView v;
    v.phase = phase;
    v.version = "1.2.3";
    return v;
}

} // namespace

TEST_CASE("update status: Check now is offered unless something is running or nothing is set up") {
    CHECK(can_check_now(in(UpdatePhase::NotChecked)));
    CHECK(can_check_now(in(UpdatePhase::UpToDate)));
    CHECK(can_check_now(in(UpdatePhase::Available)));
    CHECK(can_check_now(in(UpdatePhase::Failed)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Checking)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Downloading)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::Installing)));
    CHECK_FALSE(can_check_now(in(UpdatePhase::NotSetUp)));
}

TEST_CASE("update status: the daily check is due a day after a good check, never when off") {
    constexpr std::int64_t now = 1'800'000'000;
    CHECK_FALSE(auto_check_due(false, now, 0, 0));
    CHECK(auto_check_due(true, now, 0, 0));                           // never checked
    CHECK_FALSE(auto_check_due(true, now, now - kAutoCheckEveryS + 1, 0));
    CHECK(auto_check_due(true, now, now - kAutoCheckEveryS, 0));
    // A failed attempt counts: one attempt a day, however old the last good check is.
    CHECK_FALSE(auto_check_due(true, now, 0, now - 60));
    CHECK_FALSE(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - 3600));
    CHECK_FALSE(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - kAutoCheckEveryS + 1));
    CHECK(auto_check_due(true, now, now - 3 * kAutoCheckEveryS, now - kAutoCheckEveryS));
    // A time from the future is not trusted: the check is due.
    CHECK(auto_check_due(true, now, now + 10 * kAutoCheckEveryS, 0));
}
