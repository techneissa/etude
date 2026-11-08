// SPDX-License-Identifier: GPL-3.0-or-later
// touch_gesture: swipe (split from touch_gesture_test.cpp).
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

// ---- The control bar's swipes

namespace {

// A hidden bar: the top row is 60 high.
BarSwipeArea hidden_bar() {
    BarSwipeArea a;
    a.bar_shown = false;
    a.row_bottom = 60.0f;
    a.bar_bottom = 60.0f;
    return a;
}

// A shown bar with its overview: reaches down to 108.
BarSwipeArea shown_bar() {
    BarSwipeArea a = hidden_bar();
    a.bar_shown = true;
    a.bar_bottom = 108.0f;
    return a;
}

// Runs a touch from (x0, y0) to (x1, y1) in a few steps through a recogniser into a tracker and
// returns every swipe it reported.
std::vector<BarSwipe> swipes_of(const BarSwipeArea& area, float x0, float y0, float x1, float y1,
                                bool lift = true) {
    GestureRecognizer g;
    BarSwipeTracker t;
    std::vector<BarSwipe> out;
    const auto feed = [&] {
        for (const auto& e : g.events()) {
            const BarSwipe s = t.on_event(e, area);
            if (s != BarSwipe::None) {
                out.push_back(s);
            }
        }
        g.clear_events();
    };
    g.down(1, x0, y0, 0.0);
    feed();
    constexpr int kSteps = 8;
    for (int i = 1; i <= kSteps; ++i) {
        const float f = static_cast<float>(i) / kSteps;
        g.move(1, x0 + (x1 - x0) * f, y0 + (y1 - y0) * f, 0.02 * i);
        feed();
    }
    if (lift) {
        g.up(1, x1, y1, 0.2);
        feed();
    }
    return out;
}

// The same, without the claims: what the bar is asked to do.
std::vector<BarSwipe> actions_of(const BarSwipeArea& area, float x0, float y0, float x1, float y1,
                                 bool lift = true) {
    std::vector<BarSwipe> out;
    for (const BarSwipe s : swipes_of(area, x0, y0, x1, y1, lift)) {
        if (s != BarSwipe::Claim) {
            out.push_back(s);
        }
    }
    return out;
}

} // namespace

// ---- A lift that never came

TEST_CASE("a finger lands, its lift is lost, a finger lands alone: a clean press, a tap") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.update(0.2);
    drain(g);
    // Much later, with no other finger on the screen.
    g.down(2, 300, 80, 10.0, false);
    g.up(2, 300, 80, 10.05);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Cancel, Kind::Press, Kind::Tap});
    CHECK(e.back().x == 300.0f);
    CHECK(g.fingers() == 0);
    CHECK(g.touching() == false);
}

TEST_CASE("a lost lift while a long press was held also drops it, with the release never coming") {
    GestureRecognizer::Config c;
    c.drag_after_long_press = true;
    GestureRecognizer g(c);
    g.down(1, 100, 50, 0.0);
    g.update(0.5);
    drain(g);
    REQUIRE(g.touching());
    g.down(2, 200, 60, 8.0, false);
    auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Cancel, Kind::Press});
    // The new finger is a first finger: a drag, not a pinch.
    g.move(2, 260, 60, 8.1);
    g.up(2, 260, 60, 8.2);
    e = drain(g);
    CHECK(count_of(e, Kind::DragStart) == 1);
    CHECK(count_of(e, Kind::PinchStart) == 0);
}

