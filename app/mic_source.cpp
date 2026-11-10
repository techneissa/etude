// SPDX-License-Identifier: GPL-3.0-or-later
#include "mic_source.hpp"

#include "wav_file.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <filesystem>

namespace etude::app {
namespace {

constexpr std::int64_t kReportNs = 5'000'000'000;
constexpr std::int64_t kRetryNs = 3'000'000'000;
constexpr std::int64_t kFloorWaitNs = 500'000'000;
// The meter falls by this much of its range per second.
constexpr float kMeterFallPerS = 1.6f;

double ms(std::int64_t ns) { return static_cast<double>(ns) / 1e6; }

} // namespace

MicSource::MicSource(MicSourceOptions options) : options_(std::move(options)) {}

MicSource::~MicSource() {
    close();
    if (audio_subsystem_) {
        SDL_QuitSubSystem(SDL_INIT_AUDIO);
    }
}

void MicSource::close() {
    if (feeder_) {
        feeder_->stop();
        feeder_.reset();
    }
    capture_.close();
    if (pipeline_) {
        pipeline_->stop();
        pipeline_.reset();
    }
    calibrating_ = false;
    awaiting_floor_ = false;
    view_.capturing = false;
    view_.calibrating = false;
    view_.level = 0.0f;
}

void MicSource::open(const AppSettings& s, std::int64_t now_ns) {
    MicPipelineConfig config;
    config.a4_hz = static_cast<float>(s.mic_a4_tenths) / 10.0f;
    config.learn_tuning = s.mic_learn_tuning;
    config.noise_floor_rms = static_cast<float>(s.mic_noise_floor_ppm) / 1e6f;
    auto pipeline = std::make_unique<MicPipeline>();

    std::string error;
    std::string description;
    if (!options_.wav_path.empty()) {
        WavData wav;
        if (read_wav_file(options_.wav_path, wav, error)) {
            pipeline->start(config);
            feeder_ = std::make_unique<WavFeeder>();
            feeder_->start(*pipeline,
                           resample_linear(wav.mono, wav.sample_rate, config.input_rate));
            description = "Playing " + std::filesystem::path(options_.wav_path).filename().string();
        }
    } else {
        if (!audio_subsystem_) {
            audio_subsystem_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
            if (!audio_subsystem_) {
                error = SDL_GetError();
            }
        }
        if (audio_subsystem_) {
            SDL_AudioDeviceID id = 0;
            for (const MicDevice& d : list_mic_devices()) {
                if (!s.mic_device.empty() && d.name == s.mic_device) {
                    id = d.id;
                }
            }
            // The pipeline takes the device's own channel count and rate, so SDL has no channel or
            // rate conversion to do in the callback (a non-float sample format is still converted
            // there).
            if (MicCapture::device_format(id, &config.input_rate, &config.input_channels, &error)) {
                pipeline->start(config);
            }
            if (pipeline->running() && capture_.open(*pipeline, id, &error)) {
                description = "Listening to " + capture_.device_name();
                if (options_.log) {
                    std::printf(
                        "microphone: %s, %d Hz, %d channel%s, %d frames per buffer; analysis "
                        "reads blocks of %d frames, mixed down to mono%s\n",
                        capture_.device_name().c_str(), capture_.device_rate(),
                        capture_.device_channels(), capture_.device_channels() == 1 ? "" : "s",
                        capture_.device_frames(), MicPipeline::kBlock,
                        config.input_rate == kDetectorRate ? "" : ", resampled to 48000 Hz");
                    std::fflush(stdout);
                }
            }
        }
    }
    if (description.empty()) {
        pipeline->stop();
        open_error_ = error.empty() ? "no microphone" : error;
        retry_at_ns_ = now_ns + kRetryNs;
        view_.status = "Can't open the microphone: " + open_error_;
        return;
    }
    pipeline_ = std::move(pipeline);
    open_error_.clear();
    open_device_ = s.mic_device;
    learn_applied_ = s.mic_learn_tuning;
    // A new detector has learned nothing, whatever the last one had.
    inbox_.bridge().reset_tuning(s.mic_learn_tuning);
    a4_tenths_applied_ = s.mic_a4_tenths;
    pushed_expected_.clear();
    view_.status = description;
    view_.capturing = true;
    view_ns_ = now_ns;
    if (options_.log && !options_.wav_path.empty()) {
        std::printf("microphone: feeding %s\n", options_.wav_path.c_str());
        std::fflush(stdout);
    }
}

void MicSource::apply(const AppSettings& s, std::int64_t now_ns) {
    mode_ = static_cast<int>(s.input_mode);
    view_.mic_wanted = s.input_mode != InputMode::Midi;
    const bool want = view_.mic_wanted;
    if (!want) {
        if (pipeline_ || !open_error_.empty()) {
            // Switching to MIDI only releases the device, so no permission is asked again.
            inbox_.release_all(now_ns); // the keys the microphone holds are let go
            close();
            open_error_.clear();
        }
        view_.status = "Microphone off";
        view_.a4_hz = static_cast<float>(s.mic_a4_tenths) / 10.0f;
        view_.floor_level = mic_meter_fraction(static_cast<float>(s.mic_noise_floor_ppm) / 1e6f);
        return;
    }
    if (pipeline_ && options_.wav_path.empty() && s.mic_device != open_device_) {
        inbox_.release_all(now_ns);
        close(); // another microphone was chosen
    }
    if (!pipeline_) {
        if (open_error_.empty() || now_ns >= retry_at_ns_) {
            open(s, now_ns);
        }
        if (!pipeline_) {
            view_.a4_hz = static_cast<float>(s.mic_a4_tenths) / 10.0f;
            return;
        }
    }

    if (s.mic_learn_tuning != learn_applied_) {
        pipeline_->set_learn_tuning(s.mic_learn_tuning);
        if (!s.mic_learn_tuning) {
            // Learning off keeps the A4 that was learned (it is in the settings).
            pipeline_->set_a4_hz(static_cast<float>(s.mic_a4_tenths) / 10.0f);
            a4_tenths_applied_ = s.mic_a4_tenths;
        }
        learn_applied_ = s.mic_learn_tuning;
        inbox_.bridge().set_learning_tuning(s.mic_learn_tuning);
    }
    if (s.mic_a4_tenths != a4_tenths_applied_) {
        pipeline_->set_a4_hz(static_cast<float>(s.mic_a4_tenths) / 10.0f);
        a4_tenths_applied_ = s.mic_a4_tenths;
    }
    if (learn_applied_) {
        const int tenths = std::clamp(static_cast<int>(std::lround(pipeline_->a4_hz() * 10.0f)),
                                      kMinMicA4Tenths, kMaxMicA4Tenths);
        if (tenths != a4_tenths_applied_) {
            a4_tenths_applied_ = tenths;
            a4_tenths_learned_ = tenths;
            a4_dirty_ = true;
        }
    }
    view_.a4_hz = pipeline_->a4_hz();

    // The meter: the loudest hop since the last frame, falling slowly.
    const float dt = static_cast<float>(std::max<std::int64_t>(now_ns - view_ns_, 0)) / 1e9f;
    view_ns_ = now_ns;
    view_.level =
        std::max(mic_meter_fraction(pipeline_->take_level()), view_.level - kMeterFallPerS * dt);
    view_.floor_level = mic_meter_fraction(pipeline_->noise_floor_rms());

    if (calibrating_) {
        const std::int64_t elapsed = now_ns - calibration_start_ns_;
        view_.calibration = std::clamp(
            static_cast<float>(elapsed) / static_cast<float>(kCalibrationNs), 0.0f, 1.0f);
        if (elapsed >= kCalibrationNs) {
            floor_before_ = pipeline_->noise_floor_rms();
            pipeline_->end_noise_calibration();
            calibrating_ = false;
            awaiting_floor_ = true;
            awaiting_since_ns_ = now_ns;
        }
    }
    if (awaiting_floor_) {
        const float floor = pipeline_->noise_floor_rms();
        if (floor != floor_before_ || now_ns - awaiting_since_ns_ > kFloorWaitNs) {
            awaiting_floor_ = false;
            new_floor_ = floor;
        }
    }
    view_.calibrating = calibrating_;
    if (!calibrating_) {
        view_.calibration = 0.0f;
    }
}

bool MicSource::write_back(AppSettings& s) {
    bool changed = false;
    if (reset_a4_) {
        reset_a4_ = false;
        a4_dirty_ = false;
        s.mic_a4_tenths = kDefaultMicA4Tenths;
        changed = true;
    } else if (a4_dirty_) {
        a4_dirty_ = false;
        s.mic_a4_tenths = a4_tenths_learned_;
        changed = true;
    }
    if (new_floor_ >= 0.0f) {
        const int ppm =
            std::clamp(static_cast<int>(std::lround(new_floor_ * 1e6f)), 1, kMaxMicNoiseFloorPpm);
        new_floor_ = -1.0f;
        s.mic_noise_floor_ppm = ppm;
        changed = true;
    }
    return changed;
}

void MicSource::set_expected(std::span<const int> notes) {
    if (!pipeline_) {
        expected_.clear();
        return;
    }
    std::vector<std::int8_t> next;
    for (const int n : notes) {
        if (static_cast<int>(next.size()) < kMaxChordNotes && n >= 0 && n < 128) {
            next.push_back(static_cast<std::int8_t>(n));
        }
    }
    if (next == pushed_expected_) {
        return;
    }
    pushed_expected_ = next;
    std::array<int, kMaxChordNotes> arr{};
    for (std::size_t i = 0; i < next.size(); ++i) {
        arr[i] = next[i];
    }
    pipeline_->set_expected(arr.data(), static_cast<int>(next.size()));
}

void MicSource::note_midi_press(int note, std::int64_t time_ns) {
    inbox_.bridge().note_midi_press(note, time_ns);
}

void MicSource::poll(std::int64_t now_ns, std::vector<MidiEvent>& events,
                     std::vector<UnsureMark>& marks) {
    if (!pipeline_) {
        inbox_.flush(events); // the releases of a microphone that has just closed
        return;
    }
    inbox_.take([this](MicEventIn& in) { return pipeline_->pop(in); }, now_ns, events, marks);
}

const std::vector<std::string>& MicSource::devices() {
    if (devices_dirty_) {
        refresh_devices();
    }
    return device_names_;
}

void MicSource::refresh_devices() {
    devices_dirty_ = false;
    if (!audio_subsystem_) {
        audio_subsystem_ = SDL_InitSubSystem(SDL_INIT_AUDIO);
    }
    devices_.clear();
    device_names_.clear();
    if (audio_subsystem_) {
        devices_ = list_mic_devices();
    }
    for (const MicDevice& d : devices_) {
        device_names_.push_back(d.name);
    }
}

void MicSource::calibrate_noise(std::int64_t now_ns) {
    if (!pipeline_ || calibrating_) {
        return;
    }
    pipeline_->begin_noise_calibration();
    calibrating_ = true;
    calibration_start_ns_ = now_ns;
    view_.calibrating = true;
    view_.calibration = 0.0f;
}

void MicSource::reset_tuning() { reset_a4_ = true; }

void MicSource::log_presented(std::int64_t now_ns, bool to_screen) {
    for (const std::int64_t onset : inbox_.pending_onsets()) {
        latency_.add(now_ns - onset);
    }
    inbox_.pending_onsets().clear();
    if (!options_.log || !pipeline_) {
        return;
    }
    if (next_report_ns_ == 0) {
        next_report_ns_ = now_ns + kReportNs;
    }
    if (now_ns < next_report_ns_) {
        return;
    }
    next_report_ns_ = now_ns + kReportNs;
    const LatencySummary l = latency_.take_summary();
    const MicStats st = pipeline_->take_stats();
    const double budget_ms = 1000.0 * st.block_samples / kDetectorRate;
    if (l.count > 0) {
        std::printf(
            "mic latency onset to %s: %zu notes, min %.1f ms, median %.1f ms, max %.1f ms\n",
            to_screen ? "present" : "app", l.count, ms(l.min_ns), ms(l.median_ns), ms(l.max_ns));
    } else {
        std::printf("mic latency onset to %s: no notes\n", to_screen ? "present" : "app");
    }
    std::printf("mic analysis: %llu blocks, worst %.3f ms, mean %.3f ms (a block lasts %.2f ms); "
                "dropped %llu samples in %llu blocks, %llu events, %llu stamps; not sure %llu; "
                "level floor %.1f dBFS; A4 %.1f Hz\n",
                static_cast<unsigned long long>(st.blocks), ms(st.worst_block_ns),
                ms(st.mean_block_ns), budget_ms,
                static_cast<unsigned long long>(st.dropped_samples),
                static_cast<unsigned long long>(st.dropped_blocks),
                static_cast<unsigned long long>(st.dropped_events),
                static_cast<unsigned long long>(st.dropped_stamps),
                static_cast<unsigned long long>(inbox_.unsure_seen()),
                pipeline_->noise_floor_rms() > 0.0f
                    ? 20.0 * std::log10(static_cast<double>(pipeline_->noise_floor_rms()))
                    : -999.0,
                static_cast<double>(pipeline_->a4_hz()));
    std::fflush(stdout);
}

} // namespace etude::app
