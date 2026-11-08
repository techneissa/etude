// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/update_key.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

using namespace etude;

TEST_CASE("update key: the placeholder is recognised and is not a usable key") {
    CHECK(update_key_is_placeholder("MINISIGN-PUBLIC-KEY-NOT-SET"));
    CHECK_FALSE(update_key_is_placeholder("RWSbnFPlQv5DPDM0DikGUi39R5PqinL6Qohqyo+bdDZ0dDISvkJ7/PJI"));
    CHECK_FALSE(update_key_is_placeholder(""));
}

TEST_CASE("update key: while the built-in key is the placeholder, nothing verifies") {
    const std::vector<std::uint8_t> data{1, 2, 3};
    const SignatureCheck check = verify_release_signature(data, "any text", "any comment");
    if (update_key_is_placeholder(update_public_key())) {
        CHECK_FALSE(update_key_ready());
        CHECK(check.error == SignatureError::KeyNotSet);
    } else {
        // Once the real key is pasted in, it must be a well-formed minisign public key.
        CHECK(update_key_ready());
        CHECK(check.error != SignatureError::KeyNotSet);
        CHECK(check.error != SignatureError::BadKey);
        CHECK_FALSE(check.ok());
    }
}
