// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/live_redraw.hpp"

#include <doctest/doctest.h>

using etude::app::live_redraw_due;
using etude::app::LiveRedrawState;
using etude::app::WindowRedrawEvent;

namespace {
constexpr std::int64_t kFrame = 16'666'667;
constexpr std::int64_t kMs = 1'000'000;
} // namespace

TEST_CASE("live redraw: outside a frame the watch always redraws") {
    const LiveRedrawState s;
    CHECK(live_redraw_due(s, WindowRedrawEvent::Resized, 0, kFrame));
    CHECK(live_redraw_due(s, WindowRedrawEvent::Exposed, 0, kFrame));
}

TEST_CASE("live redraw: never while the frame itself draws") {
    LiveRedrawState s;
    s.in_frame = true;
    s.pumping = false;
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::Resized, 100 * kMs, kFrame));
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::LiveExposed, 100 * kMs, kFrame));
}

TEST_CASE("live redraw: a quick pump leaves the drawing to the loop") {
    LiveRedrawState s;
    s.in_frame = true;
    s.pumping = true;
    s.pump_start_ns = 1000 * kMs;
    // A resize the loop asked for itself arrives at once: the loop draws right after the pump.
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::Resized, 1000 * kMs + 1, kFrame));
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::PixelResized, 1000 * kMs + kFrame - 1, kFrame));
    // Exactly a frame inside the pump: an OS loop holds it.
    CHECK(live_redraw_due(s, WindowRedrawEvent::Resized, 1000 * kMs + kFrame, kFrame));
}

TEST_CASE("live redraw: the OS's live-resize timer redraws at once") {
    LiveRedrawState s;
    s.in_frame = true;
    s.pumping = true;
    s.pump_start_ns = 1000 * kMs;
    CHECK(live_redraw_due(s, WindowRedrawEvent::LiveExposed, 1000 * kMs + 1, kFrame));
}

TEST_CASE("live redraw: inside a long pump about once a frame") {
    LiveRedrawState s;
    s.in_frame = true;
    s.pumping = true;
    s.pump_start_ns = 0;
    s.last_redraw_ns = 200 * kMs;
    // A size event and the timer together: one redraw. Just under half a frame after it, none.
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::LiveExposed, 200 * kMs + kFrame / 2 - 1, kFrame));
    CHECK(live_redraw_due(s, WindowRedrawEvent::LiveExposed, 200 * kMs + kFrame / 2, kFrame));
    // Size events wait a whole frame after the last redraw.
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::Resized, 200 * kMs + kFrame - 1, kFrame));
    CHECK(live_redraw_due(s, WindowRedrawEvent::Resized, 200 * kMs + kFrame, kFrame));
}

TEST_CASE("live redraw: a redraw from an earlier pump does not count against this one") {
    LiveRedrawState s;
    s.in_frame = true;
    s.pumping = true;
    s.last_redraw_ns = 10 * kMs;
    s.pump_start_ns = 500 * kMs;
    CHECK_FALSE(live_redraw_due(s, WindowRedrawEvent::Resized, 500 * kMs + kFrame - 1, kFrame));
    CHECK(live_redraw_due(s, WindowRedrawEvent::Resized, 500 * kMs + kFrame, kFrame));
}
