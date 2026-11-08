// SPDX-License-Identifier: GPL-3.0-or-later
// --latency-log: the time from each note-on to the frame that shows it.
#pragma once

#include "etude/latency_stats.hpp"

#include <cstdint>
#include <cstdio>
#include <vector>

namespace etude::app {

constexpr std::int64_t kLatencyReportNs = 5'000'000'000;

// --latency-log: time from each note-on's MIDI stamp to just after the SDL_RenderPresent of the
// first frame drawn with it, on the same clock. Covers queueing, the wait for the next frame,
// drawing and the present call; not the display's own scan-out and panel lag.
class LatencyLog {
  public:
    LatencyLog() { pending_.reserve(256); }

    void note_on(std::int64_t time_ns) {
        if (pending_.size() < pending_.capacity()) {
            pending_.push_back(time_ns);
        }
    }

    // Call right after SDL_RenderPresent.
    void presented(std::int64_t now_ns) {
        for (const std::int64_t t : pending_) {
            stats_.add(now_ns - t);
        }
        pending_.clear();
        if (next_report_ns_ == 0) {
            next_report_ns_ = now_ns + kLatencyReportNs;
        }
        if (now_ns < next_report_ns_) {
            return;
        }
        next_report_ns_ = now_ns + kLatencyReportNs;
        const etude::LatencySummary s = stats_.take_summary();
        if (s.count == 0) {
            return;
        }
        std::printf("latency note-on to present: %zu notes, min %.2f ms, median %.2f ms, max %.2f "
                    "ms\n",
                    s.count, static_cast<double>(s.min_ns) / 1e6,
                    static_cast<double>(s.median_ns) / 1e6, static_cast<double>(s.max_ns) / 1e6);
    }

  private:
    std::vector<std::int64_t> pending_;
    etude::LatencyStats stats_;
    std::int64_t next_report_ns_ = 0;
};

} // namespace etude::app