TEST_CASE("a lost lift in a drag, a pinch or a fling is dropped the same way") {
    {
        GestureRecognizer g;
        g.down(1, 100, 50, 0.0);
        g.move(1, 180, 50, 0.1);
        drain(g);
        REQUIRE(g.touching());
        g.down(2, 20, 20, 9.0, false);
        const auto e = drain(g);
        CHECK(kinds(e) == std::vector<Kind>{Kind::Cancel, Kind::Press});
    }
    {
        GestureRecognizer g;
        g.down(1, 100, 50, 0.0);
        g.down(2, 200, 50, 0.0);
        g.move(2, 240, 50, 0.1);
        drain(g);
        REQUIRE(g.pinching());
        g.down(3, 20, 20, 9.0, false);
        const auto e = drain(g);
        CHECK(kinds(e) == std::vector<Kind>{Kind::Cancel, Kind::Press});
        CHECK(g.pinching() == false);
        CHECK(g.fingers() == 1);
        // And the next two fingers pinch as ever.
        g.down(4, 120, 20, 9.1, true);
        const auto p = drain(g);
        CHECK(count_of(p, Kind::PinchStart) == 1);
    }
    {
        GestureRecognizer g;
        g.down(1, 100, 50, 0.0);
        g.move(1, 160, 50, 0.05);
        g.move(1, 260, 50, 0.1);
        g.up(1, 360, 50, 0.12); // a fast drag: it flings
        drain(g);
        REQUIRE(g.flinging());
        g.down(2, 20, 20, 0.2, false);
        const auto e = drain(g);
        CHECK(count_of(e, Kind::FlingEnd) == 1);
        CHECK(count_of(e, Kind::Press) == 1);
    }
}

TEST_CASE("a finger that lands while another is known down is a second finger, as before") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0, false); // the first finger: nothing else on the screen
    g.down(2, 200, 50, 0.01, true); // the host counts one finger down: this is the second
    g.move(2, 230, 50, 0.05);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Press, Kind::Cancel, Kind::PinchStart, Kind::PinchMove});
    CHECK(g.pinching());
}

TEST_CASE("the default is a second finger, so a caller that cannot tell keeps the old behaviour") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0);
    g.down(2, 200, 50, 0.01);
    CHECK(g.pinching());
}

TEST_CASE("a landing for a finger the recogniser holds is a lost lift, whatever the caller knows") {
    GestureRecognizer g;
    g.down(7, 100, 50, 0.0);
    drain(g);
    g.down(7, 300, 90, 4.0, true); // the same id again: the old touch can only be gone
    g.up(7, 300, 90, 4.05);
    const auto e = drain(g);
    CHECK(kinds(e) == std::vector<Kind>{Kind::Cancel, Kind::Press, Kind::Tap});
    CHECK(e.back().x == 300.0f);
}

TEST_CASE("a lost lift with nothing held is nothing to drop: no Cancel") {
    GestureRecognizer g;
    g.down(1, 100, 50, 0.0, false);
    g.up(1, 100, 50, 0.05);
    drain(g);
    g.down(2, 100, 50, 5.0, false);
    CHECK(kinds(drain(g)) == std::vector<Kind>{Kind::Press});
}

TEST_CASE("a downward swipe from the top row reveals the hidden bar, once") {
    CHECK(actions_of(hidden_bar(), 400, 4, 400, 60) == std::vector<BarSwipe>{BarSwipe::Reveal});
    CHECK(actions_of(hidden_bar(), 400, 59, 410, 140) == std::vector<BarSwipe>{BarSwipe::Reveal});
    // Still down, still only one report.
    CHECK(actions_of(hidden_bar(), 400, 4, 400, 200, false) ==
          std::vector<BarSwipe>{BarSwipe::Reveal});
}

TEST_CASE("an upward swipe on the shown bar hides it, from the row or its overview") {
    CHECK(actions_of(shown_bar(), 300, 40, 300, 0) == std::vector<BarSwipe>{BarSwipe::Hide});
    CHECK(actions_of(shown_bar(), 300, 100, 300, 30) == std::vector<BarSwipe>{BarSwipe::Hide});
}

TEST_CASE("swipes in the wrong direction, place or axis are nothing") {
    // Up on a hidden bar, down on a shown one.
    CHECK(actions_of(hidden_bar(), 400, 50, 400, 0).empty());
    CHECK(actions_of(shown_bar(), 400, 10, 400, 90).empty());
    // A reveal must begin in the top row, a hide on the bar: a swipe in the notes does neither.
    CHECK(actions_of(hidden_bar(), 400, 61, 400, 160).empty());
    CHECK(actions_of(shown_bar(), 400, 109, 400, 20).empty());
    // Sideways, or mostly sideways, is the overview's scrub, not a swipe.
    CHECK(actions_of(shown_bar(), 100, 50, 400, 30).empty());
    CHECK(actions_of(hidden_bar(), 100, 10, 400, 60).empty());
}

TEST_CASE("a tap or a small move is never a swipe") {
    CHECK(actions_of(hidden_bar(), 400, 20, 400, 20).empty());
    CHECK(actions_of(hidden_bar(), 400, 20, 402, 28).empty()); // under the drag threshold
    CHECK(actions_of(hidden_bar(), 400, 10, 400, 30).empty()); // a drag, but 20 < 24 points
}

