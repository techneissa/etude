// SPDX-License-Identifier: GPL-3.0-or-later
// The one tap rule every control shares: a tap fires on release, inside the slop, not cancelled; a
// hold past the long-press time repeats (core tap_rule.hpp).
#include "etude/tap_rule.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr float kSlop = kTouchSlopPt;
constexpr double kLong = kLongPressS;

TapRuleConfig cfg(double repeat = 0.0) {
    TapRuleConfig c;
    c.repeat_s = repeat;
    return c;
}

} // namespace

TEST_CASE("tap: nothing fires while the finger is down") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    CHECK(t.pressed());
    CHECK_FALSE(t.tap(cfg()));
    t.move(100.0f, 50.0f, 1.1);
    CHECK_FALSE(t.tap(cfg()));
}

TEST_CASE("tap: a release inside the slop fires") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.move(103.0f, 52.0f, 1.05);
    t.up(103.0f, 52.0f, 1.1);
    CHECK_FALSE(t.pressed());
    CHECK(t.tap(cfg()));
}

TEST_CASE("tap: exactly the slop and just under the long-press time still fire") {
    TapTracker t;
    t.down(0.0f, 0.0f, 2.0);
    t.up(kSlop, 0.0f, 2.0 + kLong - 0.001);
    CHECK(t.tap(cfg()));
}

TEST_CASE("tap: a move past the slop makes it a drag, even when the finger comes back") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.move(100.0f, 50.0f - kSlop - 1.0f, 1.05);
    t.move(100.0f, 50.0f, 1.1);
    t.up(100.0f, 50.0f, 1.12);
    CHECK_FALSE(t.tap(cfg()));
}

TEST_CASE("tap: a swipe that ends on the control does not fire") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.move(100.0f, 20.0f, 1.08);
    t.up(100.0f, 20.0f, 1.1);
    CHECK_FALSE(t.tap(cfg()));
    CHECK(t.travel() == doctest::Approx(30.0f));
}

TEST_CASE("tap: a drag claim cancels it") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.cancel();
    t.up(100.0f, 50.0f, 1.1);
    CHECK(t.cancelled());
    CHECK_FALSE(t.tap(cfg()));
}

TEST_CASE("tap: a second finger cancels it") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.move(100.0f, 50.0f, 1.05);
    t.cancel(); // the second finger landed
    t.up(100.0f, 50.0f, 1.1);
    CHECK_FALSE(t.tap(cfg()));
}

TEST_CASE("tap: held to the long-press time is a hold, and its release is no tap") {
    TapTracker t;
    t.down(100.0f, 50.0f, 1.0);
    t.move(100.0f, 50.0f, 1.0 + kLong - 0.01);
    CHECK_FALSE(t.holding(cfg()));
    t.move(100.0f, 50.0f, 1.0 + kLong);
    CHECK(t.holding(cfg()));
    t.up(100.0f, 50.0f, 1.0 + kLong + 0.02);
    CHECK_FALSE(t.tap(cfg()));
    CHECK_FALSE(t.holding(cfg())); // lifted
}

TEST_CASE("tap: a new press forgets the last one") {
    TapTracker t;
    t.down(0.0f, 0.0f, 1.0);
    t.cancel();
    t.up(40.0f, 0.0f, 3.0);
    t.down(10.0f, 10.0f, 4.0);
    t.up(10.0f, 10.0f, 4.1);
    CHECK(t.tap(cfg()));
    CHECK(t.travel() == doctest::Approx(0.0f));
}

TEST_CASE("tap: a hold repeats once at the long-press time, then every period") {
    const TapRuleConfig c = cfg(0.2);
    TapTracker t;
    t.down(0.0f, 0.0f, 10.0);
    int repeats = 0;
    // Frames of 1/60 s for a second and a half of holding.
    for (int i = 1; i <= 90; ++i) {
        t.move(0.0f, 0.0f, 10.0 + i / 60.0);
        repeats += t.repeats(c);
    }
    // At 0.45 s, 0.65, 0.85, 1.05, 1.25 and 1.45 s.
    CHECK(repeats == 6);
}

