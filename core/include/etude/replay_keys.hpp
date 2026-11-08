// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_replay.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace etude {

// Which keys of the keyboard a replay lights, and how: each key the player pressed lights when the
// replay passes the note and stays lit for as long as the note's block is drawn
// (`replay_length_ms`), in the hand of the note it matched; a wrong key lights as a wrong key does
// in live play. A missed note lights nothing. Pure and fixed-size, so a frame allocates nothing: it
// is stepped with the notes a `ReplayPlayer` step passed, the same ones the player's own sound
// takes, and independent of that sound being on.
struct ReplayLitKey {
    bool on = false;
    bool wrong = false; // a key with no note for it: drawn as live play draws a wrong key
    std::uint8_t velocity = 0;
    Hand hand = Hand::Unknown;
};

class ReplayKeys {
  public:
    using Lit = std::array<ReplayLitKey, 128>;

    // The replay moved on to `position_ms`, passing `passed`: lets go of what is due, then lights
    // each key a passed note pressed. A key struck again while it is lit starts over.
    void step(std::span<const ReplayNote> passed, std::int32_t position_ms);
    // Every key dark (a seek, the end, the view closing, a song loading).
    void clear();
    // After a seek: dark, then the keys whose notes are mid-way at `position_ms` lit (a paused
    // replay shows the moment it stands at). Searches the notes that could still be sounding, by
    // binary search on the timeline.
    void seat(const ReplayTimeline& timeline, std::int32_t position_ms);

    const Lit& lit() const { return lit_; }
    int lit_count() const;

  private:
    void light(const ReplayNote& n);

    Lit lit_{};
    std::array<std::int64_t, 128> until_{};
};

} // namespace etude