TEST_CASE("the swipe distance is exactly kBarSwipeTravelPt, scaled by the unit") {
    BarSwipeArea a = hidden_bar();
    CHECK(classify_bar_swipe(a, 10, 0, kBarSwipeTravelPt) == BarSwipe::Reveal);
    CHECK(classify_bar_swipe(a, 10, 0, kBarSwipeTravelPt - 0.01f) == BarSwipe::None);
    a.unit = 2.0f;
    CHECK(classify_bar_swipe(a, 10, 0, kBarSwipeTravelPt) == BarSwipe::None);
    CHECK(classify_bar_swipe(a, 10, 0, 2.0f * kBarSwipeTravelPt) == BarSwipe::Reveal);
    // A bad unit counts as 1.
    a.unit = 0.0f;
    CHECK(classify_bar_swipe(a, 10, 0, kBarSwipeTravelPt) == BarSwipe::Reveal);
}

TEST_CASE("the drag axis rule is exactly kDragAxisRatio") {
    const BarSwipeArea a = hidden_bar();
    const float dy = 60.0f;
    CHECK(classify_bar_swipe(a, 10, dy / kDragAxisRatio, dy) == BarSwipe::Reveal);
    CHECK(classify_bar_swipe(a, 10, dy / kDragAxisRatio + 0.5f, dy) == BarSwipe::None);
    CHECK(classify_bar_swipe(a, 10, -dy / kDragAxisRatio, dy) == BarSwipe::Reveal); // either way
}

TEST_CASE("the start row's edge is exclusive: a reveal begins above the row's bottom") {
    const BarSwipeArea a = hidden_bar();
    CHECK(classify_bar_swipe(a, 59.99f, 0, 40) == BarSwipe::Reveal);
    CHECK(classify_bar_swipe(a, 60.0f, 0, 40) == BarSwipe::None);
    const BarSwipeArea b = shown_bar();
    CHECK(classify_bar_swipe(b, 107.99f, 0, -40) == BarSwipe::Hide);
    CHECK(classify_bar_swipe(b, 108.0f, 0, -40) == BarSwipe::None);
}

TEST_CASE("bad numbers are never a swipe") {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    const BarSwipeArea a = hidden_bar();
    CHECK(classify_bar_swipe(a, nan, 0, 40) == BarSwipe::None);
    CHECK(classify_bar_swipe(a, 10, nan, 40) == BarSwipe::None);
    CHECK(classify_bar_swipe(a, 10, 0, nan) == BarSwipe::None);
}

TEST_CASE("a second finger or a cancel forgets the drag, and the next drag can swipe again") {
    const BarSwipeArea area = hidden_bar();
    BarSwipeTracker t;
    using Event = GestureRecognizer::Event;
    Event start{Kind::DragStart};
    start.y = 5.0f;
    Event move{Kind::DragMove};
    move.total_dy = 40.0f;
    CHECK(t.on_event(start, area) == BarSwipe::None);
    CHECK(t.on_event(Event{Kind::Cancel}, area) == BarSwipe::None);
    CHECK(t.on_event(move, area) == BarSwipe::None); // no drag in progress
    CHECK(t.on_event(start, area) == BarSwipe::None);
    CHECK(t.on_event(move, area) == BarSwipe::Reveal);
    CHECK(t.on_event(move, area) == BarSwipe::None); // reported once
    CHECK(t.on_event(Event{Kind::DragEnd}, area) == BarSwipe::None);
    CHECK(t.on_event(start, area) == BarSwipe::None);
    CHECK(t.on_event(move, area) == BarSwipe::Reveal); // a new drag
}

TEST_CASE("a swipe that begins on the bar judges the bar as it was when the drag began") {
    // The tracker keeps only the start row: the area can change under a drag (the bar fading in)
    // without a half-finished swipe flipping direction.
    BarSwipeTracker t;
    using Event = GestureRecognizer::Event;
    Event start{Kind::DragStart};
    start.y = 20.0f;
    Event up{Kind::DragMove};
    up.total_dy = -40.0f;
    CHECK(t.on_event(start, shown_bar()) == BarSwipe::None);
    CHECK(t.on_event(up, shown_bar()) == BarSwipe::Hide);
}

