// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/square_drag.hpp"

#include "etude/touch_gesture.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace etude;

namespace {

constexpr SquareArea kArea{100.0f, 50.0f, 300.0f, 150.0f}; // 200 by 100
constexpr float kReach = 22.0f;

} // namespace

TEST_CASE("square: a point maps to 0..1 on each axis and clamps at the edges") {
    CHECK(square_pos_at(kArea, 200.0f, 100.0f) == SquarePos{0.5f, 0.5f});
    CHECK(square_pos_at(kArea, 100.0f, 50.0f) == SquarePos{0.0f, 0.0f});
    CHECK(square_pos_at(kArea, 300.0f, 150.0f) == SquarePos{1.0f, 1.0f});
    CHECK(square_pos_at(kArea, -50.0f, 999.0f) == SquarePos{0.0f, 1.0f});
    CHECK(square_pos_at(kArea, std::numeric_limits<float>::quiet_NaN(), 100.0f) ==
          SquarePos{0.0f, 0.5f});
    CHECK(square_pos_at({10.0f, 10.0f, 10.0f, 10.0f}, 10.0f, 10.0f) == SquarePos{0.0f, 0.0f});
}

TEST_CASE("square: a mouse press sets the value there and the drag follows the pointer") {
    SquareDrag d;
    const auto v = d.press(SliderPointer::Mouse, 200.0f, 75.0f, kArea, {0.9f, 0.9f}, kReach);
    REQUIRE(v.has_value());
    CHECK(*v == SquarePos{0.5f, 0.25f});
    CHECK(d.dragging());
    const auto m = d.move(250.0f, 125.0f, kArea, kTouchSlopPt);
    REQUIRE(m.has_value());
    CHECK(*m == SquarePos{0.75f, 0.75f});
    CHECK_FALSE(d.release(250.0f, 125.0f, kArea).has_value());
    CHECK_FALSE(d.dragging());
}

TEST_CASE("square: a finger on the thumb changes nothing until it moves, then drags by delta") {
    SquareDrag d;
    // Thumb at (0.5, 0.5) -> (200, 100). The finger lands 10 off its centre.
    CHECK_FALSE(
        d.press(SliderPointer::Finger, 208.0f, 106.0f, kArea, {0.5f, 0.5f}, kReach).has_value());
    CHECK(d.mode() == SliderMode::Undecided);
    CHECK_FALSE(d.move(210.0f, 107.0f, kArea, kTouchSlopPt).has_value()); // inside the slop
    const auto v = d.move(228.0f, 98.0f, kArea, kTouchSlopPt);
    REQUIRE(v.has_value());
    CHECK(d.dragging());
    CHECK(v->x == doctest::Approx(0.6f)); // 20 of 200 right
    CHECK(v->y == doctest::Approx(0.42f)); // 8 of 100 up
    // A move any way, vertical included, keeps dragging: nothing is handed to a scroll.
    const auto up = d.move(228.0f, 40.0f, kArea, kTouchSlopPt);
    REQUIRE(up.has_value());
    CHECK(up->y == doctest::Approx(0.0f));
    CHECK(d.dragging());
    // The value clamps at the square's edges.
    const auto far = d.move(900.0f, 900.0f, kArea, kTouchSlopPt);
    REQUIRE(far.has_value());
    CHECK(*far == SquarePos{1.0f, 1.0f});
    CHECK_FALSE(d.release(900.0f, 900.0f, kArea).has_value());
}

TEST_CASE("square: a tap off the thumb moves it to the tap, a tap on the thumb does not") {
    SquareDrag d;
    d.press(SliderPointer::Finger, 260.0f, 130.0f, kArea, {0.1f, 0.1f}, kReach);
    CHECK_FALSE(d.move(262.0f, 131.0f, kArea, kTouchSlopPt).has_value()); // within the slop
    const auto tap = d.release(262.0f, 131.0f, kArea);
    REQUIRE(tap.has_value());
    CHECK(tap->x == doctest::Approx(0.81f));
    CHECK(tap->y == doctest::Approx(0.81f));

    SquareDrag on;
    on.press(SliderPointer::Finger, 200.0f, 100.0f, kArea, {0.5f, 0.5f}, kReach);
    CHECK_FALSE(on.release(200.0f, 100.0f, kArea).has_value());
}

TEST_CASE("square: a touch taken away changes nothing, and the slop is the touch slop") {
    SquareDrag d;
    d.press(SliderPointer::Finger, 260.0f, 130.0f, kArea, {0.1f, 0.1f}, kReach);
    CHECK_FALSE(d.release(260.0f, 130.0f, kArea, true).has_value());
    CHECK_FALSE(d.dragging());
    // Just inside the slop in any direction is still a tap; just past it is a drag.
    SquareDrag t;
    t.press(SliderPointer::Finger, 150.0f, 60.0f, kArea, {0.9f, 0.9f}, kReach);
    CHECK_FALSE(t.move(150.0f + kTouchSlopPt * 0.9f, 60.0f, kArea, kTouchSlopPt).has_value());
    CHECK(t.mode() == SliderMode::Undecided);
    CHECK(t.move(150.0f, 60.0f + kTouchSlopPt * 1.5f, kArea, kTouchSlopPt).has_value());
    CHECK(t.dragging());
}
