// SPDX-License-Identifier: GPL-3.0-or-later
#include "idle_wait.hpp"

#include <doctest/doctest.h>

using namespace etude;
using namespace etude::app;

namespace {

constexpr std::int64_t kFrame = 16'666'667;
constexpr std::int64_t kSecond = 1'000'000'000;

int g_blocks = 0;
std::int64_t g_waited_ns = 0;
int g_wakes = 0;

void fake_block(std::int64_t timeout_ns) {
    ++g_blocks;
    g_waited_ns = timeout_ns;
}
void fake_wake() { ++g_wakes; }

// A loop that has drawn still frames for a second, so it may idle.
IdleLoop settled_loop(std::int64_t* last) {
    g_blocks = 0;
    g_wakes = 0;
    IdleLoop loop(true, fake_block, fake_wake);
    std::int64_t t = 0;
    for (; t < kSecond; t += kFrame) {
        loop.begin_frame(false, false);
        loop.end_frame(t);
    }
    *last = t - kFrame;
    return loop;
}

} // namespace

TEST_CASE("an idle loop waits and the frame after the wait is the first after it") {
    std::int64_t last = 0;
    IdleLoop loop = settled_loop(&last);
    CHECK(loop.wait_if_idle(last));
    CHECK(g_blocks == 1);
    CHECK(g_waited_ns > 0);
    CHECK(loop.first_after_wait());
    loop.begin_frame(false, false);
    loop.end_frame(last + kFrame);
    CHECK_FALSE(loop.first_after_wait());
}

TEST_CASE("a request that came after the frame began is no wait: the next frame is not the first after one") {
    std::int64_t last = 0;
    IdleLoop loop = settled_loop(&last);
    loop.begin_frame(false, false);
    wake_loop(); // a MIDI note arrived after the ports were drained
    CHECK_FALSE(loop.wait_if_idle(last));
    CHECK(g_blocks == 0);
    CHECK_FALSE(loop.first_after_wait());
    loop.end_frame(last + kFrame);
}

TEST_CASE("a loop that is off or not yet still never waits") {
    IdleLoop off(false, fake_block, fake_wake);
    CHECK_FALSE(off.wait_if_idle(10 * kSecond));
    IdleLoop fresh(true, fake_block, fake_wake);
    CHECK_FALSE(fresh.wait_if_idle(10 * kSecond));
    CHECK(g_blocks == 0);
}

TEST_CASE("input and known motion keep the loop drawing") {
    std::int64_t last = 0;
    IdleLoop loop = settled_loop(&last);
    loop.note_input(true);
    loop.begin_frame(false, false);
    loop.end_frame(last + kFrame);
    CHECK_FALSE(loop.wait_if_idle(last + kFrame));
    loop.begin_frame(false, true); // a song plays
    loop.end_frame(last + 2 * kFrame);
    CHECK_FALSE(loop.wait_if_idle(last + 2 * kFrame + 100'000'000));
}

TEST_CASE("a redraw inside the OS resize loop is not a frame of the loop") {
    std::int64_t last = 0;
    IdleLoop loop = settled_loop(&last);
    loop.begin_frame(true, true);
    CHECK(loop.wait_if_idle(last)); // busy was not taken from the live redraw
}

TEST_CASE("a burst of wake requests wakes a waiting loop once") {
    set_wake_hook(fake_wake);
    g_wakes = 0;
    const std::uint64_t seen = wake_requests();
    CHECK(arm_wait(seen));
    for (int i = 0; i < 20; ++i) {
        wake_loop();
    }
    CHECK(g_wakes == 1);
    disarm_wait();
    wake_loop(); // not waiting: only counted
    CHECK(g_wakes == 1);
    CHECK_FALSE(arm_wait(seen)); // 21 requests since `seen`
    disarm_wait();
}

TEST_CASE("wake sources are counted and named") {
    const WakeCounts before = wake_counts();
    CHECK(wake_report(before, wake_counts()) == "none");
    wake_loop(WakeSource::Upload);
    wake_loop(WakeSource::Upload);
    wake_loop(WakeSource::Library);
    CHECK(wake_report(before, wake_counts()) == "library 1, upload 2");
}
