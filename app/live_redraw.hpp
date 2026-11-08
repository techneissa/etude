// SPDX-License-Identifier: GPL-3.0-or-later
// When the window event watch redraws on its own. The OS runs some window changes in a loop of
// its own inside the event pump (on macOS a drag of the window's edge, the zoom animation of the
// green button, a move into or out of full screen), and the main loop draws nothing until that
// loop ends: the window shows its old frame, stretched, for as long as the change takes. The watch
// is called from inside that loop, so it draws there instead, at most about once a frame.
#pragma once

#include <cstdint>

namespace etude::app {

// A window event that may call for a redraw.
enum class WindowRedrawEvent : std::uint8_t {
    Exposed,     // the window needs repainting
    LiveExposed, // the same, sent by the OS's live-resize loop on its own timer (60 a second)
    Resized,     // a new size in points
    PixelResized // a new size in pixels
};

// What the main loop is doing, as the watch sees it.
struct LiveRedrawState {
    bool in_frame = false;            // between the start and the end of a main-loop frame
    bool pumping = false;             // inside that frame's event pump
    std::int64_t pump_start_ns = 0;   // when the pump began
    std::int64_t last_redraw_ns = -1; // the watch's last redraw, -1 for none
};

// Whether the watch redraws now for `event`. Outside a frame (the window being changed by a call
// from elsewhere) always. Inside a frame's own drawing never: it is about to draw. Inside the
// event pump only when the pump has held the loop for a frame or more (an OS loop runs inside it)
// or the OS's live-resize timer asks, and then no more often than about once every half frame, so
// a size event and the timer arriving together cost one redraw.
inline bool live_redraw_due(const LiveRedrawState& s, WindowRedrawEvent event, std::int64_t now_ns,
                            std::int64_t frame_ns) {
    if (!s.in_frame) {
        return true;
    }
    if (!s.pumping) {
        return false;
    }
    if (s.last_redraw_ns >= 0 && now_ns - s.last_redraw_ns < frame_ns / 2) {
        return false;
    }
    if (event == WindowRedrawEvent::LiveExposed) {
        return true;
    }
    const std::int64_t since =
        now_ns - (s.last_redraw_ns > s.pump_start_ns ? s.last_redraw_ns : s.pump_start_ns);
    return since >= frame_ns;
}

} // namespace etude::app
