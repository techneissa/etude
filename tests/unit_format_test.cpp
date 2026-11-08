// SPDX-License-Identifier: GPL-3.0-or-later
// One format for every unit the UI shows: the number, then the unit, no space.
#include "etude/unit_format.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <string>

using etude::format_unit;
using etude::Unit;

namespace {
std::string text(double v, Unit u, int decimals = 0, bool plus = false) {
    return std::string(format_unit(v, u, decimals, plus).view());
}
} // namespace

TEST_CASE("units: each unit follows its number with no space") {
    CHECK(text(60, Unit::Percent) == "60%");
    CHECK(text(100, Unit::Percent) == "100%");
    CHECK(text(80, Unit::Milliseconds) == "80ms");
    CHECK(text(5, Unit::Seconds) == "5s");
    CHECK(text(120, Unit::Bpm) == "120BPM");
    CHECK(text(3, Unit::Times) == "3\xC3\x97");
    CHECK(text(20, Unit::Count) == "20");
    CHECK(text(0, Unit::Count) == "0");
}

TEST_CASE("units: decimals are fixed, never trimmed") {
    CHECK(text(4.0, Unit::Seconds, 1) == "4.0s");
    CHECK(text(2.5, Unit::Seconds, 1) == "2.5s");
    CHECK(text(0.126, Unit::Seconds, 2) == "0.13s");
    CHECK(text(60.4, Unit::Percent) == "60%");
    CHECK(text(60.6, Unit::Percent) == "61%");
}

TEST_CASE("units: a negative value uses the minus sign, a positive one a plus when asked") {
    CHECK(text(-250, Unit::Milliseconds) == "\xE2\x88\x92" "250ms");
    CHECK(text(-5, Unit::Seconds, 0, true) == "\xE2\x88\x92" "5s");
    CHECK(text(5, Unit::Seconds, 0, true) == "+5s");
    CHECK(text(5, Unit::Seconds, 0, false) == "5s");
}

TEST_CASE("units: zero never carries a sign, even from a value that rounds to it") {
    CHECK(text(0, Unit::Milliseconds) == "0ms");
    CHECK(text(0, Unit::Milliseconds, 0, true) == "0ms");
    CHECK(text(-0.0, Unit::Milliseconds) == "0ms");
    CHECK(text(-0.4, Unit::Milliseconds) == "0ms");
    CHECK(text(0.4, Unit::Seconds, 0, true) == "0s");
    CHECK(text(-0.04, Unit::Seconds, 1) == "0.0s");
    // Just over the half: it keeps its sign.
    CHECK(text(-0.06, Unit::Seconds, 1) == "\xE2\x88\x92" "0.1s");
}

TEST_CASE("units: a value that is not finite, or absurdly large, reads as a dash") {
    CHECK(text(std::numeric_limits<double>::quiet_NaN(), Unit::Percent) == "-");
    CHECK(text(std::numeric_limits<double>::infinity(), Unit::Seconds) == "-");
    CHECK(text(-std::numeric_limits<double>::infinity(), Unit::Seconds) == "-");
    CHECK(text(1e9, Unit::Percent) == "-");
    CHECK(text(999999999.0, Unit::Milliseconds) == "999999999ms");
}

TEST_CASE("units: decimals are clamped and the text always fits and ends in a terminator") {
    CHECK(text(1.5, Unit::Seconds, -3) == "2s");
    CHECK(text(1.5, Unit::Seconds, 9) == "1.500s");
    const auto t = format_unit(-999999999.0, Unit::Bpm, 3, true);
    CHECK(t.length == std::string(t.c_str()).size());
    CHECK(t.view().substr(t.view().size() - 3) == "BPM");
}
