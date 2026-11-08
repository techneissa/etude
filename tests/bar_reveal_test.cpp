// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/bar_reveal.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("after a finger the bar stays longer than after the mouse") {
    CHECK(bar_hold_s(true, false) > bar_hold_s(false, false));
    CHECK(bar_hold_s(true, true) > bar_hold_s(false, true));
    // About five seconds on a touch screen with nobody at the keys.
    CHECK(kBarHoldTouchS == 5.0);
    CHECK(bar_hold_s(true, false) == kBarHoldTouchS);
}

TEST_CASE("at the keys the bar goes sooner, for both kinds of use") {
    CHECK(bar_hold_s(false, true) < bar_hold_s(false, false));
    CHECK(bar_hold_s(true, true) < bar_hold_s(true, false));
    CHECK(bar_hold_s(false, false) == 3.0);
    CHECK(bar_hold_s(false, true) == 1.0);
}

TEST_CASE("a finger at the keys still gets longer than the mouse's playing hold") {
    // A player who pressed Play with a finger needs time to reach the next button.
    CHECK(bar_hold_s(true, true) >= 2.0 * bar_hold_s(false, true));
}
