// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/base64.hpp"

#include <doctest/doctest.h>

#include <string>

using namespace etude;

namespace {

std::string decoded(std::string_view text) {
    const auto bytes = base64_decode(text);
    REQUIRE(bytes.has_value());
    return std::string(bytes->begin(), bytes->end());
}

} // namespace

TEST_CASE("base64: the RFC 4648 test vectors") {
    CHECK(decoded("") == "");
    CHECK(decoded("Zg==") == "f");
    CHECK(decoded("Zm8=") == "fo");
    CHECK(decoded("Zm9v") == "foo");
    CHECK(decoded("Zm9vYg==") == "foob");
    CHECK(decoded("Zm9vYmE=") == "fooba");
    CHECK(decoded("Zm9vYmFy") == "foobar");
}

TEST_CASE("base64: the alphabet's last two characters are + and /") {
    const auto bytes = base64_decode("+/+/");
    REQUIRE(bytes.has_value());
    CHECK(bytes->size() == 3);
    CHECK((*bytes)[0] == 0xFB);
    CHECK((*bytes)[1] == 0xFF);
    CHECK((*bytes)[2] == 0xBF);
}

TEST_CASE("base64: anything that is not canonical is refused") {
    CHECK_FALSE(base64_decode("Zm9").has_value());      // length not a multiple of 4
    CHECK_FALSE(base64_decode("Zm9v\n").has_value());   // whitespace
    CHECK_FALSE(base64_decode("Zm 9v").has_value());
    CHECK_FALSE(base64_decode("Zm9-").has_value());     // URL alphabet
    CHECK_FALSE(base64_decode("Zg=A").has_value());     // padding in the middle
    CHECK_FALSE(base64_decode("=Zg=").has_value());
    CHECK_FALSE(base64_decode("Zh==").has_value());     // unused bits of the last character set
    CHECK_FALSE(base64_decode("Zm9=").has_value());
    CHECK_FALSE(base64_decode("====").has_value());
    CHECK_FALSE(base64_decode("Zg===").has_value());
}
