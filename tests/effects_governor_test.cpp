// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/effects_governor.hpp"

#include <doctest/doctest.h>

#include <utility>

using namespace etude;

namespace {

constexpr std::int64_t kBudget = 16'666'667;
constexpr std::int64_t kSlow = 25'000'000;  // well over budget
constexpr std::int64_t kFast = 6'000'000;   // well inside the headroom
constexpr std::int64_t kNear = 14'000'000;  // under budget but not headroom
constexpr std::int64_t kPeriod60 = 16'666'667;
constexpr std::int64_t kPeriod120 = 8'333'333;
constexpr std::int64_t kHalfSecond = 500'000'000;
constexpr std::int64_t kFiveSeconds = 5'000'000'000;

void feed(EffectsGovernor& g, std::int64_t ns, int frames, std::int64_t interval = kPeriod60) {
    for (int i = 0; i < frames; ++i) {
        g.add_frame(ns, interval);
    }
}

// Frames of `ns` at `interval` until the level changes: the time that took, or -1 within `limit_ns`.
std::int64_t time_until_change(EffectsGovernor& g, std::int64_t ns, std::int64_t limit_ns,
                               std::int64_t interval = kPeriod60) {
    const EffectsLevel start = g.level();
    std::int64_t elapsed = 0;
    while (elapsed < limit_ns) {
        elapsed += interval;
        if (g.add_frame(ns, interval) != start) {
            return elapsed;
        }
    }
    return -1;
}

// The level after `ns` of slow frames at `interval`, from a fresh governor on that refresh rate.
EffectsLevel level_after_slow(std::int64_t duration_ns, std::int64_t interval) {
    GovernorConfig c;
    c.budget_ns = interval;
    EffectsGovernor g(c);
    for (std::int64_t t = 0; t < duration_ns; t += interval) {
        g.add_frame(interval * 2, interval);
    }
    return g.level();
}

} // namespace

TEST_CASE("levels drop effects in the fixed order: shaders, particles, motion, glow width, "
          "extras") {
    const EffectsBudget full = effects_budget(EffectsLevel::Full);
    CHECK(full.particles);
    CHECK(full.motion_background);
    CHECK(full.glow_width == 1.0f);
    CHECK(full.style_extras);

    const EffectsBudget no_shaders = effects_budget(EffectsLevel::NoShaders);
    CHECK(full.shaders);
    CHECK_FALSE(no_shaders.shaders);
    CHECK(no_shaders.particles); // the shaders go first, before anything else
    CHECK(no_shaders.motion_background);
    CHECK(no_shaders.glow_width == 1.0f);
    CHECK(no_shaders.style_extras);

    const EffectsBudget l1 = effects_budget(EffectsLevel::NoParticles);
    CHECK_FALSE(l1.shaders);
    CHECK_FALSE(l1.particles);
    CHECK(l1.motion_background);
    CHECK(l1.glow_width == 1.0f);
    CHECK(l1.style_extras);

    const EffectsBudget l2 = effects_budget(EffectsLevel::NoMotion);
    CHECK_FALSE(l2.particles);
    CHECK_FALSE(l2.motion_background);
    CHECK(l2.glow_width == 1.0f);
    CHECK(l2.style_extras);

    const EffectsBudget l3 = effects_budget(EffectsLevel::NarrowGlow);
    CHECK_FALSE(l3.motion_background);
    CHECK(l3.glow_width < 1.0f);
    CHECK(l3.glow_width > 0.0f);
    CHECK(l3.style_extras);

    const EffectsBudget l4 = effects_budget(EffectsLevel::NoStyleExtras);
    CHECK_FALSE(l4.particles);
    CHECK_FALSE(l4.motion_background);
    CHECK(l4.glow_width < 1.0f);
    CHECK_FALSE(l4.style_extras);
}

TEST_CASE("every level has a name") {
    for (EffectsLevel l : kAllEffectsLevels) {
        CHECK_FALSE(effects_level_name(l).empty());
    }
    CHECK(effects_level_name(EffectsLevel::Full) == "full");
}