TEST_CASE("tap: nothing repeats before the long-press time or without a period") {
    TapTracker t;
    t.down(0.0f, 0.0f, 5.0);
    int early = 0;
    for (int i = 1; i <= 20; ++i) {
        t.move(0.0f, 0.0f, 5.0 + i / 60.0); // up to 0.33 s
        early += t.repeats(cfg(0.2));
    }
    CHECK(early == 0);
    int none = 0;
    for (int i = 21; i <= 90; ++i) {
        t.move(0.0f, 0.0f, 5.0 + i / 60.0);
        none += t.repeats(cfg(0.0));
    }
    CHECK(none == 0);
}

TEST_CASE("tap: a hold that leaves the slop, is cancelled or lifts stops repeating") {
    const TapRuleConfig c = cfg(0.2);
    TapTracker drifted;
    drifted.down(0.0f, 0.0f, 0.0);
    drifted.move(0.0f, 0.0f, 0.5);
    CHECK(drifted.repeats(c) == 1);
    drifted.move(kSlop + 2.0f, 0.0f, 0.8);
    CHECK(drifted.repeats(c) == 0);
    drifted.move(0.0f, 0.0f, 1.2); // back at the start: still a drag
    CHECK(drifted.repeats(c) == 0);

    TapTracker claimed;
    claimed.down(0.0f, 0.0f, 0.0);
    claimed.move(0.0f, 0.0f, 0.5);
    claimed.cancel();
    claimed.move(0.0f, 0.0f, 0.9);
    CHECK(claimed.repeats(c) == 0);

    TapTracker lifted;
    lifted.down(0.0f, 0.0f, 0.0);
    lifted.up(0.0f, 0.0f, 0.9);
    CHECK(lifted.repeats(c) == 0);
}

TEST_CASE("tap: a lag frame that spans two periods reports both repeats") {
    CHECK(hold_repeats(0.4, 0.9, cfg(0.2)) == 3); // 0.45, 0.65, 0.85
    CHECK(hold_repeats(0.46, 0.5, cfg(0.2)) == 0);
}

TEST_CASE("tap: the slop test is the distance, in any direction, and the slop itself is inside") {
    CHECK_FALSE(exceeds_slop(kSlop, 0.0f, kSlop));
    CHECK_FALSE(exceeds_slop(0.0f, -kSlop, kSlop));
    CHECK(exceeds_slop(kSlop, 1.0f, kSlop));
    CHECK_FALSE(exceeds_slop(7.0f, 7.0f, kSlop)); // 9.9 pt
    CHECK(exceeds_slop(8.0f, 8.0f, kSlop));       // 11.3 pt
}

TEST_CASE("tap: the release after a repeat fires no tap") {
    const TapRuleConfig c = cfg(0.2);
    TapTracker t;
    t.down(0.0f, 0.0f, 10.0);
    int repeats = 0;
    for (int i = 1; i <= 36; ++i) { // 0.6 s: past the long-press time, one repeat
        t.move(0.0f, 0.0f, 10.0 + i / 60.0);
        repeats += t.repeats(c);
    }
    CHECK(repeats >= 1);
    t.up(0.0f, 0.0f, 10.61);
    CHECK_FALSE(t.tap(c));
}

TEST_CASE("tap: the slop grows with the unit the screen is drawn at") {
    const TapRuleConfig one = TapRuleConfig::scaled(1.0f);
    const TapRuleConfig two = TapRuleConfig::scaled(2.0f, 0.35);
    CHECK(one.slop == doctest::Approx(kTouchSlopPt));
    CHECK(two.slop == doctest::Approx(2.0f * kTouchSlopPt));
    CHECK(two.repeat_s == doctest::Approx(0.35));
    CHECK(two.long_press_s == doctest::Approx(kLongPressS));
    TapTracker t; // a drift of 15 points: a drag at unit 1, still a tap at unit 2
    t.down(0.0f, 0.0f, 1.0);
    t.up(15.0f, 0.0f, 1.1);
    CHECK_FALSE(t.tap(one));
    CHECK(t.tap(two));
}

