// SPDX-License-Identifier: GPL-3.0-or-later
#include "mic_pipeline.hpp"
#include "crash_thread.hpp"

#include "fp_mode.hpp"
#include "loop_wake.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>

namespace etude::app {
namespace {

std::int64_t steady_ns() {
    using namespace std::chrono;
    return duration_cast<nanoseconds>(steady_clock::now().time_since_epoch()).count();
}

void atomic_max(std::atomic<float>& target, float value) {
    float current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

void atomic_max(std::atomic<std::int64_t>& target, std::int64_t value) {
    std::int64_t current = target.load(std::memory_order_relaxed);
    while (value > current &&
           !target.compare_exchange_weak(current, value, std::memory_order_relaxed)) {
    }
}

} // namespace

MicPipeline::MicPipeline() = default;

MicPipeline::~MicPipeline() { stop(); }

void MicPipeline::prepare(const MicPipelineConfig& config) {
    stop();
    config_ = config;
    config_.input_channels = std::max(config.input_channels, 1);
    config_.input_rate = std::max(config.input_rate, 1);
    const auto channels = static_cast<std::size_t>(config_.input_channels);
    ring_ =
        std::make_unique<SampleRing>(static_cast<std::size_t>(config_.input_rate) * channels / 4);
    accepted_ = 0;
    delivered_ = 0;
    interleaved_.assign(static_cast<std::size_t>(kBlock) * channels, 0.0f);
    mono_.assign(kBlock, 0.0f);
    resampler_.reset(config_.input_rate, kDetectorRate);
    const std::size_t converted =
        config_.input_rate == kDetectorRate ? 0 : resampler_.max_output(kBlock);
    converted_.assign(converted, 0.0f);
    const std::size_t block_out = config_.input_rate == kDetectorRate ? kBlock : converted;
    out_.assign(static_cast<std::size_t>(PitchDetector::kMaxEventsPerHop) *
                    (block_out / PitchDetector::kHop + 2),
                PitchEvent{});

    PitchDetectorConfig dc;
    dc.sample_rate = kDetectorRate;
    dc.method = config.method;
    dc.a4_hz = config.a4_hz;
    dc.learn_tuning = config.learn_tuning;
    detector_ = std::make_unique<PitchDetector>(dc);
    if (config.noise_floor_rms > 0.0f) {
        detector_->set_noise_floor_rms(config.noise_floor_rms);
    }
    a4_.store(config.a4_hz, std::memory_order_relaxed);
    floor_.store(detector_->noise_floor_rms(), std::memory_order_relaxed);
}

void MicPipeline::start_thread() {
    if (thread_.joinable() || !detector_) {
        return;
    }
    stop_.store(false, std::memory_order_release);
    thread_ = spawn_thread([this] { run(); });
}

void MicPipeline::start(const MicPipelineConfig& config) {
    prepare(config);
    start_thread();
}

void MicPipeline::stop() {
    if (!thread_.joinable()) {
        return;
    }
    stop_.store(true, std::memory_order_release);
    thread_.join();
    detector_.reset();
}

void MicPipeline::push(const float* samples, int frames, std::int64_t now_ns) noexcept {
    if (frames <= 0 || samples == nullptr || !ring_) {
        return;
    }
    const auto count =
        static_cast<std::size_t>(frames) * static_cast<std::size_t>(config_.input_channels);
    if (ring_->write(samples, count)) {
        // A stamp that does not fit is counted by the queue (MicStats::dropped_stamps).
        stamps_.push({accepted_, delivered_, frames, now_ns});
        accepted_ += frames;
    }
    delivered_ += frames;
}

void MicPipeline::send(const Command& c) {
    // A full queue means the thread is not running; the command is lost with it.
    commands_.push(c);
}

void MicPipeline::set_expected(const int* notes, int count) {
    Command c;
    c.kind = Command::Kind::Expected;
    c.count = std::clamp(count, 0, kMaxChordNotes);
    for (int i = 0; i < c.count; ++i) {
        c.notes[static_cast<std::size_t>(i)] = static_cast<std::int8_t>(notes[i]);
    }
    send(c);
}

void MicPipeline::begin_noise_calibration() {
    Command c;
    c.kind = Command::Kind::BeginCalibration;
    send(c);
}

void MicPipeline::end_noise_calibration() {
    Command c;
    c.kind = Command::Kind::EndCalibration;
    send(c);
}

void MicPipeline::set_noise_floor_rms(float rms) {
    Command c;
    c.kind = Command::Kind::NoiseFloor;
    c.value = rms;
    send(c);
}

void MicPipeline::set_learn_tuning(bool on) {
    Command c;
    c.kind = Command::Kind::LearnTuning;
    c.value = on ? 1.0f : 0.0f;
    send(c);
}

void MicPipeline::set_a4_hz(float a4_hz) {
    Command c;
    c.kind = Command::Kind::A4;
    c.value = a4_hz;
    send(c);
}

MicStats MicPipeline::take_stats() {
    MicStats s;
    s.blocks = blocks_.exchange(0, std::memory_order_relaxed);
    const std::int64_t total = total_ns_.exchange(0, std::memory_order_relaxed);
    s.worst_block_ns = worst_ns_.exchange(0, std::memory_order_relaxed);
    s.mean_block_ns = s.blocks > 0 ? total / static_cast<std::int64_t>(s.blocks) : 0;
    // The budget of one full block: the device frames it reads, as the detector's samples.
    s.block_samples = static_cast<int>(static_cast<std::int64_t>(kBlock) * kDetectorRate /
                                       std::max(config_.input_rate, 1));
    s.dropped_samples = ring_ ? ring_->dropped_samples() : 0;
    s.dropped_blocks = ring_ ? ring_->dropped_blocks() : 0;
    s.dropped_events = events_.dropped();
    s.dropped_stamps = stamps_.dropped();
    return s;
}

void MicPipeline::run() {
    enable_flush_to_zero();
    PitchDetector& detector = *detector_;
    const int channels = config_.input_channels;
    const bool resample = config_.input_rate != kDetectorRate;
    SampleClock clock;
    clock.reset(config_.input_rate);
    Stamp next{};
    bool have_next = false;
    std::int64_t offset = 0;   // stream frame minus accepted frame: frames dropped so far
    std::int64_t consumed = 0; // accepted frames handed on
    std::array<std::int8_t, kMaxChordNotes> expected{};
    int expected_count = 0;

    while (!stop_.load(std::memory_order_acquire)) {
        for (Command c; commands_.pop(c);) {
            switch (c.kind) {
            case Command::Kind::Expected: {
                expected_count = c.count;
                std::array<int, kMaxChordNotes> notes{};
                for (int i = 0; i < c.count; ++i) {
                    expected[static_cast<std::size_t>(i)] = c.notes[static_cast<std::size_t>(i)];
                    notes[static_cast<std::size_t>(i)] = c.notes[static_cast<std::size_t>(i)];
                }
                detector.set_expected_notes(notes.data(), c.count);
                break;
            }
            case Command::Kind::BeginCalibration:
                detector.begin_noise_calibration();
                break;
            case Command::Kind::EndCalibration:
                detector.end_noise_calibration();
                floor_.store(detector.noise_floor_rms(), std::memory_order_relaxed);
                break;
            case Command::Kind::NoiseFloor:
                detector.set_noise_floor_rms(c.value);
                floor_.store(detector.noise_floor_rms(), std::memory_order_relaxed);
                break;
            case Command::Kind::LearnTuning:
                detector.set_learn_tuning(c.value > 0.5f);
                a4_.store(detector.a4_hz(), std::memory_order_relaxed);
                break;
            case Command::Kind::A4:
                detector.set_a4_hz(c.value);
                a4_.store(detector.a4_hz(), std::memory_order_relaxed);
                break;
            }
        }

        const std::size_t got = ring_->read(interleaved_.data(), interleaved_.size());
        if (got == 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
            continue;
        }
        const auto frames = got / static_cast<std::size_t>(channels);
        // Blocks that started at or before the first frame of this chunk give the clock line and
        // the count of frames dropped before it.
        for (;;) {
            if (!have_next) {
                if (!stamps_.pop(next)) {
                    break;
                }
                have_next = true;
            }
            if (next.accepted_before > consumed) {
                break;
            }
            clock.observe(next.now_ns, next.stream_before + next.count);
            offset = next.stream_before - next.accepted_before;
            have_next = false;
        }

        // Mono: the first two channels averaged.
        const std::size_t mix = channels > 1 ? 2 : 1;
        double sum = 0.0;
        for (std::size_t f = 0; f < frames; ++f) {
            float v = interleaved_[f * static_cast<std::size_t>(channels)];
            if (mix == 2) {
                v = 0.5f * (v + interleaved_[f * static_cast<std::size_t>(channels) + 1]);
            }
            mono_[f] = v;
            sum += static_cast<double>(v) * static_cast<double>(v);
        }
        atomic_max(level_, static_cast<float>(std::sqrt(sum / static_cast<double>(frames))));
        const float* audio = mono_.data();
        std::size_t count = frames;
        if (resample) {
            count = resampler_.process(mono_.data(), frames, converted_.data());
            audio = converted_.data();
        }

        const std::int64_t t0 = steady_ns();
        const int found = detector.process(audio, static_cast<int>(count), out_.data(),
                                           static_cast<int>(out_.size()));
        const std::int64_t spent = steady_ns() - t0;
        consumed += static_cast<std::int64_t>(frames);
        blocks_.fetch_add(1, std::memory_order_relaxed);
        total_ns_.fetch_add(spent, std::memory_order_relaxed);
        atomic_max(worst_ns_, spent);
        a4_.store(detector.a4_hz(), std::memory_order_relaxed);

        // A detector sample index back to the device's frame count, then to the clock.
        const auto time_of = [&](std::int64_t detector_sample) {
            const std::int64_t frame = detector_sample * config_.input_rate / kDetectorRate;
            return clock.started() ? clock.time_of(frame + offset) : steady_ns();
        };
        for (int i = 0; i < found; ++i) {
            MicEventIn in;
            in.event = out_[static_cast<std::size_t>(i)];
            in.onset_ns = time_of(in.event.onset_sample);
            in.sample_ns = time_of(in.event.sample);
            in.expected = expected;
            in.expected_count = expected_count;
            events_.push(in);
            wake_loop(WakeSource::Mic); // the frame loop may be idle
        }
    }
}

} // namespace etude::app
