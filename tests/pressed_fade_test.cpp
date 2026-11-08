// SPDX-License-Identifier: GPL-3.0-or-later
// A control's stored pressed fade counts only while the control is drawn every frame: a row that
// is pressed on the frame it opens a page, and not drawn while the page is up, must come back
// not pressed (app/ui/pressed_fade.hpp, used by `item_state` in kit.cpp).
#include "ui/pressed_fade.hpp"

#include <doctest/doctest.h>

using namespace etude::app::ui;

TEST_CASE("pressed fade: a value written on the previous or the same frame carries on") {
    CHECK(live_pressed_fade(0.6f, 10, 10) == 0.6f);
    CHECK(live_pressed_fade(0.6f, 10, 11) == 0.6f);
}

TEST_CASE("pressed fade: a value from a frame the control missed is stale, exactly past one frame") {
    CHECK(live_pressed_fade(0.6f, 10, 12) == 0.0f);
    CHECK(live_pressed_fade(1.0f, 10, 250) == 0.0f);
    // The row pressed on the frame that opens a page: stored as 1 on frame 7, not drawn while the
    // page is up, drawn again 90 frames later.
    CHECK(live_pressed_fade(1.0f, 7, 97) == 0.0f);
}

TEST_CASE("pressed fade: a stamp from the future (the frame counter was reset) is stale") {
    CHECK(live_pressed_fade(0.6f, 500, 3) == 0.0f);
}

TEST_CASE("pressed fade: nothing stored stays nothing") {
    CHECK(live_pressed_fade(0.0f, 10, 11) == 0.0f);
}
