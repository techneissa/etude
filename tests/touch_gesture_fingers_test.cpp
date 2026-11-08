// SPDX-License-Identifier: GPL-3.0-or-later
// touch_gesture: fingers (split from touch_gesture_test.cpp).
#include "etude/touch_gesture.hpp"
#include "etude/finger_claims.hpp"
#include "etude/finger_count.hpp"
#include "etude/handle_grab.hpp"
#include "etude/touch_kind.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

#include "touch_gesture_test_support.hpp"

using namespace etude;
using Kind = GestureRecognizer::Event::Kind;
using namespace touch_gesture_test_support;

// ---- The finger count: what tells a landing finger from a second one

TEST_CASE("the count follows downs and ups, real and scripted apart") {
    FingerCount c;
    c.down(false, 1);
    CHECK(c.total() == 1);
    c.down(true, 1);
    CHECK(c.total() == 2);
    CHECK(c.real() == 1);
    c.up(false, 2);
    c.up(false, 2); // a lift too many never goes under zero
    CHECK(c.real() == 0);
    CHECK(c.total() == 1);
    c.cancelled(3);
    CHECK(c.total() == 0);
}

TEST_CASE("a resting finger is not forgotten while SDL still has it down") {
    // A down, B down, A up; B rests with no event, the pointer (the primary finger's mouse) is up.
    FingerCount c;
    c.down(false, 10); // A
    c.down(false, 10); // B
    c.up(false, 12);   // A lifts
    REQUIRE(c.total() == 1);
    // Four frames and more of nothing: the count asks SDL, and SDL says one finger is down.
    CHECK_FALSE(c.needs_check(false, 15 - 1)); // not yet: inside the idle frames
    REQUIRE(c.needs_check(false, 16));
    c.settle(1);
    CHECK(c.total() == 1);
    // C lands: B is known down, so C is a second finger and B's touch is kept.
    c.down(false, 40);
    CHECK(c.total() == 2);
    // Both lift: nothing to ask any more.
    c.up(false, 41);
    c.up(false, 41);
    CHECK_FALSE(c.needs_check(false, 60));
}

TEST_CASE("a lift that never came is forgotten once SDL says no finger is down") {
    FingerCount c;
    c.down(false, 10);
    CHECK_FALSE(c.needs_check(false, 12));
    REQUIRE(c.needs_check(false, 14));
    c.settle(0);
    CHECK(c.total() == 0);
    // And the next finger lands alone.
    c.down(false, 30);
    CHECK(c.total() == 1);
}

TEST_CASE("a held pointer, or a finger that moved, keeps the count unchecked") {
    FingerCount c;
    c.down(false, 10);
    CHECK_FALSE(c.needs_check(true, 100)); // the pointer is down: someone is touching
    c.moved(100);
    CHECK_FALSE(c.needs_check(false, 102)); // a move is a sign of life
    CHECK(c.needs_check(false, 104));
}

TEST_CASE("scripted fingers are never forgotten by time") {
    FingerCount c;
    c.down(true, 10);
    CHECK_FALSE(c.needs_check(false, 1000));
    c.settle(0); // SDL knows nothing of synthetic fingers
    CHECK(c.total() == 1);
    c.up(true, 1001);
    CHECK(c.total() == 0);
}

// ---- Touch device kinds

TEST_CASE("a trackpad's fingers are not the screen's; every other kind is, unknown included") {
    CHECK(is_screen_touch(TouchKind::Direct));
    CHECK(is_screen_touch(TouchKind::Unknown));
    CHECK_FALSE(is_screen_touch(TouchKind::IndirectAbsolute));
    CHECK_FALSE(is_screen_touch(TouchKind::IndirectRelative));
}

TEST_CASE("each touch device is logged once, and past the cap the log says so once") {
    TouchDeviceLog log;
    using Say = TouchDeviceLog::Say;
    CHECK(log.observe(7) == Say::Device);
    CHECK(log.observe(7) == Say::Nothing);
    CHECK(log.observe(7) == Say::Nothing);
    for (std::uint64_t id = 100; id < 100 + TouchDeviceLog::kMaxDevices - 1; ++id) {
        CHECK(log.observe(id) == Say::Device);
    }
    // The list is full: the next new device is the one "too many" line, then silence, however
    // many fingers come from it or from the ones after it.
    CHECK(log.observe(900) == Say::TooMany);
    for (int i = 0; i < 20; ++i) {
        CHECK(log.observe(900) == Say::Nothing);
        CHECK(log.observe(901 + static_cast<std::uint64_t>(i)) == Say::Nothing);
    }
    // A device already listed stays quiet.
    CHECK(log.observe(7) == Say::Nothing);
    CHECK(log.observe(100) == Say::Nothing);
}

// ---- Fingers claimed by a control over a screen

TEST_CASE("a claimed finger is the control's from landing to lift") {
    FingerClaims c;
    CHECK_FALSE(c.claimed(5));
    c.claim(5);
    c.claim(5); // twice is once
    CHECK(c.size() == 1);
    CHECK(c.claimed(5));
    CHECK_FALSE(c.claimed(6));
    c.release(5);
    CHECK_FALSE(c.claimed(5));
    c.claim(1);
    c.claim(2);
    c.clear();
    CHECK(c.size() == 0);
}

