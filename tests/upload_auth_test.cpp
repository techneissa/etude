// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/upload_auth.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

const std::string kTokenA(kTokenChars, 'a');
const std::string kTokenB(kTokenChars, 'b');

} // namespace

TEST_CASE("upload auth: PIN and token text") {
    CHECK(pin_from_random(0) == "0000");
    CHECK(pin_from_random(42) == "0042");
    CHECK(pin_from_random(9999) == "9999");
    CHECK(pin_from_random(10000) == "0000");
    CHECK(pin_from_random(0xFFFFFFFFu).size() == kPinDigits);
    CHECK(is_pin_text("0042"));
    CHECK_FALSE(is_pin_text("042"));
    CHECK_FALSE(is_pin_text("00420"));
    CHECK_FALSE(is_pin_text("12a4"));
    CHECK_FALSE(is_pin_text(" 123"));
    CHECK_FALSE(is_pin_text(""));
    std::array<std::uint8_t, 16> b{};
    b[0] = 0xAB;
    b[15] = 0x07;
    CHECK(token_from_bytes(b) == "ab000000000000000000000000000007");
    CHECK(is_token_text(token_from_bytes(b)));
    CHECK_FALSE(is_token_text("AB000000000000000000000000000007")); // upper case is not ours
    CHECK_FALSE(is_token_text("ab00"));
}

TEST_CASE("upload auth: the right PIN earns a session, a wrong one does not") {
    UploadAuth auth("4821");
    CHECK(auth.login("0000", kTokenA, 1000).result == LoginResult::WrongPin);
    CHECK(auth.login("482", kTokenA, 1000).result == LoginResult::BadFormat);
    CHECK(auth.login("48211", kTokenA, 1000).result == LoginResult::BadFormat);
    CHECK(auth.login("", kTokenA, 1000).result == LoginResult::BadFormat);
    CHECK(auth.sessions() == 0);
    const LoginAnswer ok = auth.login("4821", kTokenA, 1000);
    CHECK(ok.result == LoginResult::Ok);
    CHECK(ok.token == kTokenA);
    CHECK(auth.check(kTokenA, 2000));
    CHECK_FALSE(auth.check(kTokenB, 2000));
    CHECK_FALSE(auth.check("", 2000));
    CHECK_FALSE(auth.check("4821", 2000)); // the PIN is not a cookie
    auth.logout(kTokenA);
    CHECK_FALSE(auth.check(kTokenA, 2000));
}

TEST_CASE("upload auth: five wrong PINs lock the PIN for a minute, exactly") {
    UploadAuth auth("4821");
    for (int i = 0; i < kMaxPinFailures - 1; ++i) {
        CHECK(auth.login("1111", kTokenA, 0).result == LoginResult::WrongPin);
    }
    // The fifth wrong try starts the lock; even the right PIN waits.
    CHECK(auth.login("1111", kTokenA, 100).result == LoginResult::WrongPin);
    const LoginAnswer locked = auth.login("4821", kTokenA, 100 + 1);
    CHECK(locked.result == LoginResult::Locked);
    CHECK(locked.retry_ms == kPinLockMs - 1);
    CHECK(auth.login("4821", kTokenA, 100 + kPinLockMs - 1).result == LoginResult::Locked);
    // Just at the end of the lock the right PIN works.
    CHECK(auth.login("4821", kTokenA, 100 + kPinLockMs).result == LoginResult::Ok);
}

TEST_CASE("upload auth: a right PIN resets the count, and the lock gives five more tries") {
    UploadAuth auth("4821");
    for (int i = 0; i < kMaxPinFailures - 1; ++i) {
        auth.login("1111", kTokenA, 0);
    }
    CHECK(auth.login("4821", kTokenA, 10).result == LoginResult::Ok);
    for (int i = 0; i < kMaxPinFailures - 1; ++i) {
        CHECK(auth.login("1111", kTokenB, 20).result == LoginResult::WrongPin);
    }
    CHECK(auth.login("4821", kTokenB, 30).result == LoginResult::Ok);
    // After a lock ends, five wrong tries lock again.
    UploadAuth other("4821");
    for (int i = 0; i < kMaxPinFailures; ++i) {
        other.login("1111", kTokenA, 0);
    }
    CHECK(other.login("1111", kTokenA, kPinLockMs).result == LoginResult::WrongPin);
    CHECK(other.login("1111", kTokenA, kPinLockMs).result == LoginResult::WrongPin);
}

