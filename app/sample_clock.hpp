// SPDX-License-Identifier: GPL-3.0-or-later
// Maps the audio device's sample count to the steady clock, so a note due at a steady-clock time
// can start on the sample that time falls on. The audio callback runs at uneven times, but the
// samples it writes play at an even pace, so the mapping is a line with the device's rate and one
// offset, which the callback times only nudge.
//
// A callback never runs early for the sample it writes: the device pulls a buffer when one has
// played, and the thread that fills it is woken late by the OS (and, after a stall, runs several
// back to back, each less late than the one before). So a callback's time is the line plus a wake
// delay that is never negative, and the line is the earliest of them: a callback that ran before
// the line moves it back at once, and one that ran after only lets it creep forward by kCreepNs,
// which is how it follows the two clocks drifting apart (up to about 700 ppm). A burst of late
// callbacks moves the line by that much each and no more.
//
// The offset is the device's own latency as well, which every song note shares, so it does not
// matter for the timing.
#pragma once

#include <algorithm>
#include <cstdint>

namespace etude::app {

class SampleClock {
  public:
    // How far the line moves forward in one callback when the callback ran after it.
    static constexpr std::int64_t kCreepNs = 2'000;

    void reset(int rate_hz) {
        rate_ = rate_hz > 0 ? rate_hz : 1;
        started_ = false;
    }

    // `now_ns`: when the callback ran (steady clock). `frames_before`: samples written before it.
    // Returns how far from the line it ran, positive when late.
    std::int64_t observe(std::int64_t now_ns, std::int64_t frames_before) {
        if (!started_) {
            started_ = true;
            anchor_frame_ = frames_before;
            anchor_ns_ = now_ns;
            return 0;
        }
        const std::int64_t predicted = time_of(frames_before);
        const std::int64_t error = now_ns - predicted;
        anchor_ns_ = error < 0 ? now_ns : predicted + std::min(error, kCreepNs);
        anchor_frame_ = frames_before;
        return error;
    }

    // The steady-clock time the line gives for the sample with this index.
    std::int64_t time_of(std::int64_t frame) const {
        return anchor_ns_ + (frame - anchor_frame_) * 1'000'000'000 / rate_;
    }

    bool started() const { return started_; }

  private:
    std::int64_t rate_ = 48000;
    bool started_ = false;
    std::int64_t anchor_frame_ = 0;
    std::int64_t anchor_ns_ = 0;
};

} // namespace etude::app