TEST_CASE("only a few fingers can be claimed, and a full list claims no more") {
    FingerClaims c;
    for (std::int64_t id = 0; id < static_cast<std::int64_t>(FingerClaims::kMax) + 3; ++id) {
        c.claim(id);
    }
    CHECK(c.size() == FingerClaims::kMax);
    CHECK_FALSE(c.claimed(static_cast<std::int64_t>(FingerClaims::kMax)));
}

// ---- A hold on a section handle (the recogniser's events through HandleGrab)

namespace {

// The recogniser's events for one touch, through HandleGrab the way the editor feeds it: the song
// time under a point is x / 10 seconds, and a handle may sit anywhere between 1 s and 30 s.
struct HoldRig {
    GestureRecognizer g;
    HandleGrab grab;
    std::vector<HandleGrabStep> steps;
    HoldRig() {
        GestureRecognizer::Config c;
        c.drag_after_long_press = true;
        g = GestureRecognizer(c);
    }
    void feed() {
        for (const auto& e : drain(g)) {
            if (e.kind == Kind::LongPress) {
                grab.grab(2, 10.0, e.x / 10.0); // the handle at 10 s, the finger on it
                continue;
            }
            steps.push_back(grab.on_event(e, e.x / 10.0, [](double raw) {
                return std::clamp(std::round(raw), 1.0, 30.0);
            }));
        }
    }
    int count(HandleGrabStep::Kind k) const {
        int n = 0;
        for (const auto& s : steps) {
            n += s.kind == k ? 1 : 0;
        }
        return n;
    }
};

} // namespace

TEST_CASE("a hold takes the handle, moving moves it, the lift commits where it was moved to") {
    HoldRig r;
    r.g.down(1, 100, 50, 0.0);
    r.g.update(0.5);
    r.feed();
    REQUIRE(r.grab.active());
    r.g.move(1, 130, 50, 0.6);
    r.g.move(1, 160, 50, 0.7);
    r.g.up(1, 160, 50, 0.8);
    r.feed();
    CHECK(r.count(HandleGrabStep::Kind::Moved) == 2);
    REQUIRE(r.count(HandleGrabStep::Kind::Commit) == 1);
    const HandleGrabStep& last = r.steps.back();
    CHECK(last.kind == HandleGrabStep::Kind::Commit);
    CHECK(last.handle == 2);
    CHECK(last.time_s == doctest::Approx(16.0)); // 10 s, and the finger moved 6 s of screen
    CHECK_FALSE(r.grab.active());
}

TEST_CASE("a hold that lifts without moving lets the handle go as it was, and edits nothing") {
    HoldRig r;
    r.g.down(1, 100, 50, 0.0);
    r.g.update(0.5);
    r.feed();
    r.g.move(1, 103, 51, 0.6); // a tremor inside the slop
    r.g.up(1, 103, 51, 0.9);
    r.feed();
    CHECK(r.count(HandleGrabStep::Kind::Moved) == 0);
    CHECK(r.count(HandleGrabStep::Kind::Commit) == 0);
    REQUIRE(r.count(HandleGrabStep::Kind::Released) == 1);
    CHECK(r.steps.back().time_s == doctest::Approx(10.0));
    CHECK_FALSE(r.grab.active());
}

TEST_CASE("a second finger or a cancel during the hold drops the handle with no commit") {
    HoldRig r;
    r.g.down(1, 100, 50, 0.0);
    r.g.update(0.5);
    r.feed();
    r.g.move(1, 150, 50, 0.6);
    r.g.down(2, 300, 50, 0.65); // a second finger: the hold's touch is cancelled
    r.feed();
    CHECK(r.count(HandleGrabStep::Kind::Commit) == 0);
    CHECK_FALSE(r.grab.active());
    r.g.up(2, 300, 50, 0.9);
    r.g.up(1, 150, 50, 0.95);
    r.feed();
    CHECK(r.count(HandleGrabStep::Kind::Commit) == 0);
    CHECK(r.count(HandleGrabStep::Kind::Released) == 0);
}

TEST_CASE("a hold on a handle that was never grabbed does nothing") {
    HandleGrab grab;
    GestureRecognizer::Event e;
    e.kind = Kind::HeldEnd;
    const HandleGrabStep s = grab.on_event(e, 5.0, [](double v) { return v; });
    CHECK(s.kind == HandleGrabStep::Kind::None);
}

TEST_CASE("a lost lift after a hold: the next touch starts clean and the old grab is gone") {
    HoldRig r;
    r.g.down(1, 100, 50, 0.0);
    r.g.update(0.5);
    r.feed();
    REQUIRE(r.grab.active());
    r.g.down(2, 400, 60, 9.0, false); // no other finger on the screen: the old touch lost its lift
    r.feed();
    CHECK_FALSE(r.grab.active());
    r.g.up(2, 400, 60, 9.05);
    r.feed();
    CHECK(r.count(HandleGrabStep::Kind::Commit) == 0);
}
