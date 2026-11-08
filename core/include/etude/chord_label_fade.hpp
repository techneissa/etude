// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace etude {

// How a hand's chord label changes (a pure state machine, one per hand): the name and its line
// fade out where they were and the next ones fade in over their own keys, with no gliding. A
// label also fades out while the section band hangs over the strip, and comes back when it goes.
// Reduced effects switch at once.
inline constexpr std::int64_t kChordFadeNs = 140'000'000;

// A change inside another fade lets the earlier name finish its own: up to this many names fade out
// at once; a change past that drops the faintest.
inline constexpr std::size_t kChordMaxLeaving = 2;

// What is drawn: a label of the hand's timeline (by index) and how opaque it is, 0..1.
struct ChordLabelAlpha {
    int index = -1;
    float alpha = 0.0f;
};

class ChordLabelFade {
  public:
    // `index` is the label at the playhead in the hand's timeline (-1: none); `visible` is false
    // while the band hangs over the strip. Call once a frame with the frame's clock.
    void update(int index, bool visible, std::int64_t now_ns, bool reduced_effects);
    // The timeline was replaced (another song, hands or transposition): its indices mean other
    // labels, so nothing fades out and the label at the playhead fades in.
    void reset();
    // The labels fading out, then the one fading in or shown; only those with an alpha over 0.
    // At most kChordMaxLeaving + 1.
    std::span<const ChordLabelAlpha> items() const { return {items_.data(), count_}; }

  private:
    int arriving_ = -1;
    float arriving_alpha_ = 0.0f;
    std::array<ChordLabelAlpha, kChordMaxLeaving> leaving_{}; // index -1: empty
    std::int64_t last_ns_ = 0;
    bool started_ = false;
    std::array<ChordLabelAlpha, kChordMaxLeaving + 1> items_{};
    std::size_t count_ = 0;
};

} // namespace etude
