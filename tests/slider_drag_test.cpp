// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/slider_drag.hpp"

#include "etude/touch_gesture.hpp"

#include <doctest/doctest.h>

#include <limits>

using namespace etude;

namespace {

constexpr SliderTrack kTrack{100.0f, 300.0f}; // 200 wide
constexpr float kReach = 22.0f;

} // namespace

TEST_CASE("slider: a mouse press sets the value there and the drag follows the pointer") {
    SliderDrag d;
    const auto v = d.press(SliderPointer::Mouse, 200.0f, 50.0f, kTrack, 0.1f, kReach);
    REQUIRE(v.has_value());
    CHECK(*v == doctest::Approx(0.5f));
    CHECK(d.dragging());
    const auto m = d.move(250.0f, 80.0f, kTrack, kTouchSlopPt); // vertical drift does not matter
    REQUIRE(m.has_value());
    CHECK(*m == doctest::Approx(0.75f));
    CHECK_FALSE(d.release(250.0f, kTrack).has_value());
}

TEST_CASE("slider: a finger on the thumb changes nothing until it moves, then drags by delta") {
    SliderDrag d;
    // Thumb at 0.5 -> x 200. The finger lands 10 off its centre.
    CHECK_FALSE(d.press(SliderPointer::Finger, 210.0f, 50.0f, kTrack, 0.5f, kReach).has_value());
    CHECK(d.mode() == SliderMode::Undecided);
    CHECK_FALSE(d.move(212.0f, 51.0f, kTrack, kTouchSlopPt).has_value()); // inside the slop
    CHECK(d.mode() == SliderMode::Undecided);
    const auto v = d.move(230.0f, 52.0f, kTrack, kTouchSlopPt);
    REQUIRE(v.has_value());
    CHECK(d.dragging());
    CHECK(*v == doctest::Approx(0.6f)); // moved 20 of 200 from the value it started at
    const auto back = d.move(190.0f, 60.0f, kTrack, kTouchSlopPt);
    REQUIRE(back.has_value());
    CHECK(*back == doctest::Approx(0.4f));
}

TEST_CASE("slider: a tap on the track moves the thumb to the tap, a tap on the thumb does not") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 260.0f, 50.0f, kTrack, 0.1f, kReach);
    CHECK_FALSE(d.move(263.0f, 52.0f, kTrack, kTouchSlopPt).has_value()); // within the slop
    const auto tap = d.release(263.0f, kTrack);
    REQUIRE(tap.has_value());
    CHECK(*tap == doctest::Approx(0.815f));
    CHECK(d.mode() == SliderMode::Idle);

    d.press(SliderPointer::Finger, 205.0f, 50.0f, kTrack, 0.5f, kReach);
    CHECK_FALSE(d.release(205.0f, kTrack).has_value());
}

TEST_CASE(
    "slider: a drag that starts on the track changes the value only after the slop, by delta") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 150.0f, 50.0f, kTrack, 0.9f, kReach); // thumb far away
    CHECK_FALSE(d.move(157.0f, 50.0f, kTrack, kTouchSlopPt).has_value());
    CHECK(d.mode() == SliderMode::Undecided);
    const auto v = d.move(170.0f, 51.0f, kTrack, kTouchSlopPt);
    REQUIRE(v.has_value());
    CHECK(*v == doctest::Approx(1.0f)); // 0.9 plus 20/200, clamped
    const auto w = d.move(130.0f, 51.0f, kTrack, kTouchSlopPt);
    REQUIRE(w.has_value());
    CHECK(*w == doctest::Approx(0.8f)); // 0.9 minus 20/200
}

TEST_CASE("slider: the drag clamps to the track") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 150.0f, 50.0f, kTrack, 0.9f, kReach);
    const auto v = d.move(260.0f, 50.0f, kTrack, kTouchSlopPt);
    REQUIRE(v.has_value());
    CHECK(*v == doctest::Approx(1.0f));
    const auto lo = d.move(-500.0f, 50.0f, kTrack, kTouchSlopPt);
    REQUIRE(lo.has_value());
    CHECK(*lo == doctest::Approx(0.0f));
}

TEST_CASE("slider: a mostly vertical drag goes to the scroll and never changes the value") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 150.0f, 50.0f, kTrack, 0.3f, kReach);
    CHECK_FALSE(d.move(153.0f, 70.0f, kTrack, kTouchSlopPt).has_value());
    CHECK(d.scrolling());
    // The touch stays the scroll's even if it later wanders sideways.
    CHECK_FALSE(d.move(250.0f, 75.0f, kTrack, kTouchSlopPt).has_value());
    CHECK(d.scrolling());
    CHECK_FALSE(d.release(250.0f, kTrack).has_value());
}

TEST_CASE("slider: a mostly vertical drag that began on the thumb scrolls too") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 200.0f, 50.0f, kTrack, 0.5f, kReach);
    CHECK_FALSE(d.move(204.0f, 30.0f, kTrack, kTouchSlopPt).has_value());
    CHECK(d.scrolling());
}

TEST_CASE("slider: once dragging, vertical movement does not turn it into a scroll") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 150.0f, 50.0f, kTrack, 0.3f, kReach);
    REQUIRE(d.move(180.0f, 52.0f, kTrack, kTouchSlopPt).has_value());
    const auto v = d.move(185.0f, 200.0f, kTrack, kTouchSlopPt);
    REQUIRE(v.has_value());
    CHECK(d.dragging());
}

TEST_CASE("slider: equal horizontal and vertical movement is a drag") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 150.0f, 50.0f, kTrack, 0.3f, kReach);
    CHECK(d.move(170.0f, 70.0f, kTrack, kTouchSlopPt).has_value());
    CHECK(d.dragging());
}

TEST_CASE("slider: a cancelled touch changes nothing, even a tap on the track") {
    SliderDrag d;
    d.press(SliderPointer::Finger, 260.0f, 50.0f, kTrack, 0.1f, kReach);
    CHECK_FALSE(d.release(260.0f, kTrack, true).has_value());
    CHECK(d.mode() == SliderMode::Idle);
}

TEST_CASE("slider: a track with no length and non-finite input are harmless") {
    SliderDrag d;
    const SliderTrack flat{10.0f, 10.0f};
    CHECK(slider_value_at(flat, 50.0f) == 0.0f);
    d.press(SliderPointer::Finger, 10.0f, 0.0f, flat, 0.5f, kReach);
    CHECK_FALSE(d.move(80.0f, 0.0f, flat, kTouchSlopPt).has_value());
    CHECK(slider_value_at(kTrack, std::numeric_limits<float>::infinity()) == 0.0f);
}
