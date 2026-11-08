// SPDX-License-Identifier: GPL-3.0-or-later
// The path from audio samples to pitch events, with no SDL in it: a capture callback (or a test)
// pushes blocks in the device's own format (any channel count and rate, float) into a ring
// buffer, and an analysis thread drains it, mixes it down to mono, resamples it to the detector's
// rate when the device runs at another one, runs the pitch detector and queues events for the
// frame loop. Nothing is converted on the capture side: the callback only copies.
//
//   producer thread (capture callback)  --SampleRing + stamps-->  analysis thread
//   analysis thread  --event queue-->  frame loop (main thread)
//   frame loop  --command queue-->  analysis thread
//
// Every queue has one producer and one consumer. push() is wait-free: it copies the block, stamps
// it and returns; it never allocates, locks or logs. After prepare() neither thread allocates.
#pragma once

#include "sample_clock.hpp"

#include "etude/mic_input.hpp"
#include "etude/pitch_detect.hpp"
#include "etude/resampler.hpp"
#include "etude/sample_ring.hpp"
#include "etude/spsc_queue.hpp"

#include <array>
#include <atomic>
#include <cstdint>
#include <memory>
#include <thread>
#include <vector>

namespace etude::app {

// The rate the pitch detector is tuned for. A device at another rate is resampled to it on the
// analysis thread.
inline constexpr int kDetectorRate = 48000;

struct MicPipelineConfig {
    // The device's own format, as push() delivers it: frames per second and interleaved channels.
    // Only the first two channels are used; they are averaged to mono.
    int input_rate = 48000;
    int input_channels = 1;
    PitchMethod method = PitchMethod::Mpm;
    float a4_hz = 440.0f;
    bool learn_tuning = false;
    float noise_floor_rms = 0.0f; // 0: not calibrated
};

// One reporting window of the analysis thread's work.
struct MicStats {
    std::uint64_t blocks = 0;        // process() calls in the window
    std::int64_t worst_block_ns = 0; // the slowest one
    std::int64_t mean_block_ns = 0;
    int block_samples = 0;             // samples in a full block (the budget is this long)
    std::uint64_t dropped_samples = 0; // since start: the ring was full
    std::uint64_t dropped_blocks = 0;
    std::uint64_t dropped_events = 0; // since start: the event queue was full
    // Since start: a block's stamp did not fit its queue, so the clock line missed one update. The
    // next stamp mends it, but a drop of samples in the same stretch would shift onset times
    // until then.
    std::uint64_t dropped_stamps = 0;
};

class MicPipeline {
  public:
    // Device frames read per analysis block: 256 is 5.3 ms at 48 kHz.
    static constexpr int kBlock = 256;
    static constexpr std::size_t kEventQueue = 256;

    MicPipeline();
    ~MicPipeline();
    MicPipeline(const MicPipeline&) = delete;
    MicPipeline& operator=(const MicPipeline&) = delete;

    // Allocates everything the two threads use: the ring (at least 250 ms of the device's
    // frames, rounded up to a power of two), the converters, the detector. push() works from here
    // on, with no thread running: blocks pile up, and the ones that do not fit are dropped. Main
    // thread.
    void prepare(const MicPipelineConfig& config);
    // Starts the analysis thread; prepare() first.
    void start_thread();
    // prepare() and start_thread().
    void start(const MicPipelineConfig& config);
    // Stops and joins the thread. Safe to call twice.
    void stop();
    bool running() const { return thread_.joinable(); }
    // The device's rate: what a source of blocks has to deliver.
    int sample_rate() const { return config_.input_rate; }
    int input_channels() const { return config_.input_channels; }
    // Samples the ring holds (all channels), valid after prepare().
    std::size_t ring_capacity() const { return ring_ ? ring_->capacity() : 0; }

    // ---- Producer thread (the capture callback). Wait-free.

    // `frames` interleaved frames of input_channels samples each. `now_ns`: the steady-clock time
    // the block was delivered, i.e. when its last frame had just been captured.
    void push(const float* samples, int frames, std::int64_t now_ns) noexcept;

    // ---- Frame loop

    // Next event, with times on the steady clock. False when none is waiting.
    bool pop(MicEventIn& out) noexcept { return events_.pop(out); }

    // Samples (all channels) the analysis thread has yet to read.
    std::size_t backlog_samples() const { return ring_ ? ring_->available() : 0; }

    // Expected-note mode (up to kMaxChordNotes notes); count 0 returns to open detection.
    void set_expected(const int* notes, int count);
    void begin_noise_calibration();
    // The measured floor is published to noise_floor_rms() when the thread has handled this.
    void end_noise_calibration();
    void set_noise_floor_rms(float rms);
    void set_learn_tuning(bool on);
    void set_a4_hz(float a4_hz);

    // The loudest hop since the last call, as RMS (0..1).
    float take_level() { return level_.exchange(0.0f, std::memory_order_relaxed); }
    float a4_hz() const { return a4_.load(std::memory_order_relaxed); }
    float noise_floor_rms() const { return floor_.load(std::memory_order_relaxed); }
    // Starts a new reporting window.
    MicStats take_stats();

  private:
    struct Stamp {
        std::int64_t accepted_before = 0; // frames the ring had accepted before this block
        std::int64_t stream_before = 0;   // frames delivered before it, dropped ones included
        std::int64_t count = 0;           // frames in it
        std::int64_t now_ns = 0;
    };
    struct Command {
        enum class Kind : std::uint8_t {
            Expected,
            BeginCalibration,
            EndCalibration,
            NoiseFloor,
            LearnTuning,
            A4,
        } kind = Kind::Expected;
        float value = 0.0f;
        int count = 0;
        std::array<std::int8_t, kMaxChordNotes> notes{};
    };

    void run();
    void send(const Command& c);

    MicPipelineConfig config_;
    std::unique_ptr<SampleRing> ring_;
    SpscQueue<Stamp, 512> stamps_;
    SpscQueue<MicEventIn, kEventQueue> events_;
    SpscQueue<Command, 64> commands_;
    std::unique_ptr<PitchDetector> detector_;
    std::thread thread_;
    std::atomic<bool> stop_{false};

    // Analysis thread, all sized by prepare().
    StreamResampler resampler_;
    std::vector<float> interleaved_;
    std::vector<float> mono_;
    std::vector<float> converted_;
    std::vector<PitchEvent> out_;

    // Producer only, in frames.
    std::int64_t accepted_ = 0;
    std::int64_t delivered_ = 0;

    // Analysis thread -> others.
    std::atomic<float> level_{0.0f};
    std::atomic<float> a4_{440.0f};
    std::atomic<float> floor_{0.0f};
    std::atomic<std::uint64_t> blocks_{0};
    std::atomic<std::int64_t> total_ns_{0};
    std::atomic<std::int64_t> worst_ns_{0};
};

} // namespace etude::app
