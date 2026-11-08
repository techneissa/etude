// SPDX-License-Identifier: GPL-3.0-or-later
// A turning icon eases to its angle without deforming anything, and a number rolls like a counter:
// the old one leaves in the direction of change while the new one arrives, never more than a few
// layers, always settling on exactly the last value.
#include "etude/icon_motion.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>

using namespace etude;

namespace {
constexpr float kFrame = 1.0f / 60.0f;
constexpr float kQuarter = 1.5707964f;
} // namespace

TEST_CASE("turn: settles exactly on the angle without overshooting") {
    TurnIcon t;
    t.set(0.0f);
    CHECK_FALSE(t.moving());
    t.turn_to(kQuarter);
    REQUIRE(t.moving());
    float last = 0.0f;
    int frames = 0;
    while (t.step(kFrame)) {
        REQUIRE(++frames < 120);
        CHECK(t.angle() >= last);     // only one way
        CHECK(t.angle() <= kQuarter); // the smooth spring does not overshoot
        last = t.angle();
    }
    CHECK(t.angle() == kQuarter);
    CHECK_FALSE(t.moving());
}

TEST_CASE(
    "turn: the same angle again changes nothing, a new one mid-turn starts from where it is") {
    TurnIcon t;
    t.set(0.0f);
    t.turn_to(0.0f);
    CHECK_FALSE(t.moving());
    t.turn_to(kQuarter);
    for (int i = 0; i < 5; ++i) {
        t.step(kFrame);
    }
    const float here = t.angle();
    REQUIRE(here > 0.0f);
    REQUIRE(here < kQuarter);
    t.turn_to(0.0f); // back before it arrived
    CHECK(t.angle() == doctest::Approx(here));
    int frames = 0;
    while (t.step(kFrame)) {
        REQUIRE(++frames < 120);
    }
    CHECK(t.angle() == 0.0f);
}

TEST_CASE("turn: go without animation switches at once, also mid-turn") {
    TurnIcon t;
    t.go(kQuarter, false);
    CHECK(t.angle() == kQuarter);
    CHECK_FALSE(t.moving());
    t.go(0.0f, true);
    t.step(kFrame);
    REQUIRE(t.moving());
    t.go(kQuarter, false);
    CHECK_FALSE(t.moving());
    CHECK(t.angle() == kQuarter);
}

TEST_CASE("roll: an increasing value rises from below while the old one leaves through the top") {
    RollIcon r;
    r.set(5);
    CHECK(r.count() == 1);
    CHECK(r.value() == 5);
    CHECK(r.offset(0) == 0.0f);
    CHECK(r.alpha(0) == 1.0f);
    r.roll_to(6);
    REQUIRE(r.count() == 2);
    r.step(kFrame);
    int old_layer = r.layer_value(0) == 5 ? 0 : 1;
    int new_layer = 1 - old_layer;
    CHECK(r.layer_value(new_layer) == 6);
    CHECK(r.offset(new_layer) > 0.0f); // y grows downward: below the middle
    CHECK(r.offset(old_layer) < 0.0f); // above it
    CHECK(std::max(r.alpha(new_layer), r.alpha(old_layer)) >= 0.124f);
    int frames = 0;
    while (r.step(kFrame)) {
        REQUIRE(++frames < 120);
    }
    CHECK(r.count() == 1);
    CHECK(r.value() == 6);
    CHECK(r.offset(0) == 0.0f);
}

TEST_CASE("roll: a decreasing value does the reverse") {
    RollIcon r;
    r.set(6);
    r.roll_to(5);
    r.step(kFrame);
    REQUIRE(r.count() == 2);
    for (int i = 0; i < 2; ++i) {
        if (r.layer_value(i) == 5) {
            CHECK(r.offset(i) < 0.0f); // arrives from above
        } else {
            CHECK(r.offset(i) > 0.0f); // the old one leaves downward
        }
    }
}

TEST_CASE("roll: the same value changes nothing; go without animation switches at once") {
    RollIcon r;
    r.go(5, false);
    r.roll_to(5);
    CHECK_FALSE(r.moving());
    r.go(9, true);
    r.step(kFrame);
    REQUIRE(r.moving());
    r.go(12, false);
    CHECK_FALSE(r.moving());
    CHECK(r.count() == 1);
    CHECK(r.value() == 12);
    CHECK(r.offset(0) == 0.0f);
}

