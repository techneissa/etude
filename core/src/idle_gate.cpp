// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/idle_gate.hpp"

namespace etude {

void IdleGate::frame_drawn(std::int64_t now_ns, const IdleFrame& frame) {
    // A picture to compare with needs the frame before it to have one too.
    const bool changed = !frame.picture_known || !have_picture_ || frame.picture != picture_;
    have_picture_ = frame.picture_known;
    picture_ = frame.picture;
    woke_ = false;
    if (!started_ || frame.input || frame.busy || changed) {
        last_moving_ns_ = now_ns;
        started_ = true;
    }
}

bool IdleGate::idle(std::int64_t now_ns) const {
    return started_ && now_ns - last_moving_ns_ >= config_.settle_ns;
}

} // namespace etude
