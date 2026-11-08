// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstdint>

#include "etude/transport.hpp"

namespace etude {

// When the frame loop may stop drawing. A frame that looks like the one before it, with no input and
// nothing known to move, is a still frame; once the picture has stayed still for `settle_ns` the
// loop waits for an event instead of presenting, and wakes at least every `tick_ns` so timers and
// the results of other threads still show.
struct IdleConfig {
    std::int64_t settle_ns = 300'000'000; // still this long before the loop stops drawing
    std::int64_t tick_ns = 250'000'000;   // the longest the loop waits for an event
};

// What the frame just drawn tells the gate.
struct IdleFrame {
    bool input = false; // an event arrived (touch, mouse, keys, window, MIDI, microphone)
    bool busy = false;  // something known to move: see song_keeps_drawing
    // A hash of what the frame drew, with positions rounded to a sixteenth of a pixel so an easing
    // that has stopped changing the picture reads as still. Not known: leave `picture_known` off.
    bool picture_known = false;
    std::uint64_t picture = 0;
};

// A song that is counting in, playing or counting down to resume moves on its own however the
// picture looks, so it never idles. Held (Wait Mode waiting for the player) and Ended do not.
inline bool song_keeps_drawing(TransportState state, bool resume_countdown) {
    return resume_countdown || state == TransportState::CountIn || state == TransportState::Playing;
}

// Pure logic: the caller feeds each drawn frame and asks whether to wait before the next.
class IdleGate {
  public:
    explicit IdleGate(IdleConfig config = {}) : config_(config) {}

    // Call after each frame is drawn, `now_ns` on the clock used for the waits.
    void frame_drawn(std::int64_t now_ns, const IdleFrame& frame);
    // True when the loop may wait for an event instead of drawing the next frame.
    bool idle(std::int64_t now_ns) const;
    // The longest wait for an event while idle.
    std::int64_t wait_ns() const { return config_.tick_ns; }
    // The loop waited: the next frame is the first after the gap.
    void waited() { woke_ = true; }
    // True for the frame that follows a wait, until it is drawn. Its interval is the deliberate gap,
    // so it feeds neither the effects governor nor a frame-time verdict.
    bool first_after_wait() const { return woke_; }

  private:
    IdleConfig config_;
    bool started_ = false;
    bool woke_ = false;
    bool have_picture_ = false;
    std::uint64_t picture_ = 0;
    std::int64_t last_moving_ns_ = 0;
};

} // namespace etude
