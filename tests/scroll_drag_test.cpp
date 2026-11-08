// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/scroll_drag.hpp"

#include <doctest/doctest.h>

#include <algorithm>

using namespace etude;

namespace {

constexpr float kMax = 400.0f;

// A page being scrolled: the offset follows the steps, clamped like a real scroll area.
struct Page {
    ScrollDrag drag;
    float offset = 0.0f;
    double t = 0.0;

    ScrollDrag::Step frame(bool held = false, double dt = 0.016) {
        t += dt;
        const ScrollDrag::Step s = drag.update(t, held, offset, kMax);
        offset = std::clamp(offset + s.scroll_by, 0.0f, kMax);
        return s;
    }
    // The finger goes from y0 to y1 in `frames` frames, then lifts.
    void swipe(float x, float y0, float y1, int frames, bool finger, bool widget_after = false,
               int widget_from_frame = 0) {
        drag.press(x, y0, t, finger);
        frame();
        for (int i = 1; i <= frames; ++i) {
            const float y = y0 + (y1 - y0) * static_cast<float>(i) / static_cast<float>(frames);
            drag.move(x, y, t + 0.016);
            frame(widget_after && i >= widget_from_frame);
        }
        drag.release(x, y1, t);
        frame(widget_after);
    }
    float coast(int frames) {
        const float before = offset;
        for (int i = 0; i < frames; ++i) {
            frame();
        }
        return offset - before;
    }
};

} // namespace

TEST_CASE("scroll drag: the page follows a drag, from the slop on") {
    Page p;
    p.drag.press(100, 300, p.t, true);
    p.frame();
    p.drag.move(100, 296, p.t + 0.016); // inside the slop
    p.frame();
    CHECK(p.offset == 0.0f);
    p.drag.move(100, 250, p.t + 0.016);
    const auto s = p.frame();
    CHECK(s.took_touch);
    CHECK(p.drag.scrolling());
    CHECK(p.offset == doctest::Approx(50.0f)); // the whole move so far
}

TEST_CASE("scroll drag: a real flick coasts on after the lift and the speed dies away") {
    Page p;
    p.swipe(100, 300, 240, 5, true);
    const float at_lift = p.offset;
    CHECK(at_lift >= 60.0f); // the drag's 60, plus the first step of the coast
    CHECK(at_lift < 90.0f);
    CHECK(p.drag.coasting());
    const float first = p.coast(10);
    const float later = p.coast(10);
    CHECK(first > 10.0f);
    CHECK(later < first); // an ease-out
    CHECK(later > 0.0f);
}

TEST_CASE("scroll drag: a new touch stops the coasting at once") {
    Page p;
    p.swipe(100, 300, 240, 5, true);
    p.coast(3);
    REQUIRE(p.drag.coasting());
    const float stopped_at = p.offset;
    p.drag.press(100, 100, p.t, true);
    p.frame();
    CHECK_FALSE(p.drag.coasting());
    CHECK(p.coast(30) == 0.0f);
    CHECK(p.offset == stopped_at);
}

TEST_CASE("scroll drag: a slider drag then release gives no scroll and no coast") {
    Page p;
    // A fast, mostly horizontal drag with some vertical drift; the slider holds it throughout.
    p.drag.press(100, 300, p.t, true);
    p.frame(true);
    for (int i = 1; i <= 5; ++i) {
        p.drag.move(100.0f + 30.0f * static_cast<float>(i), 300.0f - 6.0f * static_cast<float>(i),
                    p.t + 0.016);
        p.frame(true);
    }
    p.drag.release(250, 270, p.t);
    p.frame(true);
    CHECK(p.offset == 0.0f);
    CHECK_FALSE(p.drag.coasting());
    CHECK(p.coast(60) == 0.0f);
}

TEST_CASE(
    "scroll drag: a drag a widget takes later in the touch stops scrolling and never coasts") {
    Page p;
    p.swipe(100, 300, 240, 5, true, true, 5); // the widget claims the touch on the last frame
    CHECK_FALSE(p.drag.coasting());
    CHECK(p.coast(60) == 0.0f);
}