TEST_CASE("with shaders the first step down drops only the shaders, and the last step up gives them "
          "back") {
    GovernorConfig config;
    config.shader_level = true;
    EffectsGovernor g(config);
    REQUIRE(time_until_change(g, kSlow, 5 * kHalfSecond) > 0);
    CHECK(g.level() == EffectsLevel::NoShaders);
    REQUIRE(time_until_change(g, kSlow, 5 * kHalfSecond) > 0);
    CHECK(g.level() == EffectsLevel::NoParticles);
    feed(g, kNear / 2, 20'000); // a long run of headroom: all the way back up
    CHECK(g.level() == EffectsLevel::Full);
}

TEST_CASE("the shader level follows set_shader_level: off skips NoShaders, and off at NoShaders is "
          "Full") {
    EffectsGovernor g;
    g.set_shader_level(true);
    REQUIRE(time_until_change(g, kSlow, 5 * kHalfSecond) > 0);
    CHECK(g.level() == EffectsLevel::NoShaders);
    g.set_shader_level(false); // the style changed to one with no shader effect
    CHECK(g.level() == EffectsLevel::Full);
    CHECK(g.lowest() == EffectsLevel::Full);
    REQUIRE(time_until_change(g, kSlow, 5 * kHalfSecond) > 0);
    CHECK(g.level() == EffectsLevel::NoParticles); // the first step is a real one again
    g.set_shader_level(true); // back to a Neon style: the level stays, the way up passes NoShaders
    CHECK(g.level() == EffectsLevel::NoParticles);
    feed(g, kNear / 2, 20'000);
    CHECK(g.level() == EffectsLevel::Full);
}

TEST_CASE("frames within budget keep full effects") {
    EffectsGovernor g;
    feed(g, kNear, 2000);
    CHECK(g.level() == EffectsLevel::Full);
    CHECK(g.lowest() == EffectsLevel::Full);
}

TEST_CASE("sustained overrun steps down one level at a time, in order, to the floor") {
    EffectsGovernor g;
    for (const EffectsLevel expected : kAllEffectsLevels) {
        if (expected == EffectsLevel::Full || expected == EffectsLevel::NoShaders) {
            continue; // no shaders in this governor: Full goes straight to NoParticles
        }
        const std::int64_t t = time_until_change(g, kSlow, 5 * kHalfSecond);
        REQUIRE(t > 0);
        CHECK(t >= kHalfSecond);
        CHECK(g.level() == expected);
    }
    // The floor still draws notes and keys; nothing goes below it.
    feed(g, kSlow * 4, 10'000);
    CHECK(g.level() == kLowestEffectsLevel);
    CHECK(g.lowest() == kLowestEffectsLevel);
}

TEST_CASE("one slow frame does not degrade") {
    EffectsGovernor g;
    feed(g, kNear, 100);
    g.add_frame(100'000'000); // a 100 ms hitch
    feed(g, kNear, 200);
    CHECK(g.level() == EffectsLevel::Full);
}

TEST_CASE("a short burst under the down threshold does not degrade") {
    EffectsGovernor g;
    feed(g, kNear, 100);
    feed(g, kSlow, 15); // 0.25 s
    feed(g, kFast, 200);
    CHECK(g.level() == EffectsLevel::Full);
}

TEST_CASE("headroom steps back up only after a long run, slower than stepping down") {
    EffectsGovernor g;
    feed(g, kSlow, 200);
    REQUIRE(g.level() != EffectsLevel::Full);
    const EffectsLevel degraded = g.level();
    // Frames just under budget are not headroom: the level holds (hysteresis band).
    feed(g, kNear, 3000);
    CHECK(g.level() == degraded);
    // Real headroom brings it back one step after up_frames.
    const std::int64_t t = time_until_change(g, kFast, 10 * kFiveSeconds);
    CHECK(t >= kFiveSeconds);
    CHECK(static_cast<int>(g.level()) == static_cast<int>(degraded) - 1);
    CHECK(g.config().up_ns > g.config().down_ns);
}

TEST_CASE("sustained headroom recovers full effects and lowest() remembers the worst") {
    EffectsGovernor g;
    feed(g, kSlow, 1000);
    REQUIRE(g.level() == kLowestEffectsLevel);
    feed(g, kFast, 100'000);
    CHECK(g.level() == EffectsLevel::Full);
    CHECK(g.lowest() == kLowestEffectsLevel);
    g.reset();
    CHECK(g.lowest() == EffectsLevel::Full);
}

TEST_CASE("flapping backs off: a step up that overruns again doubles the wait") {
    GovernorConfig c;
    c.down_ns = 10 * kPeriod60;
    c.up_ns = 100 * kPeriod60;
    EffectsGovernor g(c);
    feed(g, kSlow, 60);
    REQUIRE(g.level() != EffectsLevel::Full);
    CHECK(g.up_backoff() == 1);
    REQUIRE(time_until_change(g, kFast, 1000 * kPeriod60) > 0); // steps up
    REQUIRE(time_until_change(g, kSlow, 1000 * kPeriod60) > 0); // overruns right away: steps down
    CHECK(g.up_backoff() == 2);
    const std::int64_t t = time_until_change(g, kFast, 10'000 * kPeriod60);
    CHECK(t >= 2 * c.up_ns);
}

TEST_CASE("bad frame times are ignored") {
    EffectsGovernor g;
    feed(g, 0, 1000);
    feed(g, -5, 1000);
    CHECK(g.level() == EffectsLevel::Full);
    CHECK(g.average_ns() == 0.0);
}

TEST_CASE("budget follows the config") {
    GovernorConfig c;
    c.budget_ns = 8'333'333; // 120 Hz
    EffectsGovernor g(c);
    feed(g, 12'000'000, 200); // fine at 60 Hz, over at 120 Hz
    CHECK(g.level() != EffectsLevel::Full);
    CHECK(kBudget > c.budget_ns);
}

TEST_CASE("frame cost is the work, or the interval when a refresh was missed") {
    constexpr std::int64_t period = 16'666'667;
    CHECK(frame_cost_ns(4'000'000, period, period) == 4'000'000);
    CHECK(frame_cost_ns(4'000'000, period + 2'000'000, period) == 4'000'000); // jitter
    CHECK(frame_cost_ns(4'000'000, 2 * period, period) == 2 * period);       // missed one
    CHECK(frame_cost_ns(40'000'000, 2 * period, period) == 40'000'000);
    CHECK(frame_cost_ns(4'000'000, 3 * period, 0) == 4'000'000); // no known period
}

TEST_CASE("the same run of slow frames steps down at the same moment at 60 Hz and 120 Hz") {
    // Just under 0.5 s: no step at either rate. Just over: a step at both.
    CHECK(level_after_slow(kHalfSecond - 30'000'000, kPeriod60) == EffectsLevel::Full);
    CHECK(level_after_slow(kHalfSecond - 30'000'000, kPeriod120) == EffectsLevel::Full);
    CHECK(level_after_slow(kHalfSecond + 30'000'000, kPeriod60) == EffectsLevel::NoParticles);
    CHECK(level_after_slow(kHalfSecond + 30'000'000, kPeriod120) == EffectsLevel::NoParticles);
}

TEST_CASE("the step down comes after 0.5 s of slow frames, whatever the rate") {
    for (const std::int64_t period : {kPeriod60, kPeriod120}) {
        GovernorConfig c;
        c.budget_ns = period;
        EffectsGovernor g(c);
        const std::int64_t t = time_until_change(g, period * 2, 2 * kHalfSecond, period);
        CHECK(t >= kHalfSecond - period);
        CHECK(t <= kHalfSecond + period);
    }
}

TEST_CASE("the step up comes after 5 s of headroom, whatever the rate") {
    for (const std::int64_t period : {kPeriod60, kPeriod120}) {
        GovernorConfig c;
        c.budget_ns = period;
        EffectsGovernor g(c);
        feed(g, period * 2, static_cast<int>(600'000'000 / period), period); // one step down
        REQUIRE(g.level() == EffectsLevel::NoParticles);
        const EffectsLevel degraded = g.level();
        // The average needs about 0.3 s to fall into the headroom band, so 4.7 s in is under 5 s
        // of headroom and 5.6 s in is over it.
        std::int64_t t = 0;
        while (t < 4'700'000'000) {
            g.add_frame(period / 3, period);
            t += period;
        }
        CHECK(g.level() == degraded);
        while (t < 5'600'000'000) {
            g.add_frame(period / 3, period);
            t += period;
        }
        CHECK(g.level() == EffectsLevel::Full); // one step up: no shader level here
    }
}

TEST_CASE("the backoff window is in seconds too") {
    for (const std::int64_t period : {kPeriod60, kPeriod120}) {
        GovernorConfig c;
        c.budget_ns = period;
        EffectsGovernor g(c);
        feed(g, period * 2, static_cast<int>(600'000'000 / period), period); // one step down
        REQUIRE(time_until_change(g, period / 3, 2 * kFiveSeconds, period) > 0); // up
        REQUIRE(time_until_change(g, period * 2, 2 * kFiveSeconds, period) > 0); // down within 5 s
        CHECK(g.up_backoff() == 2);
        // Now 5 s of headroom is not enough, 10 s is.
        CHECK(time_until_change(g, period / 3, kFiveSeconds, period) == -1);
        CHECK(time_until_change(g, period / 3, 2 * kFiveSeconds, period) > 0);
    }
}

TEST_CASE("the moving average smooths over the same time at 60 Hz and 120 Hz") {
    // A step from fast frames to slow ones: after 100 ms the average has moved as far at both.
    auto average_after_100ms = [](std::int64_t period) {
        GovernorConfig c;
        c.budget_ns = period;
        EffectsGovernor g(c);
        feed(g, kFast, 20, period);
        for (std::int64_t t = 0; t < 100'000'000; t += period) {
            g.add_frame(kSlow, period);
        }
        return g.average_ns();
    };
    const double a60 = average_after_100ms(kPeriod60);
    const double a120 = average_after_100ms(kPeriod120);
    CHECK(a60 == doctest::Approx(a120).epsilon(0.05));
    CHECK(a60 > kFast);
    CHECK(a60 < kSlow);
}

TEST_CASE("a slow frame right after a wait still counts, and the wait itself does not") {
    EffectsGovernor g;
    feed(g, kNear, 100);
    const double before = g.average_ns();
    // Slow frames that each follow a wait: the gap between them is not counted, the frames are.
    g.add_frame_after_gap(kSlow);
    CHECK(g.average_ns() > before);
    for (int i = 0; i < 100; ++i) {
        g.add_frame_after_gap(kSlow);
    }
    CHECK(g.level() == EffectsLevel::Full); // each restarts the runs: 0.5 s never builds up
    // A real stall that goes on after the first frame does step down, from that frame on.
    g.add_frame_after_gap(kSlow);
    feed(g, kSlow, 31);
    CHECK(g.level() == EffectsLevel::NoParticles);
}

TEST_CASE("the wait itself is no slow frame: after a gap the runs and the level are kept") {
    EffectsGovernor g;
    feed(g, kSlow, 20); // 0.33 s over budget
    const double average = g.average_ns();
    g.add_frame_after_gap(kFast); // the loop woke; this frame was cheap
    CHECK(g.average_ns() < average);
    feed(g, kSlow, 20); // the gap broke the run
    CHECK(g.level() == EffectsLevel::Full);
}

TEST_CASE("the step down comes at exactly N slow frames, not one before, at 60 Hz and 120 Hz") {
    // The frame periods rounded up, so N of them are at least 0.5 s: 30 at 60 Hz, 60 at 120 Hz.
    for (const auto& [period, frames] :
         {std::pair<std::int64_t, int>{16'666'667, 30}, std::pair<std::int64_t, int>{8'333'334, 60}}) {
        GovernorConfig c;
        c.budget_ns = period;
        EffectsGovernor g(c);
        feed(g, period * 2, frames - 1, period);
        CHECK(g.level() == EffectsLevel::Full);
        g.add_frame(period * 2, period);
        CHECK(g.level() == EffectsLevel::NoParticles);
    }
}
