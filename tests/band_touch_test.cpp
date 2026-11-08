// SPDX-License-Identifier: GPL-3.0-or-later
// The section band's track: what a tap, a long press, a drag and a cancel on it become, one test
// per action (core band_touch.hpp).
#include "etude/band_touch.hpp"

#include <doctest/doctest.h>

using namespace etude;

namespace {

constexpr float kSlop = kTouchSlopPt; // at a unit of 1
constexpr float kY = 80.0f;           // the band's middle, where every press here lands

OverviewHit segment(SectionId id) {
    OverviewHit h;
    h.kind = OverviewHit::Kind::Segment;
    h.section = id;
    return h;
}

OverviewHit marker(std::size_t m) {
    OverviewHit h;
    h.kind = OverviewHit::Kind::Marker;
    h.marker = m;
    return h;
}

} // namespace

TEST_CASE("band: a tap on a section loops it") {
    BandTouch t;
    t.press(segment(4), 100.0f, kY, 10.0);
    CHECK(t.hold(100.0f + kSlop, kY, 10.1, kSlop) == BandTouch::Hold::None); // exactly the slop: a tap
    CHECK(t.lift(kNoSection) == BandTouch::Lift::Loop);
    CHECK(t.section == 4);
    CHECK_FALSE(t.active());
}

TEST_CASE("band: a long press on a section opens its menu, once, and the lift does nothing") {
    BandTouch t;
    t.press(segment(2), 100.0f, kY, 10.0);
    CHECK(t.hold(101.0f, kY, 10.0 + kLongPressS - 0.01, kSlop) == BandTouch::Hold::None);
    CHECK(t.long_press_progress(10.0 + kLongPressS * 0.5) == doctest::Approx(0.5f));
    CHECK(t.hold(101.0f, kY, 10.0 + kLongPressS, kSlop) == BandTouch::Hold::Menu);
    CHECK(t.hold(101.0f, kY, 10.0 + kLongPressS + 0.2, kSlop) == BandTouch::Hold::None);
    CHECK(t.lift(kNoSection) == BandTouch::Lift::None);
}

TEST_CASE("band: a tap off any section seeks, a long press there opens the lane") {
    BandTouch tap;
    tap.press({}, 250.0f, kY, 1.0);
    CHECK(tap.lift(kNoSection) == BandTouch::Lift::Seek);
    CHECK(tap.seek_x == 250.0f);

    BandTouch hold;
    hold.press({}, 250.0f, kY, 1.0);
    CHECK(hold.hold(250.0f, kY, 1.0 + kLongPressS, kSlop) == BandTouch::Hold::Lane);
    CHECK(hold.lift(kNoSection) == BandTouch::Lift::None);
}

TEST_CASE("band: a drag past the slop scrubs and never loops or opens anything") {
    BandTouch t;
    t.press(segment(1), 100.0f, kY, 0.0);
    CHECK(t.hold(100.0f + kSlop + 0.5f, kY, 0.05, kSlop) == BandTouch::Hold::Scrub);
    // Held still long after: still a scrub, never a long press.
    CHECK(t.hold(110.0f, kY, 5.0, kSlop) == BandTouch::Hold::Scrub);
    CHECK(t.x == 110.0f);
    CHECK(t.lift(kNoSection) == BandTouch::Lift::None);
}

TEST_CASE("band: a drag that began on a cut moves it, committed on the lift") {
    BandTouch t;
    t.press(marker(1), 300.0f, kY, 0.0);
    CHECK(t.hold(300.0f + kSlop, kY, 0.1, kSlop) == BandTouch::Hold::None); // within the slop
    CHECK(t.hold(300.0f + kSlop + 1.0f, kY, 0.2, kSlop) == BandTouch::Hold::MoveCut);
    CHECK(t.hold(300.0f, kY, 0.3, kSlop) ==
          BandTouch::Hold::MoveCut); // back over its start: still moving
    CHECK(t.lift(7) == BandTouch::Lift::MoveCut);
    CHECK(t.hit.marker == 1);
}

