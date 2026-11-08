// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/replay_marks.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

void replay_bar_marks(const ReplayTimeline& timeline, double position_ms, double ahead_s,
                      double behind_s, std::vector<ReplayBarMark>& out) {
    out.clear();
    if (timeline.notes.empty() || !(ahead_s > 0.0) || !std::isfinite(position_ms)) {
        return;
    }
    const double lo_ms = position_ms - behind_s * 1000.0;
    const double hi_ms = position_ms + ahead_s * 1000.0;
    // A bar that starts before the stretch may still reach into it: look back by the longest bar.
    const double first_ms = lo_ms - static_cast<double>(timeline.max_length_ms);
    const auto from = std::lower_bound(
        timeline.notes.begin(), timeline.notes.end(), first_ms,
        [](const ReplayNote& n, double t) { return static_cast<double>(n.time_ms) < t; });
    for (auto it = from; it != timeline.notes.end() && static_cast<double>(it->time_ms) < hi_ms;
         ++it) {
        const double start = static_cast<double>(it->time_ms);
        const double end = start + static_cast<double>(replay_length_ms(*it));
        if (end <= lo_ms) {
            continue;
        }
        ReplayBarMark m;
        m.key = it->key;
        m.bottom = static_cast<float>((start - position_ms) / (ahead_s * 1000.0));
        m.top = static_cast<float>((end - position_ms) / (ahead_s * 1000.0));
        m.mark = it->mark;
        m.hand = it->hand;
        const bool graded_off = it->mark == ReplayMark::Early || it->mark == ReplayMark::Late;
        if (graded_off && std::abs(it->offset_ms) >= kLeanMinOffsetMs) {
            m.lean = it->offset_ms < 0 ? -1 : 1;
        }
        out.push_back(m);
    }
    if (out.size() > kMaxReplayMarks) {
        // Keep the ones nearest the hit line: the earliest in time.
        out.resize(kMaxReplayMarks);
    }
}

} // namespace etude
