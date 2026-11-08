// SPDX-License-Identifier: GPL-3.0-or-later
// The Learn cards' sideways strip of chips (ui/chip_strip.hpp): where it stops scrolling, when a
// press becomes a drag (and so no tap on the chip under it), and the chosen chip kept in view.
#include "ui/chip_strip.hpp"

#include "etude/tap_rule.hpp"

#include <doctest/doctest.h>

#include <array>

using namespace etude;
using namespace etude::app::ui;

namespace {

constexpr float kGap = 4.0f;
constexpr std::array<float, 6> kWidths{80.0f, 70.0f, 90.0f, 60.0f, 100.0f, 75.0f};

// A strip of the six chips in a view `view` wide, driven frame by frame like the screen does.
struct Strip {
    ChipStrip strip;
    double t = 0.0;
    float content = strip_content_width(kWidths, kGap);

    explicit Strip(float view) { strip.set_extent(content, view); }
    bool frame() {
        t += 0.016;
        return strip.update(t);
    }
    // A finger lands at x and, `frames` frames later, is at x + dx; then lifts.
    bool swipe(float x, float dx, int frames, bool finger, bool lift = true) {
        strip.press(x, 20.0f, t, finger);
        bool took = frame();
        for (int i = 1; i <= frames; ++i) {
            strip.move(x + dx * static_cast<float>(i) / static_cast<float>(frames), 20.0f,
                       t + 0.016);
            took = frame() || took;
        }
        if (lift) {
            strip.release(x + dx, 20.0f, t);
            frame();
        }
        return took;
    }
};

} // namespace

TEST_CASE(
    "chip strip: the content is the chips and the gaps, and the strip scrolls only past its view") {
    CHECK(strip_content_width(kWidths, kGap) == doctest::Approx(475.0f + 5 * kGap));
    CHECK(strip_content_width({}, kGap) == 0.0f);
    CHECK(strip_max_offset(495.0f, 300.0f) == doctest::Approx(195.0f));
    CHECK(strip_max_offset(200.0f, 300.0f) == 0.0f); // all of it fits: nothing to scroll
}

TEST_CASE("chip strip: the offset stays between the start and the end") {
    CHECK(strip_clamp(-30.0f, 495.0f, 300.0f) == 0.0f);
    CHECK(strip_clamp(100.0f, 495.0f, 300.0f) == 100.0f);
    CHECK(strip_clamp(900.0f, 495.0f, 300.0f) == doctest::Approx(195.0f));
    CHECK(strip_clamp(50.0f, 200.0f, 300.0f) == 0.0f);
    Strip s(300.0f);
    s.strip.scroll_by(1000.0f);
    CHECK(s.strip.offset() == doctest::Approx(s.strip.max_offset()));
    s.strip.scroll_by(-5000.0f);
    CHECK(s.strip.offset() == 0.0f);
    // A view that grows (a bigger window) pulls a strip scrolled past the new end back.
    s.strip.scroll_by(1000.0f);
    s.strip.set_extent(s.content, s.content);
    CHECK(s.strip.offset() == 0.0f);
}

TEST_CASE("chip strip: the chosen chip scrolls into view with some of its neighbour showing") {
    const float content = 495.0f;
    const float view = 300.0f;
    // In view with room to spare: the strip stays.
    CHECK(strip_offset_to_show(0.0f, 80.0f, 150.0f, content, view, 20.0f) == 0.0f);
    CHECK(strip_offset_to_show(100.0f, 150.0f, 240.0f, content, view, 20.0f) == 100.0f);
    // Past the right edge: scrolled just far enough, the margin beyond it showing.
    CHECK(strip_offset_to_show(0.0f, 350.0f, 440.0f, content, view, 20.0f) ==
          doctest::Approx(160.0f));
    // Before the left edge: the margin before it showing.
    CHECK(strip_offset_to_show(190.0f, 100.0f, 170.0f, content, view, 20.0f) ==
          doctest::Approx(80.0f));
    // The ends hold: the first chip is at the start, the last at the end.
    CHECK(strip_offset_to_show(190.0f, 0.0f, 80.0f, content, view, 20.0f) == 0.0f);
    CHECK(strip_offset_to_show(0.0f, 415.0f, 495.0f, content, view, 20.0f) ==
          doctest::Approx(195.0f));
    // A chip wider than the view shows its left edge.
    CHECK(strip_offset_to_show(0.0f, 100.0f, 500.0f, 500.0f, 300.0f, 10.0f) ==
          doctest::Approx(90.0f));
    // And through the strip: after show(), the chip is wholly inside the view at the offset.
    for (std::size_t i = 0; i < kWidths.size(); ++i) {
        Strip s(200.0f);
        float x = 0.0f;
        for (std::size_t j = 0; j < i; ++j) {
            x += kWidths[j] + kGap;
        }
        s.strip.show(x, x + kWidths[i], 10.0f);
        CHECK(x >= s.strip.offset() - 1e-3f);
        CHECK(x + kWidths[i] <= s.strip.offset() + 200.0f + 1e-3f);
    }
}

