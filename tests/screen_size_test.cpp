// SPDX-License-Identifier: GPL-3.0-or-later
// The Settings > View "Screen size" choices: their text and the order a tap goes in.
#include "ui/screen_size.hpp"

#include <doctest/doctest.h>

using namespace etude::app::ui;

TEST_CASE("screen size: the row shows Auto, a diagonal or the width") {
    CHECK(screen_size_text(0) == "Auto");
    CHECK(screen_size_text(344) == "15.6\"");
    CHECK(screen_size_text(154) == "7\"");
    CHECK(screen_size_text(321) == "321 mm"); // a hand-edited width
}

TEST_CASE("screen size: a tap goes to the next larger choice and round to Auto") {
    CHECK(next_screen_size(0) == 154);
    CHECK(next_screen_size(154) == 222);
    CHECK(next_screen_size(344) == 476);
    CHECK(next_screen_size(1000) == 0);
    CHECK(next_screen_size(321) == 344); // between two: the next larger
    CHECK(next_screen_size(5000) == 0);
}

TEST_CASE("screen size: every choice is a width the setting accepts") {
    for (const ScreenSizeChoice& c : kScreenSizes) {
        CHECK((c.width_mm == 0 || (c.width_mm >= etude::kMinScreenWidthMm &&
                                    c.width_mm <= etude::kMaxScreenWidthMm)));
    }
}
