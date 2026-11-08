// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_replay.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace etude {

// The player's own sound in a replay: each key they pressed sounds again when the replay passes it,
// for as long as the block is drawn, then lets go. Pure: it turns the notes a `ReplayPlayer` step
// passed into note-ons and note-offs for the app to send to whatever the player hears. Fixed
// storage, so a frame allocates nothing; a note-on that does not fit (a step that passes more
// notes than the queue holds, kept clear of the room every key's note-off needs) is dropped, never
// a note-off.
//
// A missed note is silent (nobody played it); a wrong key sounds, as it did.
struct ReplaySoundEvent {
    std::uint8_t key = 0;
    std::uint8_t velocity = 0; // 0 is a note-off
    friend bool operator==(const ReplaySoundEvent&, const ReplaySoundEvent&) = default;
};

class ReplaySound {
  public:
    static constexpr std::size_t kMaxEvents = 512;

    // The sound is on (the player's choice); off, nothing is queued and what sounds is let go.
    void set_on(bool on);
    bool on() const { return on_; }

    // The replay moved on to `position_ms`, passing `passed`: starts each audible note, lets go of
    // what is due. A key struck again while it still sounds is let go first.
    void step(std::span<const ReplayNote> passed, std::int32_t position_ms);
    // Lets go of everything that sounds (a seek, a pause, the end, the sound switched off).
    void silence();

    // The events since the last call, in order; valid until the next call that queues.
    std::span<const ReplaySoundEvent> take();

    bool sounding(int key) const { return key >= 0 && key < 128 && release_at_[static_cast<std::size_t>(key)] != kIdle;
    }

  private:
    static constexpr std::int32_t kIdle = INT32_MIN;
    bool push(std::uint8_t key, std::uint8_t velocity);
    void release(std::size_t key);

    bool on_ = true;
    std::array<std::int32_t, 128> release_at_ = [] {
        std::array<std::int32_t, 128> a{};
        a.fill(kIdle);
        return a;
    }();
    std::array<ReplaySoundEvent, kMaxEvents> events_{};
    std::size_t count_ = 0;
};

} // namespace etude
