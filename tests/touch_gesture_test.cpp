// SPDX-License-Identifier: GPL-3.0-or-later
// touch_gesture: the first topics (split from touch_gesture_test.cpp).
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

TEST_CASE("a touch that lifts where it began is a tap, with a press first") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.up(1, 100, 50, 0.08);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::Tap});
    CHECK(e[0].x == 100.0f);
    CHECK(e[1].y == 50.0f);
    CHECK_FALSE(g.touching());
}

TEST_CASE("a little drift never turns a tap into a drag") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.move(1, 105, 54, 0.02); // 6.4 points
    g.move(1, 94, 47, 0.04);  // 6.7 points the other way
    g.up(1, 94, 47, 0.08);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Press, Kind::Tap});
}

TEST_CASE("past the threshold it is a drag: start where it began, then the whole move") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.move(1, 112, 50, 0.02); // 12 points: over 9
    auto e = drain(g);
    REQUIRE(kinds(e) == std::vector<Kind>{Kind::Press, Kind::DragStart, Kind::DragMove});
    CHECK(e[1].x == 100.0f);
    CHECK(e[2].dx == doctest::Approx(12.0f));
    CHECK(e[2].total_dx == doctest::Approx(12.0f));
    g.move(1, 130, 56, 0.04);
    e = drain(g);
    REQUIRE(e.size() == 1);
    CHECK(e[0].dx == doctest::Approx(18.0f));
    CHECK(e[0].dy == doctest::Approx(6.0f));
    CHECK(e[0].total_dx == doctest::Approx(30.0f));
    // Once dragging, coming back near the start is still a drag, and the lift is no tap.
    g.move(1, 101, 50, 0.06);
    g.move(1, 101, 50, 0.20);
    g.up(1, 101, 50, 0.30);
    e = drain(g);
    CHECK(count_of(e, Kind::Tap) == 0);
    CHECK(e.back().kind == Kind::DragEnd);
}

TEST_CASE("the threshold is the configured one") {
    GestureRecognizer::Config c;
    c.drag_threshold = 20.0f;
    GestureRecognizer g(c);
    g.down(1, 0, 0, 0.0);
    g.move(1, 15, 0, 0.02);
    CHECK(count_of(drain(g), Kind::DragStart) == 0);
    g.move(1, 25, 0, 0.04);
    CHECK(count_of(drain(g), Kind::DragStart) == 1);
}

TEST_CASE("two taps close in time and place make a double tap; a third is a tap again") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.up(1, 100, 50, 0.05);
    g.down(1, 108, 52, 0.20);
    g.up(1, 108, 52, 0.25);
    auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::Tap, Kind::Press, Kind::DoubleTap});
    g.down(1, 108, 52, 0.40);
    g.up(1, 108, 52, 0.44);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Press, Kind::Tap});
}

TEST_CASE("a second tap too late or too far away is not a double tap") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.up(1, 100, 50, 0.05);
    g.down(1, 100, 50, 0.45); // 0.4 s after: past 0.3
    g.up(1, 100, 50, 0.50);
    CHECK(count_of(drain(g), Kind::DoubleTap) == 0);
    g.down(1, 200, 50, 0.60); // 100 points away
    g.up(1, 200, 50, 0.65);
    CHECK(count_of(drain(g), Kind::DoubleTap) == 0);
}

TEST_CASE("held still for 450 ms is one long press and the lift is not a tap") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.update(0.20);
    CHECK(g.long_press_progress(0.20) == doctest::Approx(0.2 / 0.45).epsilon(1e-4));
    CHECK(count_of(drain(g), Kind::LongPress) == 0);
    g.update(0.44);
    CHECK(count_of(drain(g), Kind::LongPress) == 0);
    g.update(0.46);
    auto e = drain(g);
    REQUIRE(count_of(e, Kind::LongPress) == 1);
    CHECK(g.long_press_progress(0.5) == 0.0f);
    g.update(0.9);
    g.move(1, 160, 50, 0.95); // moving after it fired does nothing more
    g.up(1, 160, 50, 1.0);
    e = drain(g);
    CHECK(e.empty());
}

