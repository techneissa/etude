// SPDX-License-Identifier: GPL-3.0-or-later
// The sample clock: a line from the device's sample count to the steady clock that late callbacks
// hardly move.
#include "sample_clock.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <cstdlib>
#include <random>

using etude::app::SampleClock;

namespace {
constexpr std::int64_t kMs = 1'000'000;
constexpr int kRate = 48000;
constexpr int kBlock = 128;
constexpr std::int64_t kBlockNs = std::int64_t{kBlock} * 1'000'000'000 / kRate;
} // namespace

TEST_CASE("callbacks on an even beat give a line with the device's rate") {
    SampleClock clock;
    clock.reset(kRate);
    std::int64_t frames = 0;
    std::int64_t t = 1'000 * kMs;
    for (int i = 0; i < 100; ++i) {
        clock.observe(t, frames);
        frames += kBlock;
        t += kBlockNs;
    }
    // 48000 frames are one second.
    CHECK(clock.time_of(frames + 48000) - clock.time_of(frames) == 1'000'000'000);
    CHECK(std::llabs(clock.time_of(frames) - t) <= 2);
}

TEST_CASE(
    "a callback that wakes late hardly moves the line; one that comes early moves it at once") {
    SampleClock clock;
    clock.reset(kRate);
    std::int64_t frames = 0;
    std::int64_t t = 1'000 * kMs;
    for (int i = 0; i < 200; ++i) {
        clock.observe(t, frames);
        frames += kBlock;
        t += kBlockNs;
    }
    const std::int64_t before = clock.time_of(frames);
    // One callback 5 ms late: the line creeps forward by the least step.
    const std::int64_t late = clock.observe(t + 5 * kMs, frames);
    CHECK(late > 4 * kMs);
    CHECK(clock.time_of(frames) - before == SampleClock::kCreepNs);
    // A burst of ten late callbacks moves it ten more steps, no more.
    const std::int64_t frames0 = frames;
    for (int i = 0; i < 10; ++i) {
        frames += kBlock;
        t += kBlockNs;
        clock.observe(t + 5 * kMs, frames);
    }
    const std::int64_t unmoved = before + (frames - frames0) * 1'000'000'000 / kRate;
    CHECK(clock.time_of(frames) - unmoved <= 11 * SampleClock::kCreepNs);
    // One 2 ms early moves it there at once.
    frames += kBlock;
    t += kBlockNs;
    const std::int64_t predicted = clock.time_of(frames);
    const std::int64_t early = clock.observe(predicted - 2 * kMs, frames);
    CHECK(early == -2 * kMs);
    CHECK(clock.time_of(frames) == predicted - 2 * kMs);
}

TEST_CASE("jittery callbacks leave the line near the least wake-up delay") {
    std::mt19937 rng(7);
    SampleClock clock;
    clock.reset(kRate);
    std::int64_t frames = 0;
    std::int64_t worst = 0;
    std::int64_t best = INT64_MAX;
    for (int i = 0; i < 5000; ++i) {
        const std::int64_t ideal = 1'000 * kMs + frames * 1'000'000'000 / kRate;
        // Wake-ups up to 3 ms late, sometimes in a burst.
        const std::int64_t jitter = static_cast<std::int64_t>(rng() % 3'000'000);
        clock.observe(ideal + jitter, frames);
        if (i > 500) {
            const std::int64_t off = clock.time_of(frames) - ideal;
            worst = std::max(worst, off);
            best = std::min(best, off);
        }
        frames += kBlock;
    }
    // The line sits within a fraction of a millisecond of the ideal, never below it.
    CHECK(best >= 0);
    CHECK(worst < kMs / 2);
}

TEST_CASE("a steady drift between the two clocks is followed") {
    SampleClock clock;
    clock.reset(kRate);
    std::int64_t frames = 0;
    std::int64_t t = 1'000 * kMs;
    for (int i = 0; i < 20000; ++i) {
        clock.observe(t, frames);
        frames += kBlock;
        t += kBlockNs + 300; // the device runs 300 ns per block slow: about 112 ppm
    }
    CHECK(std::llabs(clock.time_of(frames) - t) < kMs / 10);
}