TEST_CASE("tap: each press has its own number") {
    TapTracker t;
    CHECK(t.serial() == 0);
    t.down(0.0f, 0.0f, 1.0);
    const std::uint64_t first = t.serial();
    t.up(0.0f, 0.0f, 1.1);
    CHECK(t.serial() == first);
    t.down(0.0f, 0.0f, 2.0);
    CHECK(t.serial() == first + 1);
}

TEST_CASE("tap: the release that opened an overlay does not close it, the next press can") {
    TapTracker t;
    OutsideClose close;
    t.down(300.0f, 20.0f, 1.0); // a press on the opener, outside the card
    close.update(false, t.serial());
    t.up(300.0f, 20.0f, 1.1);   // its release opens the card
    close.update(true, t.serial());
    CHECK_FALSE(close.may_close(t.serial())); // the same release, in the same frame
    close.update(true, t.serial());
    CHECK_FALSE(close.may_close(t.serial())); // and in the frames after it
    t.down(500.0f, 400.0f, 2.0);              // a new press, outside the card
    close.update(true, t.serial());
    CHECK(close.may_close(t.serial()));
}

TEST_CASE("tap: an overlay that closes and opens again waits for a press after the new opening") {
    TapTracker t;
    OutsideClose close;
    t.down(0.0f, 0.0f, 1.0);
    t.up(0.0f, 0.0f, 1.1);
    close.update(true, t.serial());
    t.down(500.0f, 400.0f, 2.0);
    t.up(500.0f, 400.0f, 2.1);
    CHECK(close.may_close(t.serial()));
    close.update(false, t.serial()); // closed
    CHECK_FALSE(close.may_close(t.serial()));
    t.down(0.0f, 0.0f, 3.0);
    t.up(0.0f, 0.0f, 3.1);
    close.update(true, t.serial()); // opened again by that press's release
    CHECK_FALSE(close.may_close(t.serial()));
    t.down(500.0f, 400.0f, 4.0);
    CHECK(close.may_close(t.serial()));
}

TEST_CASE("tap: the slop is a few millimetres of the real screen") {
    // A design point is 1/96 inch at the panel's real size, so the slop in points is the same
    // physical distance on every panel: 2.5 mm.
    const float slop_mm = kTouchSlopPt * (kMmPerInch / 96.0f);
    CHECK(slop_mm >= kTapSlopMm);
    CHECK(slop_mm < kTapSlopMm + 0.3f); // rounded up to a whole point, no further
    CHECK(kTapSlopMm >= 2.0f);
    CHECK(kTapSlopMm <= 4.0f);
    // On the Pi's panel (5.57 pixels to the millimetre, the unit 1.475 pixels to a point) a drift
    // of 2.4 mm is still a tap and one of 3 mm is a drag.
    const float px_per_mm = 5.574f;
    const float u = px_per_mm / kFallbackPxPerMm;
    const TapRuleConfig c = TapRuleConfig::scaled(u);
    TapTracker near_tap;
    near_tap.down(100.0f, 100.0f, 1.0);
    near_tap.up(100.0f + 2.4f * px_per_mm, 100.0f, 1.1);
    CHECK(near_tap.tap(c));
    TapTracker far_drag;
    far_drag.down(100.0f, 100.0f, 1.0);
    far_drag.up(100.0f + 3.0f * px_per_mm, 100.0f, 1.1);
    CHECK_FALSE(far_drag.tap(c));
}

TEST_CASE("tap: a press that slides off its control before lifting is no tap") {
    // The kit's controls are ImGui buttons, which only report a release over the button the press
    // began on; a press that wanders further than the slop on its way off is no tap either, so
    // whichever way it left, it does not fire.
    const TapRuleConfig c = TapRuleConfig::scaled(1.0f);
    TapTracker slid;
    slid.down(100.0f, 100.0f, 1.0);
    slid.move(100.0f + 4.0f * kSlop, 100.0f, 1.05);
    slid.move(100.0f, 100.0f, 1.08); // back where it began before the lift
    slid.up(100.0f, 100.0f, 1.1);
    CHECK_FALSE(slid.tap(c)); // the farthest it got counts
}
