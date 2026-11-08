// SPDX-License-Identifier: GPL-3.0-or-later
// The words of the Progress page's numbers.
#include "etude/progress_text.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("progress text: minutes in minutes, then hours and minutes") {
    CHECK(minutes_words(0) == "0 min");
    CHECK(minutes_words(45) == "45 min");
    CHECK(minutes_words(60) == "1 h");
    CHECK(minutes_words(65) == "1 h 05 min");
    CHECK(minutes_words(232) == "3 h 52 min");
    CHECK(minutes_words(-5) == "0 min");
}

TEST_CASE("progress text: ordinals, with the teens ending in th") {
    CHECK(ordinal_words(1) == "1st");
    CHECK(ordinal_words(2) == "2nd");
    CHECK(ordinal_words(3) == "3rd");
    CHECK(ordinal_words(4) == "4th");
    CHECK(ordinal_words(11) == "11th");
    CHECK(ordinal_words(12) == "12th");
    CHECK(ordinal_words(13) == "13th");
    CHECK(ordinal_words(21) == "21st");
    CHECK(ordinal_words(112) == "112th");
}

TEST_CASE("progress text: bars and thousands") {
    CHECK(bars_words(8, 8) == "bar 8");
    CHECK(bars_words(3, 5) == "bars 3 to 5");
    CHECK(bars_words(0, 0) == "the pickup");
    CHECK(bars_words(0, 2) == "the pickup to bar 2");
    CHECK(thousands(0) == "0");
    CHECK(thousands(933) == "933");
    CHECK(thousands(1051) == "1,051");
    CHECK(thousands(1234567) == "1,234,567");
}