TEST_CASE("roll: rolls cut short by the next keep a few layers and settle on the last value") {
    RollIcon r;
    r.set(1);
    for (int n = 2; n <= 30; ++n) {
        r.roll_to(n);
        r.step(kFrame);
        r.step(kFrame);
        CHECK(r.count() >= 2);
        CHECK(r.count() <= kRollLayers);
        CHECK(r.value() == n);
        float shown = 0.0f;
        for (int i = 0; i < r.count(); ++i) {
            CHECK(r.alpha(i) >= 0.0f);
            CHECK(r.alpha(i) <= 1.0f);
            shown = std::max(shown, r.alpha(i));
        }
        CHECK(shown > 0.0f);
    }
    for (int n = 29; n >= 1; --n) { // and back down, the direction flipping mid-flight
        r.roll_to(n);
        r.step(kFrame);
        CHECK(r.count() <= kRollLayers);
    }
    int frames = 0;
    while (r.step(kFrame)) {
        REQUIRE(++frames < 240);
    }
    CHECK(r.count() == 1);
    CHECK(r.value() == 1);
    CHECK(r.offset(0) == 0.0f);
}

TEST_CASE("turn: reversing mid-turn keeps the angular speed") {
    TurnIcon t;
    t.set(0.0f);
    t.turn_to(kQuarter);
    for (int i = 0; i < 9; ++i) { // a tap, then the opposite tap 0.15 s later
        t.step(kFrame);
    }
    REQUIRE(t.moving());
    const float angle = t.angle();
    const float speed = t.velocity();
    REQUIRE(speed > 1.0f);
    t.turn_to(0.0f);
    CHECK(t.angle() == doctest::Approx(angle));
    CHECK(t.velocity() == doctest::Approx(speed).epsilon(1e-3)); // no jump at the retarget
    // It carries on the way it was going for a frame before the spring pulls it back.
    t.step(kFrame);
    CHECK(t.angle() > angle);
    int frames = 0;
    while (t.step(kFrame)) {
        REQUIRE(++frames < 240);
    }
    CHECK(t.angle() == 0.0f);
}

TEST_CASE("roll: retargeting mid-roll keeps the offset speed of what was on its way in") {
    RollIcon r;
    r.set(5);
    r.roll_to(6);
    for (int i = 0; i < 4; ++i) {
        r.step(kFrame);
    }
    int arriving = r.layer_value(0) == 6 ? 0 : 1;
    REQUIRE(r.layer_value(arriving) == 6);
    const float speed = r.velocity(arriving);
    const float at = r.offset(arriving);
    REQUIRE(std::fabs(speed) > 0.5f);
    r.roll_to(7); // 6 now leaves the way it came
    arriving = -1;
    for (int i = 0; i < r.count(); ++i) {
        if (r.layer_value(i) == 6) {
            arriving = i;
        }
    }
    REQUIRE(arriving >= 0);
    CHECK(r.offset(arriving) == doctest::Approx(at));
    CHECK(r.velocity(arriving) == doctest::Approx(speed).epsilon(1e-3));
    // And the same when the direction reverses.
    RollIcon d;
    d.set(5);
    d.roll_to(6);
    for (int i = 0; i < 4; ++i) {
        d.step(kFrame);
    }
    const int in = d.layer_value(0) == 6 ? 0 : 1;
    const float dspeed = d.velocity(in);
    d.roll_to(5); // back before it arrived
    for (int i = 0; i < d.count(); ++i) {
        if (d.layer_value(i) == 6) {
            CHECK(d.velocity(i) == doctest::Approx(dspeed).epsilon(1e-3));
        }
    }
}

TEST_CASE("roll: a spring that overshoots still settles and drops what has left") {
    RollIcon r;
    r.set(1);
    for (int n = 2; n <= 6; ++n) {
        r.roll_to(n, kSpringSnappy); // overshoots past 1 on the way
        int frames = 0;
        while (r.step(kFrame)) {
            REQUIRE(++frames < 240);
        }
        CHECK(r.count() == 1);
        CHECK(r.value() == n);
        CHECK(r.offset(0) == 0.0f);
        CHECK(r.alpha(0) == 1.0f);
    }
    // Cut short in the overshoot, then settled.
    r.roll_to(9, kSpringSnappy);
    for (int i = 0; i < 12; ++i) {
        r.step(kFrame);
    }
    r.roll_to(3, kSpringSnappy);
    int frames = 0;
    while (r.step(kFrame)) {
        REQUIRE(++frames < 240);
    }
    CHECK(r.count() == 1);
    CHECK(r.value() == 3);
    CHECK(r.offset(0) == 0.0f);
}
