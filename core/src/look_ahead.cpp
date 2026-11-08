// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/look_ahead.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

LookAheadPlan plan_look_ahead(const LookAheadInput& in, std::int64_t now_ns, std::int64_t window_ns,
                              double& cursor_s, std::vector<TransportOp>& out) {
    LookAheadPlan plan;
    const double speed = std::max(1, in.speed_percent) / 100.0;
    const bool looping = in.loop_on && in.loop_end_s > in.loop_start_s;
    const double loop_len = looping ? in.loop_end_s - in.loop_start_s : 0.0;
    // The position in the timeline the cursor lives in: a wrap planned ahead of the clock puts the
    // cursor after it while the clock is still before the loop end.
    const double position = in.position_s + (looping ? in.wraps_ahead * loop_len : 0.0);
    const std::int64_t delay_ns = std::max<std::int64_t>(0, in.delay_ns);
    const double window_s =
        in.moving ? static_cast<double>(std::max<std::int64_t>(0, window_ns - delay_ns)) / 1e9 * speed
                  : 0.0;
    const double hi = position + window_s;
    const auto wall = [&](double s) {
        return now_ns + delay_ns + std::llround((s - position) / speed * 1e9);
    };
    const auto move = [&](double from, double to, std::int64_t at, std::int64_t end) {
        TransportOp op;
        op.kind = TransportOp::Kind::Move;
        op.at_ns = at;
        op.end_ns = end;
        op.from_s = from;
        op.to_s = to;
        op.speed_percent = in.speed_percent;
        out.push_back(op);
    };

    if (looping && cursor_s < in.loop_end_s && hi > in.loop_end_s) {
        if (in.loop_end_s > cursor_s) {
            move(cursor_s, in.loop_end_s, wall(cursor_s), wall(in.loop_end_s));
        }
        TransportOp wrap;
        wrap.kind = TransportOp::Kind::Wrap;
        wrap.at_ns = wall(in.loop_end_s);
        wrap.from_s = in.loop_end_s;
        wrap.to_s = in.loop_start_s;
        out.push_back(wrap);
        ++plan.wraps_planned;
        // One wrap at most: the loop is longer than the window.
        const double rest = std::min(hi - in.loop_end_s, loop_len);
        const std::int64_t wrapped = wall(in.loop_end_s);
        move(in.loop_start_s, in.loop_start_s + rest, wrapped,
             wrapped + std::llround(rest / speed * 1e9));
        cursor_s = in.loop_start_s + rest;
        return plan;
    }

    double cap = in.duration_s;
    if (in.hold_at_s && *in.hold_at_s > position && in.wraps_ahead == 0) {
        cap = std::min(cap, *in.hold_at_s);
    }
    const double to = std::min(hi, cap);
    if (to > cursor_s) {
        move(cursor_s, to, wall(cursor_s), wall(to));
        cursor_s = to;
    }
    return plan;
}

} // namespace etude