TEST_CASE(
    "the axis is decided once, at the first move past the slop: vertical, horizontal or neither") {
    CHECK(decide_drag_axis(0, 0) == DragAxis::Undecided);
    CHECK(decide_drag_axis(0, 10) == DragAxis::Vertical);
    CHECK(decide_drag_axis(-3, -10) == DragAxis::Vertical);
    CHECK(decide_drag_axis(10, 0) == DragAxis::Horizontal);
    CHECK(decide_drag_axis(10, 10) == DragAxis::Horizontal); // a diagonal is a scrub, not a swipe
    CHECK(decide_drag_axis(10, 15) == DragAxis::Vertical);   // exactly the ratio
    CHECK(decide_drag_axis(10, 14.9f) == DragAxis::Horizontal);
    const float nan = std::numeric_limits<float>::quiet_NaN();
    CHECK(decide_drag_axis(nan, 10) == DragAxis::Undecided);
}

TEST_CASE("a vertical drag on the bar is claimed at the slop, before the swipe's travel") {
    const BarSwipeArea area = shown_bar();
    const auto all = swipes_of(area, 300, 100, 300, 30);
    REQUIRE(all.size() == 2);
    CHECK(all[0] == BarSwipe::Claim);
    CHECK(all[1] == BarSwipe::Hide);
    // A hidden bar's downward swipe is claimed the same way.
    const auto down = swipes_of(hidden_bar(), 400, 4, 400, 60);
    REQUIRE(down.size() == 2);
    CHECK(down[0] == BarSwipe::Claim);
    CHECK(down[1] == BarSwipe::Reveal);
}

TEST_CASE("the claim comes at the touch slop, not at the swipe's travel") {
    // One step just past the slop (9 points up): claimed, but not yet a hide, and a finger that
    // stops there and lifts hides nothing.
    GestureRecognizer g;
    BarSwipeTracker t;
    const BarSwipeArea area = shown_bar();
    g.down(1, 300, 90, 0.0);
    g.move(1, 300, 90.0f - kTouchSlopPt - 1.0f, 0.05);
    g.up(1, 300, 90.0f - kTouchSlopPt - 1.0f, 0.1);
    std::vector<BarSwipe> got;
    for (const auto& e : g.events()) {
        got.push_back(t.on_event(e, area));
    }
    CHECK(std::count(got.begin(), got.end(), BarSwipe::Claim) == 1);
    CHECK(std::count(got.begin(), got.end(), BarSwipe::Hide) == 0);
}

TEST_CASE("a slow upward swipe is claimed long before the long press, and the press never fires") {
    // The touch is held still for 0.6 s (the recogniser's long press is off for the bar's touch),
    // then creeps up 1 point per 100 ms: claimed at the slop, hidden at the travel.
    GestureRecognizer::Config cfg;
    cfg.long_press = false;
    GestureRecognizer g(cfg);
    BarSwipeTracker t;
    const BarSwipeArea area = shown_bar();
    g.down(1, 300, 100, 0.0);
    g.update(0.6);
    std::vector<BarSwipe> got;
    const auto feed = [&] {
        for (const auto& e : g.events()) {
            const BarSwipe s = t.on_event(e, area);
            if (s != BarSwipe::None) {
                got.push_back(s);
            }
        }
        g.clear_events();
    };
    feed();
    double clock_s = 0.6;
    float y = 100;
    int claim_at = -1;
    int hide_at = -1;
    for (int i = 0; i < 40; ++i) {
        y -= 1.0f;
        clock_s += 0.1;
        g.move(1, 300, y, clock_s);
        const std::size_t before = got.size();
        feed();
        for (std::size_t k = before; k < got.size(); ++k) {
            if (got[k] == BarSwipe::Claim) {
                claim_at = i;
            }
            if (got[k] == BarSwipe::Hide) {
                hide_at = i;
            }
        }
    }
    CHECK(claim_at >= 0);
    CHECK(claim_at <= static_cast<int>(kTouchSlopPt) + 1); // at the slop
    CHECK(hide_at >= static_cast<int>(kBarSwipeTravelPt) - 1);
    CHECK(claim_at < hide_at);
    // The claim came at about 9 points, 0.9 s into the creep and 1.5 s into the touch: the
    // overview's own long press (0.45 s) would have fired first unless the claim cancels it, which
    // is why the claim exists.
    CHECK(0.6 + 0.1 * (claim_at + 1) > kLongPressS);
}

