// SPDX-License-Identifier: GPL-3.0-or-later
#include "wav_feeder.hpp"
#include "crash_thread.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace etude::app {

void WavFeeder::start(MicPipeline& pipeline, std::vector<float> samples, double lead_in_s,
                      double tail_s) {
    stop();
    const double rate = static_cast<double>(pipeline.sample_rate());
    // A recording that starts in room noise would step from the lead-in's digital silence into
    // it, which the detector hears as an onset. Fade such a start in over 100 ms; a recording that
    // starts with a note is left alone.
    const std::size_t fade = std::min(samples.size(), static_cast<std::size_t>(0.1 * rate));
    double energy = 0.0;
    for (std::size_t i = 0; i < fade; ++i) {
        energy += static_cast<double>(samples[i]) * static_cast<double>(samples[i]);
    }
    if (fade > 0 && std::sqrt(energy / static_cast<double>(fade)) < 0.003) {
        for (std::size_t i = 0; i < fade; ++i) {
            samples[i] *=
                0.5f *
                (1.0f - std::cos(3.14159265f * static_cast<float>(i) / static_cast<float>(fade)));
        }
    }
    samples_.assign(static_cast<std::size_t>(lead_in_s * rate), 0.0f);
    samples_.insert(samples_.end(), samples.begin(), samples.end());
    samples_.resize(samples_.size() + static_cast<std::size_t>(tail_s * rate), 0.0f);
    stop_.store(false, std::memory_order_release);
    finished_.store(false, std::memory_order_release);
    thread_ = spawn_thread([this, &pipeline] { run(&pipeline); });
}

void WavFeeder::stop() {
    if (thread_.joinable()) {
        stop_.store(true, std::memory_order_release);
        thread_.join();
    }
}

void WavFeeder::run(MicPipeline* pipeline) {
    using clock = std::chrono::steady_clock;
    const auto start = clock::now();
    const double rate = static_cast<double>(pipeline->sample_rate());
    std::size_t pos = 0;
    while (pos < samples_.size() && !stop_.load(std::memory_order_acquire)) {
        const std::size_t n = std::min<std::size_t>(kBlock, samples_.size() - pos);
        // A block is delivered when its last sample has been captured.
        const auto due = start + std::chrono::nanoseconds(static_cast<std::int64_t>(
                                     static_cast<double>(pos + n) / rate * 1e9));
        std::this_thread::sleep_until(due);
        const std::int64_t now =
            std::chrono::duration_cast<std::chrono::nanoseconds>(clock::now().time_since_epoch())
                .count();
        pipeline->push(samples_.data() + pos, static_cast<int>(n), now);
        pos += n;
    }
    finished_.store(true, std::memory_order_release);
}

} // namespace etude::app
