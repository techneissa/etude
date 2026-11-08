// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/replay_sound.hpp"

#include <algorithm>
#include <cstdint>
#include <utility>

namespace etude {

bool ReplaySound::push(std::uint8_t key, std::uint8_t velocity) {
    if (count_ >= events_.size()) {
        return false;
    }
    events_[count_++] = {key, velocity};
    return true;
}

void ReplaySound::set_on(bool on) {
    if (on == on_) {
        return;
    }
    on_ = on;
    if (!on_) {
        silence();
    }
}

void ReplaySound::release(std::size_t key) {
    // A note-off that finds no room stays due and goes out with the next call.
    if (push(static_cast<std::uint8_t>(key), 0)) {
        release_at_[key] = kIdle;
    }
}

void ReplaySound::step(std::span<const ReplayNote> passed, std::int32_t position_ms) {
    // Let go first what is due, so a key struck again in this step starts clean.
    for (std::size_t key = 0; key < release_at_.size(); ++key) {
        if (release_at_[key] != kIdle && release_at_[key] <= position_ms) {
            release(key);
        }
    }
    if (!on_) {
        return;
    }
    for (const ReplayNote& n : passed) {
        if (n.velocity == 0 || n.mark == ReplayMark::Missed || n.key > 127) {
            continue;
        }
        // Note-ons stop short of the end of the queue: room is kept for every key's note-off.
        if (count_ + 2 > events_.size() - release_at_.size()) {
            continue;
        }
        const std::size_t key = n.key;
        if (release_at_[key] != kIdle) {
            release(key);
        }
        push(n.key, n.velocity);
        const std::int64_t end = static_cast<std::int64_t>(n.time_ms) +
                                 std::max(replay_length_ms(n), kMinReplayLengthMs);
        release_at_[key] = static_cast<std::int32_t>(std::min<std::int64_t>(end, INT32_MAX));
    }
}

void ReplaySound::silence() {
    for (std::size_t key = 0; key < release_at_.size(); ++key) {
        if (release_at_[key] != kIdle) {
            release(key);
        }
    }
}

std::span<const ReplaySoundEvent> ReplaySound::take() {
    const std::size_t n = std::exchange(count_, 0);
    return {events_.data(), n};
}

} // namespace etude