TEST_CASE("with drag after long press, a held finger that moves reports where it is, then lifts") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 100, 50, 0.0);
    g.move(1, 104, 50, 0.1); // inside the slop: still a press
    g.update(0.5);
    REQUIRE(count_of(drain(g), Kind::LongPress) == 1);
    g.move(1, 160, 70, 0.6);
    g.move(1, 220, 90, 0.7);
    g.up(1, 220, 90, 0.8);
    const auto e = drain(g);
    REQUIRE(e.size() == 3);
    CHECK(e[0].kind == Kind::HeldMove);
    CHECK(e[0].x == 160.0f);
    CHECK(e[0].total_dx == 60.0f);
    CHECK(e[1].kind == Kind::HeldMove);
    CHECK(e[1].y == 90.0f);
    CHECK(e[2].kind == Kind::HeldEnd);
    CHECK(e[2].x == 220.0f);
    CHECK(count_of(e, Kind::Tap) == 0);
}

TEST_CASE("with drag after long press, a right click holds at once and drags the same way") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(0, 30, 40, 1.0);
    g.long_press_now(30, 40, 1.0);
    g.move(0, 90, 40, 1.1);
    g.up(0, 90, 40, 1.2);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::LongPress, Kind::HeldMove, Kind::HeldEnd});
}

TEST_CASE("a held finger that never leaves the slop lifts with a release, not a held move or end") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    // A still long press.
    g.down(1, 100, 50, 0.0);
    g.update(0.5);
    g.up(1, 100, 50, 0.9);
    auto e = drain(g);
    CHECK(count_of(e, Kind::LongPress) == 1);
    CHECK(count_of(e, Kind::HeldMove) == 0);
    CHECK(count_of(e, Kind::HeldEnd) == 0);
    REQUIRE(count_of(e, Kind::HeldRelease) == 1);
    CHECK(e.back().kind == Kind::HeldRelease);
    CHECK(e.back().x == 100.0f);
    CHECK(e.back().y == 50.0f);
    // A tremor inside the slop after the long press.
    g.down(1, 100, 50, 2.0);
    g.update(2.5);
    g.move(1, 103, 52, 2.6);
    g.move(1, 98, 49, 2.7);
    g.up(1, 98, 49, 2.8);
    e = drain(g);
    CHECK(count_of(e, Kind::HeldMove) == 0);
    CHECK(count_of(e, Kind::HeldEnd) == 0);
    CHECK(count_of(e, Kind::HeldRelease) == 1);
    // A right click that does not move.
    g.down(0, 30, 40, 4.0);
    g.long_press_now(30, 40, 4.0);
    g.up(0, 30, 40, 4.2);
    e = drain(g);
    CHECK(kinds(e) ==
          std::vector<Kind>{Kind::Press, Kind::LongPress, Kind::HeldRelease});
}

TEST_CASE("a hold that moved ends with a held end, never also a release") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 100, 50, 0.0);
    g.update(0.5);
    g.move(1, 140, 50, 0.6);
    g.up(1, 140, 50, 0.7);
    const auto e = drain(g);
    CHECK(kinds(e) ==
          std::vector<Kind>{Kind::Press, Kind::LongPress, Kind::HeldMove, Kind::HeldEnd});
}

TEST_CASE("without drag after long press a held finger lifts with no held event at all") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.update(0.5);
    g.up(1, 100, 50, 0.9);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Press, Kind::LongPress});
}

TEST_CASE("a hold on a handle then a drag moves it: long press, held moves from the first move past the slop, held end") {
    // The section editor takes a handle on the long press and moves it with the held moves; the
    // hold never needs a drag start, so the sequence is the recogniser's whole contract.
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 300, 90, 0.0);
    g.update(0.46);
    g.move(1, 304, 90, 0.5); // inside the slop: the handle has not moved yet
    g.move(1, 330, 91, 0.6);
    g.move(1, 360, 91, 0.7);
    g.up(1, 360, 91, 0.8);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::LongPress, Kind::HeldMove,
                                        Kind::HeldMove, Kind::HeldEnd});
    CHECK(count_of(e, Kind::DragStart) == 0);
    CHECK(count_of(e, Kind::Tap) == 0);
    CHECK(e[2].total_dx == 30.0f);
    CHECK(e[3].total_dx == 60.0f);
}