TEST_CASE("band: a tap on a cut's grab zone loops the section under the finger; a long press there "
          "does nothing") {
    BandTouch tap;
    tap.press(marker(0), 300.0f, kY, 0.0);
    CHECK(tap.lift(5) == BandTouch::Lift::Loop);
    CHECK(tap.section == 5);

    BandTouch hold;
    hold.press(marker(0), 300.0f, kY, 0.0);
    CHECK(hold.hold(300.0f, kY, kLongPressS * 2.0, kSlop) == BandTouch::Hold::None);
    CHECK(hold.lift(kNoSection) == BandTouch::Lift::None);
}

TEST_CASE("band: a cancelled touch (a bar swipe, a second finger) does nothing") {
    for (const OverviewHit& on : {segment(3), marker(0), OverviewHit{}}) {
        BandTouch t;
        t.press(on, 100.0f, kY, 0.0);
        t.cancel();
        CHECK(t.hold(200.0f, kY, 1.0, kSlop) == BandTouch::Hold::None);
        CHECK(t.hold(100.0f, kY, 2.0, kSlop) == BandTouch::Hold::None);
        CHECK(t.lift(3) == BandTouch::Lift::None);
        CHECK(t.long_press_progress(0.2) == 0.0f);
    }
}

TEST_CASE("band: the loop toggle ends a loop, else loops the section at the playhead") {
    CHECK(loop_toggle_tap(true, 4) == LoopTap::EndLoop);
    CHECK(loop_toggle_tap(true, kNoSection) == LoopTap::EndLoop); // a loop set at the keys
    CHECK(loop_toggle_tap(false, 4) == LoopTap::LoopSection);
    CHECK(loop_toggle_tap(false, kNoSection) == LoopTap::NoSection);
}

TEST_CASE("band: a downward swipe that drifts sideways past the slop never scrubs or moves a cut") {
    // Down 40, right 12: vertical as it leaves the slop. Nothing scrubs, nothing moves, no long
    // press follows, and the lift does nothing.
    for (const OverviewHit& on : {segment(1), marker(0), OverviewHit{}}) {
        BandTouch t;
        t.press(on, 300.0f, kY, 0.0);
        CHECK(t.hold(302.0f, kY + 6.0f, 0.02, kSlop) == BandTouch::Hold::None); // in the slop
        CHECK(t.hold(312.0f, kY + 40.0f, 0.05, kSlop) == BandTouch::Hold::None);
        CHECK(t.hold(360.0f, kY + 45.0f, 0.10, kSlop) == BandTouch::Hold::None);
        CHECK(t.hold(360.0f, kY + 45.0f, 2.0, kSlop) == BandTouch::Hold::None);
        CHECK(t.lift(3) == BandTouch::Lift::None);
    }
}

TEST_CASE("band: the mirror, right 40 and down 12, scrubs; begun on a cut it moves the cut") {
    BandTouch scrub;
    scrub.press(segment(1), 300.0f, kY, 0.0);
    CHECK(scrub.hold(340.0f, kY + 12.0f, 0.05, kSlop) == BandTouch::Hold::Scrub);
    CHECK(scrub.hold(380.0f, kY + 60.0f, 0.10, kSlop) == BandTouch::Hold::Scrub); // decided once

    BandTouch cut;
    cut.press(marker(0), 300.0f, kY, 0.0);
    CHECK(cut.hold(340.0f, kY + 12.0f, 0.05, kSlop) == BandTouch::Hold::MoveCut);
    CHECK(cut.lift(kNoSection) == BandTouch::Lift::MoveCut);
}

TEST_CASE("band: a press that drifts down inside the slop is still a long press") {
    BandTouch t;
    t.press(segment(2), 100.0f, kY, 0.0);
    CHECK(t.hold(100.0f, kY + kSlop, kLongPressS, kSlop) == BandTouch::Hold::Menu);
}
