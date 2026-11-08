// SPDX-License-Identifier: GPL-3.0-or-later
// The frame loop's idle wait: core IdleGate decides, this waits for an event and is woken by input
// (loop_wake.hpp). The wait itself is a function passed in (idle_loop.hpp gives it SDL's), so this
// file has no SDL and is tested.
#pragma once

#include "loop_wake.hpp"
#include "picture_hash.hpp"

#include "etude/idle_gate.hpp"

#include <cstdint>

namespace etude::app {

// Waits for an event while nothing moves. Off (`allowed` false: scripted runs, ETUDE_NO_IDLE): every
// turn draws.
class IdleLoop {
  public:
    // `block` waits up to the given time for an event; `wake` makes it return (any thread).
    IdleLoop(bool allowed, void (*block)(std::int64_t timeout_ns), void (*wake)())
        : block_(block), allowed_(allowed) {
        set_wake_hook(wake);
    }

    // Top of a loop turn. Waits for an event when the picture has been still; true when it waited.
    bool wait_if_idle(std::int64_t now_ns) {
        if (!allowed_ || !gate_.idle(now_ns)) {
            return false;
        }
        const bool armed = arm_wait(seen_);
        if (armed) {
            block_(gate_.wait_ns());
        }
        disarm_wait();
        if (armed) {
            gate_.waited(); // a request that came before the wait is no wait at all
        }
        return armed;
    }
    // Whether the event poll returned an event (the wake-up included).
    void note_input(bool got) { input_ = input_ || got; }
    // The frame's first step, before the MIDI ports are drained. `busy`: something known to move.
    // A redraw from inside the OS's resize loop (`live`) is not a frame of the loop.
    void begin_frame(bool live, bool busy) {
        if (!live) {
            seen_ = wake_requests();
            busy_ = busy;
            picture::begin(!busy);
        }
    }
    // After the present.
    void end_frame(std::int64_t now_ns) {
        gate_.frame_drawn(now_ns, {input_, busy_, !busy_, picture::take()});
        input_ = false;
    }
    // The frame being drawn follows a wait: its gap is deliberate, so it counts for its own work
    // only (not the time since the last frame) in the governor and the frame log.
    bool first_after_wait() const { return gate_.first_after_wait(); }

  private:
    etude::IdleGate gate_;
    void (*block_)(std::int64_t) = nullptr;
    bool allowed_ = false;
    bool input_ = false;
    bool busy_ = false;
    std::uint64_t seen_ = 0;
};

} // namespace etude::app
