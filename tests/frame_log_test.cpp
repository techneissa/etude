// SPDX-License-Identifier: GPL-3.0-or-later
// The frame log's CPU time beside the wall clock, and a scripted window call's time kept as its
// own detail.
#include "frame_log.hpp"
#include "timing_factor.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>

using namespace etude::app;

namespace {

constexpr std::int64_t kMs = 1'000'000;

} // namespace

TEST_CASE("frame log: the CPU time of each phase is kept beside its wall time") {
    FrameLog log(nullptr);
    log.start(1'000'000'000, 500'000'000);
    log.mark(FramePhase::Wait, 1'010'000'000, 500'000'000);   // 10 ms asleep: no CPU
    log.mark(FramePhase::Update, 1'020'000'000, 504'000'000); // 10 ms, 4 ms on the CPU
    log.mark(FramePhase::Present, 1'021'000'000, 504'500'000);
    CHECK_FALSE(log.finish(1'021'000'000, 504'500'000, 7));
    CHECK(log.last_period_ns() == 21 * kMs);
    CHECK(log.last_cpu_ns() == 4'500'000);
    CHECK(log.last_phase_cpu_ns(FramePhase::Wait) == 0);
    CHECK(log.last_phase_cpu_ns(FramePhase::Update) == 4 * kMs);
    CHECK(log.last_phase_ns(FramePhase::Update) == 10 * kMs);
    // The next frame's CPU time starts where this one's ended, as its wall time does.
    log.start(1'030'000'000);
    log.mark(FramePhase::Wait, 1'040'000'000, 504'500'000 + 100'000);
    CHECK(log.last_phase_cpu_ns(FramePhase::Wait) == 100'000);
}

TEST_CASE("frame log: a stall line names the CPU time") {
    char text[4096] = {};
    std::FILE* out = fmemopen(text, sizeof text, "w");
    REQUIRE(out != nullptr);
    FrameLog log(out);
    log.start(0, 0);
    log.mark(FramePhase::Scene, 40 * kMs, 3 * kMs);
    CHECK(log.finish(40 * kMs, 3 * kMs, 12));
    std::fclose(out);
    const std::string printed(text);
    CHECK(printed.find("stall frame 12: 40.0 ms") != std::string::npos);
    CHECK(printed.find("cpu 3.0 ms") != std::string::npos);
}

TEST_CASE("frame log: the thread CPU clock never runs backwards and runs while the thread works") {
    const std::int64_t before = thread_cpu_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(thread_cpu_ns() - before >= 0);
    // Spin until the thread has had 5 ms of CPU time, not for a fixed wall time: on a loaded
    // machine the thread can be descheduled for most of any wall window. The wall bound only
    // stops a clock that never moves.
    const std::int64_t work_start = thread_cpu_ns();
    const std::int64_t wall_start = frame_clock_ns();
    volatile std::uint64_t sink = 0;
    while (thread_cpu_ns() - work_start < 5 * kMs && frame_clock_ns() - wall_start < 60'000 * kMs) {
        sink = sink + 1;
    }
    CHECK(thread_cpu_ns() - work_start >= 5 * kMs);
}

TEST_CASE("frame log: the thread CPU clock does not run while the thread sleeps" * TIMING_ONLY) {
    const std::int64_t before = thread_cpu_ns();
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    CHECK(thread_cpu_ns() - before < 10 * kMs);
}

TEST_CASE("frame log: a scripted window call's time is kept as its own detail") {
    std::FILE* sink = std::fopen("/dev/null", "w");
    REQUIRE(sink != nullptr);
    FrameLog log(sink);
    set_active_frame_log(&log);
    log.start(frame_clock_ns());
    log.mark(FramePhase::Wait, frame_clock_ns());
    {
        const FrameDetailScope window_call(FrameDetail::Window);
        std::this_thread::sleep_for(std::chrono::milliseconds(40)); // the window system answers
    }
    log.mark(FramePhase::Events, frame_clock_ns());
    log.finish(frame_clock_ns(), 1);
    set_active_frame_log(nullptr);
    CHECK(log.last_detail_ns(FrameDetail::Window) >= 35 * kMs); // a sleep never ends early
    std::fclose(sink);
}
