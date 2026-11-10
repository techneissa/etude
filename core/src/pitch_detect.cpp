// SPDX-License-Identifier: GPL-3.0-or-later
#include "pitch_detect_impl.hpp"

#include "pitch_dsp.hpp"
#include "etude/pitch.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <vector>

namespace etude {

float note_from_hz(float hz, float a4_hz) {
    if (!(hz > 0.0f) || !(a4_hz > 0.0f)) {
        return -1.0f;
    }
    return static_cast<float>(kA4) + 12.0f * std::log2(hz / a4_hz);
}

int pitch_window_samples(int note, int sample_rate, float octave_check_periods) {
    const float period = static_cast<float>(sample_rate) / hz_of(note, 440.0f);
    const float periods = note - 12 >= kPianoLowNote ? octave_check_periods : 2.2f;
    return std::clamp(static_cast<int>(std::ceil(periods * period)), kMinFrame, kMaxFrame);
}

PitchDetector::Impl::Impl(const PitchDetectorConfig& c)
    : cfg(c), analyzer(c.sample_rate, kMaxFrame), comb_analyzer(c.sample_rate, kMaxFrame),
      before_analyzer(c.sample_rate, kBeforeSpectrum),
      after_analyzer(c.sample_rate, kBeforeSpectrum), checker(c.sample_rate), a4(c.a4_hz),
      a4_base(c.a4_hz) {
    cfg.low_note = std::clamp(cfg.low_note, 0, 127);
    cfg.high_note = std::clamp(cfg.high_note, cfg.low_note, 127);
    cfg.confirm_frames = std::max(1, cfg.confirm_frames);
    states = cfg.high_note - cfg.low_note + 2;
    delta.assign(static_cast<size_t>(states), 0.0f);
    obs.assign(static_cast<size_t>(states), 0.0f);
    next.assign(static_cast<size_t>(states), 0.0f);
    const float fs = static_cast<float>(cfg.sample_rate);
    min_lag = fs / (hz_of(cfg.high_note, 440.0f) * 1.06f);
    max_lag = std::min(kMaxFrame / 2,
                       static_cast<int>(std::ceil(fs / (hz_of(cfg.low_note, 440.0f) * 0.94f))));
    max_hz = std::min(0.45f * fs, kMaxPartialHz);
    for (int i = 0; i < kOnsetWindow; ++i) {
        onset_hann[static_cast<size_t>(i)] = static_cast<float>(
            0.5 - 0.5 * std::cos(6.283185307179586 * i / (kOnsetWindow - 1)));
    }
}

PitchDetector::PitchDetector(const PitchDetectorConfig& config)
    : impl_(std::make_unique<Impl>(config)) {}
PitchDetector::~PitchDetector() = default;
PitchDetector::PitchDetector(PitchDetector&&) noexcept = default;
PitchDetector& PitchDetector::operator=(PitchDetector&&) noexcept = default;

int PitchDetector::process(const float* samples, int count, PitchEvent* out, int capacity) {
    if (samples == nullptr || count <= 0) {
        return 0;
    }
    Impl& d = *impl_;
    d.out = out;
    d.capacity = out != nullptr ? std::max(0, capacity) : 0;
    d.emitted = 0;
    for (int i = 0; i < count; ++i) {
        const float v = samples[i];
        d.history[static_cast<size_t>(d.written & (kHistory - 1))] = std::isfinite(v) ? v : 0.0f;
        ++d.written;
        if (++d.since_hop == kHop) {
            d.since_hop = 0;
            d.hop();
        }
    }
    d.out = nullptr;
    return d.emitted;
}

void PitchDetector::reset() {
    Impl& d = *impl_;
    std::fill(d.history.begin(), d.history.end(), 0.0f);
    std::fill(d.onset_prev.begin(), d.onset_prev.end(), 0.0f);
    d.written = 0;
    d.since_hop = 0;
    d.gate_open = false;
    d.quiet_hops = 0;
    d.hop_energy.fill(0.0);
    d.hop_index = 0;
    d.flux_avg = 0.0f;
    d.energy_avg = 0.0f;
    d.spike_hops = 0;
    d.recent_energy.fill(0.0f);
    d.recent_index = 0;
    d.last_onset = std::numeric_limits<std::int64_t>::min() / 2;
    d.state = Impl::State::Idle;
    d.onset = 0;
    d.prev_note = -1;
    d.prev_period = 0.0f;
    d.current_note = -1;
    d.last_note = -1;
    d.last_period = 0.0f;
    d.reset_note_search();
}

std::int64_t PitchDetector::dropped_events() const { return impl_->dropped; }

void PitchDetector::begin_noise_calibration() {
    impl_->calibrating = true;
    impl_->calib_sum = 0.0;
    impl_->calib_hops = 0;
}

void PitchDetector::end_noise_calibration() {
    Impl& d = *impl_;
    d.calibrating = false;
    if (d.calib_hops > 0) {
        d.noise_floor = static_cast<float>(d.calib_sum / static_cast<double>(d.calib_hops));
    }
}

float PitchDetector::noise_floor_rms() const { return impl_->noise_floor; }
void PitchDetector::set_noise_floor_rms(float rms) { impl_->noise_floor = std::max(0.0f, rms); }

void PitchDetector::set_learn_tuning(bool on) {
    Impl& d = *impl_;
    d.cfg.learn_tuning = on;
    if (!on) {
        d.a4 = d.a4_base;
        d.tuning_count = 0;
        d.tuning_next = 0;
    }
}

void PitchDetector::set_a4_hz(float a4_hz) {
    if (a4_hz > 0.0f && std::isfinite(a4_hz)) {
        impl_->a4 = a4_hz;
        impl_->a4_base = a4_hz;
        impl_->tuning_count = 0;
        impl_->tuning_next = 0;
    }
}

float PitchDetector::a4_hz() const { return impl_->a4; }

void PitchDetector::set_expected_notes(const int* notes, int count) {
    Impl& d = *impl_;
    d.expected_count = 0;
    for (int i = 0; i < count && d.expected_count < kMaxChordNotes; ++i) {
        if (notes[i] >= d.cfg.low_note && notes[i] <= d.cfg.high_note) {
            d.expected[static_cast<size_t>(d.expected_count++)] = notes[i];
        }
    }
}

} // namespace etude
