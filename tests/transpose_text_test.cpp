// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/transpose_text.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude::app::ui;

namespace {

std::string shift(int s) {
    char b[text::kShort];
    text::shift(b, sizeof b, s);
    return b;
}
std::string octaves(int s) {
    char b[text::kShort];
    text::octaves(b, sizeof b, s);
    return b;
}
std::string outside(int s) {
    char b[text::kShort];
    text::outside(b, sizeof b, s);
    return b;
}
std::string option(int o, int s) {
    char b[text::kLong];
    text::option(b, sizeof b, o, s);
    return b;
}

} // namespace

TEST_CASE("transpose text: a shift reads with its sign, zero bare") {
    CHECK(shift(0) == "0");
    CHECK(shift(2) == "+2");
    CHECK(shift(12) == "+12");
    CHECK(shift(-5) == "\xE2\x88\x92"
                       "5");
    CHECK(shift(-12) == "\xE2\x88\x92"
                        "12");
}

TEST_CASE("transpose text: an octave move names the octaves") {
    CHECK(octaves(0) == "0");
    CHECK(octaves(1) == "+1 oct");
    CHECK(octaves(-1) == "\xE2\x88\x92"
                         "1 oct");
    CHECK(octaves(-2) == "\xE2\x88\x92"
                         "2 oct");
}

TEST_CASE("transpose text: what is left outside the keyboard") {
    CHECK(outside(0) == "all fit");
    CHECK(outside(1) == "1 outside");
    CHECK(outside(14) == "14 outside");
    CHECK(outside(12345) == "12345 outside");
}

TEST_CASE("transpose text: the keyboard card says how far the keys are shifted") {
    char b[text::kLong];
    text::keyboard_shifted(b, sizeof b, 1);
    CHECK(std::string(b) == "Keyboard shifted +1 oct?");
    text::keyboard_shifted(b, sizeof b, -2);
    CHECK(std::string(b) == "Keyboard shifted \xE2\x88\x92"
                            "2 oct?");
}

TEST_CASE("transpose text: an option is the move and what it leaves, as the card writes them") {
    CHECK(option(-1, 0) == "\xE2\x88\x92"
                           "1 oct \xC2\xB7 all fit");
    CHECK(option(0, 14) == "0 \xC2\xB7 14 outside");
}