TEST_CASE("a horizontal start is a scrub, and never a swipe however it drifts after") {
    const BarSwipeArea area = shown_bar();
    GestureRecognizer g;
    BarSwipeTracker t;
    g.down(1, 100, 80, 0.0);
    g.move(1, 100 + 20, 80 - 4, 0.05); // past the slop, mostly sideways
    g.move(1, 100 + 22, 80 - 60, 0.1); // then straight up, far past the travel
    g.move(1, 100 + 22, 80 - 120, 0.15);
    g.up(1, 100 + 22, 80 - 120, 0.2);
    for (const auto& e : g.events()) {
        CHECK(t.on_event(e, area) == BarSwipe::None);
    }
}

TEST_CASE("a vertical start stays vertical: drifting sideways later does not cancel the swipe") {
    const BarSwipeArea area = shown_bar();
    GestureRecognizer g;
    BarSwipeTracker t;
    g.down(1, 100, 100, 0.0);
    g.move(1, 102, 100 - 12, 0.05);     // vertical at the slop
    g.move(1, 100 + 60, 100 - 30, 0.1); // now mostly sideways, past the travel
    int hides = 0;
    for (const auto& e : g.events()) {
        hides += t.on_event(e, area) == BarSwipe::Hide ? 1 : 0;
    }
    CHECK(hides == 1);
}

TEST_CASE("a vertical drag that starts below the bar is not claimed") {
    const BarSwipeArea area = shown_bar();
    const auto all = swipes_of(area, 300, 200, 300, 120);
    CHECK(all.empty());
    CHECK(swipes_of(hidden_bar(), 400, 100, 400, 200).empty());
}

TEST_CASE("a two-finger gesture never reveals or hides the bar") {
    for (const bool shown : {false, true}) {
        const BarSwipeArea area = shown ? shown_bar() : hidden_bar();
        const float y0 = 40.0f;
        // Two fingers on the bar together, spreading or moving vertically, every way round.
        for (const float dy1 : {-80.0f, 80.0f}) {
            for (const float dy2 : {-80.0f, 80.0f}) {
                GestureRecognizer g;
                BarSwipeTracker t;
                g.down(1, 300, y0, 0.0);
                g.down(2, 330, y0, 0.01); // the second finger, before the first has moved
                for (int i = 1; i <= 8; ++i) {
                    const float f = static_cast<float>(i) / 8.0f;
                    g.move(1, 300, y0 + dy1 * f, 0.02 * i);
                    g.move(2, 330, y0 + dy2 * f, 0.02 * i);
                }
                g.up(1, 300, y0 + dy1, 0.3);
                g.up(2, 330, y0 + dy2, 0.31);
                for (const auto& e : g.events()) {
                    CHECK(t.on_event(e, area) == BarSwipe::None);
                }
            }
        }
        // The second finger lands in the middle of the first one's drag, before it has travelled
        // far: the swipe is over, and what the first finger does after counts for nothing.
        GestureRecognizer g;
        BarSwipeTracker t;
        g.down(1, 300, y0, 0.0);
        g.move(1, 300, y0 + (shown ? -12.0f : 12.0f), 0.05);
        g.down(2, 340, y0, 0.06);
        g.move(1, 300, y0 + (shown ? -90.0f : 90.0f), 0.1);
        g.up(1, 300, y0 + (shown ? -90.0f : 90.0f), 0.15);
        g.up(2, 340, y0, 0.16);
        for (const auto& e : g.events()) {
            const BarSwipe s = t.on_event(e, area);
            CHECK(s != BarSwipe::Reveal);
            CHECK(s != BarSwipe::Hide);
        }
        // A drag after the pinch is a fresh one.
        g.clear_events();
        g.down(1, 300, y0, 1.0);
        g.move(1, 300, y0 + (shown ? -40.0f : 40.0f), 1.05);
        bool swiped = false;
        for (const auto& e : g.events()) {
            const BarSwipe s = t.on_event(e, area);
            swiped = swiped || s == BarSwipe::Reveal || s == BarSwipe::Hide;
        }
        CHECK(swiped);
    }
}