TEST_CASE("scroll drag: a touch that never scrolled never coasts") {
    Page p;
    // Fast drag on a page that cannot scroll (max offset 0).
    ScrollDrag d;
    double t = 0.0;
    d.press(100, 300, t, true);
    d.update(t += 0.016, false, 0.0f, 0.0f);
    for (int i = 1; i <= 5; ++i) {
        d.move(100, 300.0f - 12.0f * static_cast<float>(i), t + 0.016);
        const auto s = d.update(t += 0.016, false, 0.0f, 0.0f);
        CHECK(s.scroll_by == 0.0f);
    }
    d.release(100, 240, t);
    d.update(t += 0.016, false, 0.0f, 0.0f);
    for (int i = 0; i < 20; ++i) {
        CHECK(d.update(t += 0.016, false, 0.0f, 0.0f).scroll_by == 0.0f);
    }
    CHECK_FALSE(d.coasting());
}

TEST_CASE("scroll drag: a mouse drag scrolls but does not coast") {
    Page p;
    p.swipe(100, 300, 240, 5, false);
    CHECK(p.offset == doctest::Approx(60.0f));
    CHECK_FALSE(p.drag.coasting());
    CHECK(p.coast(30) == 0.0f);
}

TEST_CASE("scroll drag: coasting stops dead at the end of the page") {
    Page p;
    p.offset = kMax - 20.0f;
    p.swipe(100, 300, 150, 5, true); // a hard fling toward the end
    p.coast(20);
    CHECK(p.offset == doctest::Approx(kMax));
    CHECK_FALSE(p.drag.busy());
    CHECK(p.coast(30) == 0.0f);
}

TEST_CASE("scroll drag: coasting stops dead at the top too") {
    Page p;
    p.offset = 20.0f;
    p.swipe(100, 150, 300, 5, true); // toward the top
    p.coast(20);
    CHECK(p.offset == doctest::Approx(0.0f));
    CHECK_FALSE(p.drag.busy());
}

TEST_CASE("scroll drag: holding still before a drag is still a scroll") {
    Page p;
    p.drag.press(100, 300, p.t, true);
    p.frame(false, 2.0); // two seconds, far past a long press
    p.drag.move(100, 240, p.t + 0.016);
    p.frame();
    CHECK(p.offset == doctest::Approx(60.0f));
}

TEST_CASE("scroll drag: cancel drops the touch and the coasting") {
    Page p;
    p.swipe(100, 300, 240, 5, true);
    REQUIRE(p.drag.coasting());
    p.drag.cancel();
    CHECK_FALSE(p.drag.busy());
    CHECK(p.coast(30) == 0.0f);
}

TEST_CASE("scroll drag: a sideways row follows dx, and a locked page leaves the other drags alone") {
    // A shelf scrolls sideways with x.
    Page row;
    row.drag.set_axis(ScrollDrag::Axis::Horizontal, true);
    row.drag.press(300, 100, row.t, true);
    row.frame();
    row.drag.move(250, 102, row.t + 0.016);
    const auto s = row.frame();
    CHECK(s.took_touch);
    CHECK(row.offset == doctest::Approx(50.0f));
    // A drag mostly down is the page's, not the row's: it never scrolls the row, nor coasts.
    Page down;
    down.drag.set_axis(ScrollDrag::Axis::Horizontal, true);
    down.drag.press(300, 100, down.t, true);
    down.frame();
    down.drag.move(290, 160, down.t + 0.016);
    CHECK_FALSE(down.frame().took_touch);
    down.drag.move(200, 170, down.t + 0.016); // turning sideways later changes nothing
    down.frame();
    CHECK(down.offset == 0.0f);
    // And the page that locks its axis lets a sideways drag go.
    Page page;
    page.drag.set_axis(ScrollDrag::Axis::Vertical, true);
    page.swipe(300, 100, 100, 1, true); // no move: nothing
    page.drag.press(300, 200, page.t, true);
    page.frame();
    page.drag.move(200, 190, page.t + 0.016);
    CHECK_FALSE(page.frame().took_touch);
    CHECK(page.offset == 0.0f);
}
