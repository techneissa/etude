// SPDX-License-Identifier: GPL-3.0-or-later
// Scheduling the song's sound ahead of the clock. Each frame the notes that fall in the next
// `window` of the clock are handed over with the exact time they reach the hit line, so the sound
// starts when the player sees the note arrive, and a slow frame does not make it late as long as it
// is shorter than the window.
//
// The song clock (Transport) is not asked about its future. A cursor remembers the song time up to
// which notes have been handed over; the plan for a frame is the clock's position and speed now,
// and the span from the cursor to the position plus the window, as the Moves Playback turns into
// messages. Rules, each with a test in tests/look_ahead_test.cpp:
// - A note whose song time s lies in [cursor, position + window x speed) is handed over once, with
//   the due time now + (s - position) / speed. The cursor then stands at the end of that span, so
//   the next frame goes on from it and nothing is sent twice or missed.
// - A frame longer than the window finds the cursor behind the position: the notes in between are
//   handed over late (their due time is already past) and none is dropped, note-offs included.
// - The span stops at the end of the song, and at the point Wait Mode armed the clock to stop at,
//   because the clock will not go past it.
// - A loop: where the span crosses the loop end, the notes up to the end are followed by a Wrap and
//   the notes from the loop start for the rest of the window, due after the wrap. Nothing past the
//   loop end is handed over. Wraps the clock has made that were not planned (a frame longer than
//   the window), and wraps planned that the clock has not made yet, are counted in `wraps_ahead`.
// - A count-in: the first notes are handed over before the count ends, due when it does.
// - Not moving (held at a chord, or a catch-up before the end): no window, only the notes up to the
//   position.
// Whoever changes what the plan rests on (a seek, a new speed or loop, a mute, a transposition)
// cancels what was handed over and sets the cursor to the position again.
#pragma once

#include "etude/transport.hpp"

#include <cstdint>
#include <optional>
#include <vector>

namespace etude {

struct LookAheadInput {
    double position_s = 0.0; // the clock's position at now_ns
    int speed_percent = 100;
    bool moving = true; // the clock runs: schedule a window ahead of it
    // The clock starts to move this long after now_ns (the rest of a count-in): the window ahead
    // of it is what is left of the look-ahead after that, and every time is that much later.
    std::int64_t delay_ns = 0;
    double duration_s = 0.0;
    bool loop_on = false;
    double loop_start_s = 0.0;
    double loop_end_s = 0.0;
    // Wraps the clock has made minus wraps planned: positive when the clock wrapped in a frame that
    // was not planned for, negative when a wrap was planned that the clock has not reached.
    int wraps_ahead = 0;
    std::optional<double> hold_at_s; // the clock stops here, when it is still ahead
};

struct LookAheadPlan {
    int wraps_planned = 0; // Wrap ops appended
};

// Appends to `out` the ops (Move, then Wrap and Move at a loop end) that carry Playback from
// `cursor_s` to the end of the window, with wall times from `now_ns`, and moves `cursor_s` there.
LookAheadPlan plan_look_ahead(const LookAheadInput& in, std::int64_t now_ns, std::int64_t window_ns,
                              double& cursor_s, std::vector<TransportOp>& out);

} // namespace etude