TEST_CASE("upload auth: a session ends after ten idle minutes and use keeps it") {
    UploadAuth auth("4821");
    auth.login("4821", kTokenA, 0);
    CHECK(auth.check(kTokenA, kSessionIdleMs - 1)); // refreshed at that moment
    CHECK(auth.check(kTokenA, 2 * kSessionIdleMs - 2));
    CHECK_FALSE(auth.check(kTokenA, 3 * kSessionIdleMs - 2));
}

TEST_CASE("upload auth: the oldest session goes when there are too many") {
    UploadAuth auth("4821");
    std::vector<std::string> tokens;
    for (std::size_t i = 0; i <= kMaxSessions; ++i) {
        std::string t(kTokenChars, 'a');
        t[0] = static_cast<char>('0' + i);
        tokens.push_back(t);
        CHECK(auth.login("4821", t, static_cast<UploadMs>(i)).result == LoginResult::Ok);
    }
    CHECK(auth.sessions() == kMaxSessions);
    CHECK_FALSE(auth.check(tokens.front(), 100));
    CHECK(auth.check(tokens.back(), 100));
}

TEST_CASE("upload life: off by default, ready, receiving, and off after ten idle minutes") {
    UploadLife life;
    CHECK(life.status() == UploadStatus::Off);
    CHECK_FALSE(life.tick(10 * kServerIdleMs));
    life.start(1000);
    CHECK(life.status() == UploadStatus::Ready);
    CHECK(life.remaining_ms(1000) == kServerIdleMs);
    CHECK_FALSE(life.tick(1000 + kServerIdleMs - 1));
    CHECK(life.remaining_ms(1000 + kServerIdleMs - 1) == 1);
    // Use restarts the ten minutes.
    life.touch(5000);
    CHECK_FALSE(life.tick(1000 + kServerIdleMs));
    CHECK(life.tick(5000 + kServerIdleMs));
    CHECK(life.status() == UploadStatus::Off);
    CHECK(life.reason() == "Turned off after 10 minutes unused");
    CHECK_FALSE(life.tick(5000 + 2 * kServerIdleMs)); // once
}

TEST_CASE("upload life: a transfer in progress is use, however long") {
    UploadLife life;
    life.start(0);
    life.transfer_begin(100);
    CHECK(life.status() == UploadStatus::Receiving);
    CHECK_FALSE(life.tick(5 * kServerIdleMs));
    life.transfer_end(5 * kServerIdleMs);
    CHECK(life.status() == UploadStatus::Ready);
    CHECK_FALSE(life.tick(5 * kServerIdleMs + kServerIdleMs - 1));
    CHECK(life.tick(6 * kServerIdleMs));
}

TEST_CASE("upload life: two transfers and a failure") {
    UploadLife life;
    life.start(0);
    life.transfer_begin(1);
    life.transfer_begin(2);
    life.transfer_end(3);
    CHECK(life.status() == UploadStatus::Receiving);
    life.transfer_end(4);
    life.transfer_end(5); // an extra end never goes below none
    CHECK(life.status() == UploadStatus::Ready);
    life.fail("The port is taken");
    CHECK(life.status() == UploadStatus::Error);
    CHECK(life.reason() == "The port is taken");
    CHECK(life.remaining_ms(10) == 0);
    life.start(20);
    CHECK(life.status() == UploadStatus::Ready);
    CHECK(life.reason().empty());
    life.stop();
    CHECK(life.status() == UploadStatus::Off);
}
