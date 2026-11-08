// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/replay_keys.hpp"

#include <algorithm>

namespace etude {

void ReplayKeys::light(const ReplayNote& n) {
    if (n.mark == ReplayMark::Missed || n.key > 127) {
        return; // nobody played it
    }
    const std::size_t key = n.key;
    lit_[key] = {true, n.mark == ReplayMark::Wrong, n.velocity, n.hand};
    until_[key] = static_cast<std::int64_t>(n.time_ms) +
                  std::max(replay_length_ms(n), kMinReplayLengthMs);
}

void ReplayKeys::step(std::span<const ReplayNote> passed, std::int32_t position_ms) {
    for (std::size_t key = 0; key < lit_.size(); ++key) {
        if (lit_[key].on && until_[key] <= position_ms) {
            lit_[key] = {};
        }
    }
    for (const ReplayNote& n : passed) {
        light(n);
    }
}

void ReplayKeys::clear() {
    lit_ = {};
    until_.fill(0);
}

void ReplayKeys::seat(const ReplayTimeline& timeline, std::int32_t position_ms) {
    clear();
    // Notes that started up to the longest drawn length ago may still be lit.
    const std::int32_t reach = std::max(timeline.max_length_ms, kMinReplayLengthMs);
    const std::span<const ReplayNote> window =
        replay_window(timeline, position_ms - reach, position_ms + 1);
    for (const ReplayNote& n : window) {
        light(n);
        const std::size_t key = n.key;
        if (key < lit_.size() && lit_[key].on && until_[key] <= position_ms) {
            lit_[key] = {}; // over before the position
        }
    }
}

int ReplayKeys::lit_count() const {
    return static_cast<int>(std::count_if(lit_.begin(), lit_.end(),
                                          [](const ReplayLitKey& k) { return k.on; }));
}

} // namespace etude
