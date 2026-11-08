// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include "etude/run_replay.hpp"

#include <cstdint>
#include <vector>

namespace etude {

// What the screen draws over the song's falling notes for a replay at one moment: the played notes
// as narrow bars on the edge of their key's block, in the grade colours, the notes nobody played
// hatched, and the wrong keys. Fractions of the note area like a falling note's (0 is the hit line,
// 1 the top), so the scene places them with the same arithmetic.
//
// A bar that was played early lies below its note (the song clock was nearer the hit line when the
// key went down), one played late above it. `lean` says which: -1 early, +1 late, 0 for neither
// (on time, a wrong key, a missed note).
struct ReplayBarMark {
    std::uint8_t key = 0; // as played, after the run's transposition
    float bottom = 0.0f;
    float top = 0.0f;
    ReplayMark mark = ReplayMark::Missed;
    Hand hand = Hand::Unknown;
    std::int8_t lean = 0;

    friend bool operator==(const ReplayBarMark&, const ReplayBarMark&) = default;
};

// A mark leans when its grade says Early or Late and its offset from the note is at least this many
// milliseconds on the song clock: the grade is the verdict, the offset only says which way.
inline constexpr int kLeanMinOffsetMs = 1;

// Most marks one frame draws: a replay in view holds at most as many notes as a densest screen.
inline constexpr std::size_t kMaxReplayMarks = 1500;

// The marks of `timeline` in view when the replay is at `position_ms`, `ahead_s` song seconds above
// the hit line and `behind_s` below it, in the order of their time, appended to `out` (cleared
// first). A mark is in view when its bar overlaps that stretch of the song. Binary search to the
// first candidate: a long replay costs no more than a short one. At most kMaxReplayMarks (the
// nearest the hit line are kept).
void replay_bar_marks(const ReplayTimeline& timeline, double position_ms, double ahead_s,
                      double behind_s, std::vector<ReplayBarMark>& out);

} // namespace etude
