// SPDX-License-Identifier: GPL-3.0-or-later
// Feeds a recording into a MicPipeline at the pace of real time, from its own thread, in place of
// the capture device (--mic-wav): the pipeline sees the same blocks and timestamps a microphone
// would give it.
#pragma once

#include "mic_pipeline.hpp"

#include <atomic>
#include <cstdint>
#include <thread>
#include <vector>

namespace etude::app {

class WavFeeder {
  public:
    // Samples per pushed block: 10 ms at 48 kHz.
    static constexpr int kBlock = 480;

    WavFeeder() = default;
    ~WavFeeder() { stop(); }
    WavFeeder(const WavFeeder&) = delete;
    WavFeeder& operator=(const WavFeeder&) = delete;

    // Takes `samples` at the pipeline's rate; `lead_in_s` of silence goes first (the noise gate
    // settles) and `tail_s` after, so the last note is confirmed.
    void start(MicPipeline& pipeline, std::vector<float> samples, double lead_in_s = 0.5,
               double tail_s = 1.5);
    void stop();
    bool finished() const { return finished_.load(std::memory_order_acquire); }

  private:
    void run(MicPipeline* pipeline);

    std::vector<float> samples_;
    std::thread thread_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> finished_{false};
};

} // namespace etude::app