TEST_CASE("chip strip: a press that stays inside the touch slop is a tap, not a drag") {
    Strip s(200.0f);
    // Under the slop: the strip does not move and does not take the touch from the chip.
    const float inside = kTouchSlopPt - 1.0f;
    CHECK_FALSE(s.swipe(100.0f, -inside, 3, true));
    CHECK(s.strip.offset() == 0.0f);
    CHECK_FALSE(s.strip.scrolling());
    CHECK_FALSE(s.strip.busy());
    // The tap rule agrees: that much movement is no drag; one point past it is.
    CHECK_FALSE(exceeds_slop(inside, 0.0f, kTouchSlopPt));
    CHECK(exceeds_slop(kTouchSlopPt + 1.0f, 0.0f, kTouchSlopPt));
}

TEST_CASE("chip strip: a drag past the slop scrolls it and takes the touch from the chip") {
    Strip s(200.0f);
    CHECK(s.swipe(150.0f, -(kTouchSlopPt + 21.0f), 4, false, false)); // a mouse drag to the left
    CHECK(s.strip.scrolling());
    // The strip follows the finger the whole way: the move was 30 points.
    CHECK(s.strip.offset() == doctest::Approx(kTouchSlopPt + 21.0f));
    s.strip.release(120.0f, 20.0f, s.t);
    s.frame();
    // A mouse stops at the lift; a drag to the right brings it back.
    const float lifted = s.strip.offset();
    CHECK(s.strip.busy() == false);
    s.swipe(100.0f, 40.0f, 4, false);
    CHECK(s.strip.offset() < lifted);
    CHECK(s.strip.offset() >= 0.0f);
}

TEST_CASE("chip strip: it never scrolls past either end under a drag") {
    Strip s(200.0f);
    s.swipe(150.0f, 300.0f, 6, false); // to the right at the start: stays at 0
    CHECK(s.strip.offset() == 0.0f);
    s.swipe(300.0f, -2000.0f, 6, false); // far to the left: stops at the end
    CHECK(s.strip.offset() == doctest::Approx(s.strip.max_offset()));
}

TEST_CASE("chip strip: a finger's fast swipe coasts on after the lift, and stops at the end") {
    Strip s(200.0f);
    s.swipe(190.0f, -60.0f, 5, true);
    const float at_lift = s.strip.offset();
    CHECK(s.strip.busy());
    for (int i = 0; i < 120 && s.strip.busy(); ++i) {
        s.frame();
    }
    CHECK(s.strip.offset() > at_lift);
    CHECK(s.strip.offset() <= s.strip.max_offset() + 1e-3f);
    CHECK_FALSE(s.strip.busy());
}

TEST_CASE("chip strip: a drag that is more up and down than sideways is left alone") {
    Strip s(200.0f);
    s.strip.press(100.0f, 20.0f, s.t, true);
    s.frame();
    s.strip.move(104.0f, 80.0f, s.t + 0.016); // mostly down
    const bool took = s.frame();
    CHECK_FALSE(took);
    CHECK(s.strip.offset() == 0.0f);
}

TEST_CASE("chip strip: a strip that fits takes no drag") {
    Strip s(1000.0f); // wider than the content
    CHECK(s.strip.max_offset() == 0.0f);
    CHECK_FALSE(s.swipe(300.0f, -80.0f, 4, true));
    CHECK(s.strip.offset() == 0.0f);
}

TEST_CASE("chip strip: cancelling drops a touch and its coasting") {
    Strip s(200.0f);
    s.swipe(190.0f, -60.0f, 5, true);
    s.strip.cancel();
    const float held = s.strip.offset();
    CHECK_FALSE(s.strip.busy());
    s.frame();
    s.frame();
    CHECK(s.strip.offset() == held);
}