TEST_CASE("held moves start with the move that leaves the slop") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 100, 50, 0.0);
    g.update(0.5);
    g.move(1, 105, 50, 0.6);  // inside the 9 point slop: nothing
    g.move(1, 112, 50, 0.7);  // past it: the first HeldMove
    g.move(1, 104, 50, 0.8);  // back inside: still engaged, still a HeldMove
    g.up(1, 104, 50, 0.9);
    const auto e = drain(g);
    REQUIRE(count_of(e, Kind::HeldMove) == 2);
    const auto first = std::find_if(e.begin(), e.end(), [](const auto& ev) { return ev.kind == Kind::HeldMove; });
    CHECK(first->x == 112.0f);
    CHECK(count_of(e, Kind::HeldEnd) == 1);
}

TEST_CASE("moving past the slop before the long press is a drag, never a held drag") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 100, 50, 0.0);
    g.move(1, 140, 50, 0.2);
    g.update(0.6);
    g.move(1, 180, 50, 0.7);
    g.up(1, 180, 50, 0.8);
    const auto e = drain(g);
    CHECK(count_of(e, Kind::LongPress) == 0);
    CHECK(count_of(e, Kind::HeldMove) == 0);
    CHECK(count_of(e, Kind::HeldEnd) == 0);
    CHECK(count_of(e, Kind::DragStart) == 1);
}

TEST_CASE("moving before 450 ms cancels the long press") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.move(1, 140, 50, 0.2);
    g.update(0.6);
    CHECK(count_of(drain(g), Kind::LongPress) == 0);
    CHECK(g.long_press_progress(0.6) == 0.0f);
}

TEST_CASE("a long press can be asked for at once, as a right click does") {
    GestureRecognizer g;
    g.long_press_now(30, 40, 1.0);
    auto e = drain(g);
    REQUIRE(e.size() == 1);
    CHECK(e[0].kind == Kind::LongPress);
    CHECK(e[0].x == 30.0f);
    // During a press: once.
    g.down(1, 10, 10, 2.0);
    g.long_press_now(10, 10, 2.1);
    g.update(3.0);
    CHECK(count_of(drain(g), Kind::LongPress) == 1);
}

TEST_CASE("two fingers pinch: the factor is the change of distance, the centre follows") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.down(2, 200, 100, 0.01);
    auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::Cancel, Kind::PinchStart});
    CHECK(e[2].x == doctest::Approx(150.0f));
    CHECK(g.pinching());
    g.move(2, 300, 100, 0.05); // 100 apart -> 200
    e = drain(g);
    REQUIRE(e.size() == 1);
    CHECK(e[0].kind == Kind::PinchMove);
    CHECK(e[0].factor == doctest::Approx(2.0));
    CHECK(e[0].x == doctest::Approx(200.0f));
    CHECK(e[0].dx == doctest::Approx(50.0f));
    g.move(1, 50, 100, 0.09); // 200 -> 250, the centre moves back
    e = drain(g);
    CHECK(e[0].factor == doctest::Approx(1.25));
    CHECK(e[0].dx == doctest::Approx(-25.0f));
}

TEST_CASE("a second finger during a drag ends the drag and starts a pinch") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.move(1, 150, 100, 0.02);
    drain(g);
    g.down(2, 300, 100, 0.03);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::DragEnd, Kind::PinchStart});
    CHECK(e[0].vx == 0.0f);
}

TEST_CASE("lifting one finger ends the pinch; the other does nothing until it lifts") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.down(2, 200, 100, 0.01);
    g.move(2, 220, 100, 0.2);
    drain(g);
    g.up(1, 100, 100, 0.5);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::PinchEnd});
    g.move(2, 400, 100, 0.6);
    g.update(1.0);
    g.up(2, 400, 100, 1.1);
    CHECK(drain(g).empty());
    // Everything is up: the next touch is a fresh press.
    g.down(3, 10, 10, 2.0);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Press});
}

