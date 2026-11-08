// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/idle_gate.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr std::int64_t kFrame = 16'666'667;
constexpr std::int64_t kSettle = 300'000'000;
constexpr std::int64_t kSecond = 1'000'000'000;

IdleFrame still(std::uint64_t picture = 7) { return {false, false, true, picture}; }
IdleFrame moved(std::uint64_t picture) { return {false, false, true, picture}; }

// Draws still frames from `from` for `duration` and returns the time of the last one.
std::int64_t draw_still(IdleGate& g, std::int64_t from, std::int64_t duration,
                        std::uint64_t picture = 7) {
    std::int64_t t = from;
    for (; t < from + duration; t += kFrame) {
        g.frame_drawn(t, still(picture));
    }
    return t - kFrame;
}

} // namespace

TEST_CASE("a new gate draws: nothing is known to be still") {
    IdleGate g;
    CHECK_FALSE(g.idle(0));
    CHECK_FALSE(g.idle(10 * kSecond));
}

TEST_CASE("a picture that stays the same for the settle time lets the loop idle") {
    IdleGate g;
    const std::int64_t last = draw_still(g, 0, 2 * kSecond);
    CHECK(g.idle(last));
    CHECK(g.wait_ns() > 0);
}

TEST_CASE("an easing that finishes just before the settle time keeps drawing until it settles") {
    IdleGate g;
    g.frame_drawn(0, moved(1));
    std::int64_t t = kFrame;
    std::uint64_t picture = 2;
    for (; t < kSecond; t += kFrame) {
        g.frame_drawn(t, moved(picture++)); // the picture changes every frame for a second
    }
    // The last frame that differs from the one before it is the first one at the final picture.
    const std::int64_t last_change = t;
    g.frame_drawn(t, still(picture));
    g.frame_drawn(t + kFrame, still(picture));
    CHECK_FALSE(g.idle(last_change + kSettle - 1));
    CHECK(g.idle(last_change + kSettle));
}

TEST_CASE("a change one frame before the settle time restarts it") {
    IdleGate g;
    const std::int64_t last = draw_still(g, 0, kSettle - kFrame); // just under the settle time
    CHECK_FALSE(g.idle(last));
    g.frame_drawn(last + kFrame, moved(99)); // a toast fades in
    CHECK_FALSE(g.idle(last + kFrame + kSettle - 1));
    CHECK(g.idle(last + kFrame + kSettle));
}

TEST_CASE("input restarts the settle time even when the picture is the same") {
    IdleGate g;
    const std::int64_t last = draw_still(g, 0, kSecond);
    REQUIRE(g.idle(last));
    g.frame_drawn(last + kFrame, {true, false, true, 7});
    CHECK_FALSE(g.idle(last + kFrame + kSettle - 1));
    CHECK(g.idle(last + kFrame + kSettle));
}

TEST_CASE("something known to move keeps the loop drawing") {
    IdleGate g;
    std::int64_t last = 0;
    for (std::int64_t t = 0; t < 5 * kSecond; t += kFrame) {
        g.frame_drawn(t, {false, true, true, 7}); // the picture happens to repeat
        CHECK_FALSE(g.idle(t));
        last = t;
    }
    CHECK_FALSE(g.idle(last + kSettle - 1));
    CHECK(g.idle(last + kSettle));
}

TEST_CASE("a frame without a picture counts as moving") {
    IdleGate g;
    for (std::int64_t t = 0; t < 2 * kSecond; t += kFrame) {
        g.frame_drawn(t, {false, false, false, 0});
    }
    CHECK_FALSE(g.idle(2 * kSecond));
}

TEST_CASE("the first still frame after a busy stretch compares with nothing and counts as moving") {
    IdleGate g;
    g.frame_drawn(0, {false, true, false, 0}); // a song played: no picture kept
    g.frame_drawn(kFrame, still());
    CHECK_FALSE(g.idle(kFrame + kSettle - 1));
    g.frame_drawn(2 * kFrame, still());
    CHECK(g.idle(kFrame + kSettle));
}

TEST_CASE("the tick frame after a wait that finds the same picture stays idle") {
    IdleGate g;
    const std::int64_t last = draw_still(g, 0, kSecond);
    REQUIRE(g.idle(last));
    g.waited();
    CHECK(g.first_after_wait());
    g.frame_drawn(last + 250'000'000, still());
    CHECK_FALSE(g.first_after_wait());
    CHECK(g.idle(last + 250'000'000));
}

TEST_CASE("the tick frame that finds a new picture wakes the loop for good") {
    IdleGate g;
    const std::int64_t last = draw_still(g, 0, kSecond);
    g.waited();
    g.frame_drawn(last + 250'000'000, moved(8)); // a timer fired
    CHECK_FALSE(g.idle(last + 250'000'000));
    CHECK_FALSE(g.idle(last + 250'000'000 + kSettle - 1));
}

TEST_CASE("a song counting in, playing or counting down to resume never idles") {
    CHECK(song_keeps_drawing(TransportState::CountIn, false));
    CHECK(song_keeps_drawing(TransportState::Playing, false));
    CHECK(song_keeps_drawing(TransportState::Paused, true));
    CHECK(song_keeps_drawing(TransportState::Held, true));
    CHECK_FALSE(song_keeps_drawing(TransportState::Paused, false));
    CHECK_FALSE(song_keeps_drawing(TransportState::Held, false));
    CHECK_FALSE(song_keeps_drawing(TransportState::Ended, false));
}
