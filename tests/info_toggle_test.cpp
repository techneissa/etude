// SPDX-License-Identifier: GPL-3.0-or-later
// A tap on an (i) opens its bubble, a tap on the open one closes it, a tap on another switches in
// one tap, and a press anywhere else closes (app/ui/info_toggle.hpp).
#include "../app/ui/info_toggle.hpp"

#include <doctest/doctest.h>

#include <cstdint>

using namespace etude::app::ui;

namespace {

// What the frame loop does with a finger: the press first (`finish_frame`), the release's tap on an
// (i) (`info_button`) after it. `on` is the (i) the finger is on, 0 for none.
struct Bubble {
    std::uint32_t open = 0;
    void tap(std::uint32_t on) {
        open = info_after_press(open, on != 0);
        if (on != 0) {
            open = info_after_tap(open, on);
        }
    }
};

} // namespace

TEST_CASE("(i): a tap on a closed (i) opens its bubble") {
    Bubble b;
    b.tap(7);
    CHECK(b.open == 7);
}

TEST_CASE("(i): a tap on the open (i) closes it, and another tap opens it again") {
    Bubble b;
    b.tap(7);
    b.tap(7);
    CHECK(b.open == 0);
    b.tap(7);
    CHECK(b.open == 7);
}

TEST_CASE("(i): a tap on another (i) switches to it in the same tap") {
    Bubble b;
    b.tap(7);
    b.tap(9);
    CHECK(b.open == 9);
    b.tap(7);
    CHECK(b.open == 7);
}

TEST_CASE("(i): a press on an (i) leaves the open bubble to the release, a press elsewhere closes it") {
    // On the press the bubble stays, so the tap that follows decides: closing on the press and
    // opening again on the release is the bug this rule prevents.
    CHECK(info_after_press(7, true) == 7);
    CHECK(info_after_press(7, false) == 0);
    CHECK(info_after_press(0, false) == 0);
    Bubble b;
    b.tap(7);
    b.tap(0); // a tap on the page
    CHECK(b.open == 0);
}