TEST_CASE("a third finger is ignored") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.down(2, 200, 100, 0.01);
    drain(g);
    g.down(3, 500, 500, 0.02);
    g.move(3, 600, 600, 0.03);
    CHECK(drain(g).empty());
    CHECK(g.fingers() == 2);
}

TEST_CASE("a fast drag keeps going after the lift, slowing like an ease-out") {
    GestureRecognizer g;
    g.down(1, 0, 0, 0.0);
    // 1000 points per second: 10 points every 10 ms.
    for (int i = 1; i <= 10; ++i) {
        g.move(1, 10.0f * static_cast<float>(i), 0, 0.01 * i);
    }
    g.up(1, 100, 0, 0.1);
    auto e = drain(g);
    REQUIRE(e.back().kind == Kind::DragEnd);
    CHECK(e.back().vx == doctest::Approx(1000.0f).epsilon(0.02));
    CHECK(g.flinging());
    float total = 0.0f;
    float last_step = 1e9f;
    double t = 0.1;
    int steps = 0;
    bool ended = false;
    while (!ended && steps < 2000) {
        t += 0.016;
        g.update(t);
        for (const auto& ev : drain(g)) {
            if (ev.kind == Kind::Fling) {
                total += ev.dx;
                CHECK(ev.dx <= last_step + 1e-3f); // each step moves less than the one before
                last_step = ev.dx;
            } else if (ev.kind == Kind::FlingEnd) {
                ended = true;
            }
        }
        ++steps;
    }
    CHECK(ended);
    CHECK_FALSE(g.flinging());
    // The speed halves every 0.22 s: the distance is v * half / ln 2, less what is left over.
    const double expected = 1000.0 * 0.22 / std::log(2.0);
    CHECK(total > expected * 0.9);
    CHECK(total < expected * 1.01);
}

TEST_CASE("a new touch stops the fling at once") {
    GestureRecognizer g;
    g.down(1, 0, 0, 0.0);
    for (int i = 1; i <= 10; ++i) {
        g.move(1, 10.0f * static_cast<float>(i), 0, 0.01 * i);
    }
    g.up(1, 100, 0, 0.1);
    g.update(0.2);
    drain(g);
    REQUIRE(g.flinging());
    g.down(2, 50, 50, 0.21);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::FlingEnd, Kind::Press});
    g.update(0.5);
    CHECK(drain(g).empty());
}

TEST_CASE("a slow drag, or one that stopped before the lift, does not fling") {
    GestureRecognizer g;
    g.down(1, 0, 0, 0.0);
    for (int i = 1; i <= 10; ++i) {
        g.move(1, 1.0f * static_cast<float>(i) + 10.0f, 0, 0.1 * i); // 10 points a second
    }
    g.up(1, 20, 0, 1.0);
    CHECK_FALSE(g.flinging());
    g.down(1, 0, 0, 2.0);
    for (int i = 1; i <= 10; ++i) {
        g.move(1, 10.0f * static_cast<float>(i), 0, 2.0 + 0.01 * i);
    }
    g.up(1, 100, 0, 2.5); // held still for 0.4 s, then lifted
    drain(g);
    CHECK_FALSE(g.flinging());
}

TEST_CASE("a fast pinch coasts: the zoom keeps going and settles") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.down(2, 200, 100, 0.0);
    // The fingers spread by 4% every 10 ms: a rate of about 4 e-folds a second.
    float gap = 100.0f;
    double t = 0.0;
    for (int i = 0; i < 10; ++i) {
        t += 0.01;
        gap *= 1.04f;
        g.move(2, 100.0f + gap, 100, t);
    }
    g.up(2, 100.0f + gap, 100, t + 0.005);
    drain(g);
    g.up(1, 100, 100, t + 0.01);
    CHECK(g.flinging());
    double product = 1.0;
    bool ended = false;
    double now = t + 0.01;
    for (int i = 0; i < 2000 && !ended; ++i) {
        now += 0.016;
        g.update(now);
        for (const auto& ev : drain(g)) {
            if (ev.kind == Kind::ZoomFling) {
                CHECK(ev.factor > 1.0);
                product *= ev.factor;
            } else if (ev.kind == Kind::FlingEnd) {
                ended = true;
            }
        }
    }
    CHECK(ended);
    CHECK(product > 1.2);
    CHECK(product < 5.0);
}

TEST_CASE("a pinch that stopped before the lift does not coast") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    g.down(2, 200, 100, 0.0);
    g.move(2, 260, 100, 0.01);
    g.up(2, 260, 100, 0.5);
    g.up(1, 100, 100, 0.51);
    drain(g);
    CHECK_FALSE(g.flinging());
}

TEST_CASE("cancel drops the gesture and any momentum") {
    GestureRecognizer g;
    g.down(1, 100, 100, 0.0);
    drain(g);
    g.cancel();
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Cancel});
    CHECK_FALSE(g.touching());
    g.up(1, 100, 100, 0.1); // a stray lift of a forgotten finger
    CHECK(drain(g).empty());
}

TEST_CASE("the same timestamped input always gives the same events") {
    const auto run = [] {
        GestureRecognizer g;
        g.down(1, 0, 0, 0.0);
        g.move(1, 30, 5, 0.03);
        g.move(1, 80, 9, 0.06);
        g.up(1, 120, 12, 0.09);
        for (int i = 1; i < 40; ++i) {
            g.update(0.09 + 0.016 * i);
        }
        return drain(g);
    };
    const auto a = run();
    const auto b = run();
    REQUIRE(a.size() == b.size());
    for (std::size_t i = 0; i < a.size(); ++i) {
        CHECK(a[i].kind == b[i].kind);
        CHECK(a[i].dx == b[i].dx);
    }
}

TEST_CASE("the recogniser starts from the shared touch constants") {
    const GestureRecognizer g;
    CHECK(g.config().drag_threshold == kTouchSlopPt);
    CHECK(g.config().long_press_s == kLongPressS);
    CHECK(kTouchSlopPt == pt_from_mm_up(kTapSlopMm)); // 2.5 mm of screen, to a whole point
    CHECK(kLongPressS == doctest::Approx(0.45));
    CHECK(kIgnoreMouseAfterFingerS == doctest::Approx(0.6));
}

TEST_CASE("set_config changes what the recogniser takes for a drag and a long press") {
    GestureRecognizer g;
    GestureRecognizer::Config c = g.config();
    c.drag_threshold = 30.0f;
    c.long_press_s = 1.0;
    g.set_config(c);
    CHECK(g.config().drag_threshold == 30.0f);
    CHECK(g.config().long_press_s == doctest::Approx(1.0));
    // 20 points is under the new threshold: still a tap, not a drag.
    g.down(1, 100, 50, 0.0);
    g.move(1, 120, 50, 0.05);
    g.up(1, 120, 50, 0.10);
    const auto e = drain(g);
    CHECK(count_of(e, Kind::DragStart) == 0);
    CHECK(count_of(e, Kind::Tap) == 1);
    // A hold of 0.6 s is not yet a long press; 1.0 s is.
    g.down(1, 100, 50, 1.0);
    g.update(1.6);
    CHECK(count_of(drain(g), Kind::LongPress) == 0);
    g.update(2.0);
    CHECK(count_of(drain(g), Kind::LongPress) == 1);
    g.up(1, 100, 50, 2.1);
    // A recogniser made with a config keeps it.
    const GestureRecognizer h(c);
    CHECK(h.config().drag_threshold == 30.0f);
}

TEST_CASE(
    "with the long press off, holding still never becomes one and a later drag still starts") {
    GestureRecognizer::Config config;
    config.long_press = false;
    GestureRecognizer g(config);
    g.down(1, 100, 100, 0.0);
    g.update(5.0);
    CHECK(g.long_press_progress(5.0) == 0.0f);
    g.move(1, 100, 140, 5.1);
    const auto events = drain(g);
    CHECK(count_of(events, Kind::LongPress) == 0);
    CHECK(count_of(events, Kind::DragStart) == 1);
}
