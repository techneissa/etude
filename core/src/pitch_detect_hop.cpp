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

// RMS over the last kGateHops hops. One hop is too short: a bass partial's level in 128
// samples swings with its phase, which would flap the gate open and shut.
float PitchDetector::Impl::gate_rms() {
    double sum = 0.0;
    for (int i = 0; i < kHop; ++i) {
        const float v = sample_at(written - kHop + i);
        sum += static_cast<double>(v) * v;
    }
    hop_energy[static_cast<size_t>(hop_index)] = sum;
    hop_index = (hop_index + 1) % kGateHops;
    double total = 0.0;
    for (double e : hop_energy) {
        total += e;
    }
    return static_cast<float>(std::sqrt(total / (kGateHops * kHop)));
}

// Mean positive change of log magnitude over the bins from 140 Hz to 6 kHz, against the
// previous hop, and the energy in those bins. Magnitudes are floored near the gate level so
// room noise adds little flux. The lowest bins are left out: a 512-sample frame cannot
// resolve bass partials, and their level swings with phase from hop to hop.
float PitchDetector::Impl::spectral_flux(float* energy) {
    copy_recent(onset_time.data(), written, kOnsetWindow);
    for (int i = 0; i < kOnsetWindow; ++i) {
        onset_time[static_cast<size_t>(i)] *= onset_hann[static_cast<size_t>(i)];
    }
    onset_fft.forward(kOnsetWindow, onset_time.data(), onset_spec.data());
    const float scale = 4.0f / static_cast<float>(kOnsetWindow); // sine amplitude, Hann
    const float floor = 0.5f * gate_threshold();
    const int top =
        std::min(kOnsetWindow / 2, static_cast<int>(6000LL * kOnsetWindow / cfg.sample_rate));
    float flux = 0.0f;
    float sum = 0.0f;
    constexpr int first = 2;
    for (int k = first; k <= top; ++k) {
        const auto& c = onset_spec[static_cast<size_t>(k)];
        const float linear = std::hypot(c.r, c.i) * scale;
        sum += linear * linear;
        const float mag = std::log(linear + floor);
        const float diff = mag - onset_prev[static_cast<size_t>(k)];
        flux += std::max(0.0f, diff);
        onset_prev[static_cast<size_t>(k)] = mag;
    }
    *energy = sum;
    return flux / static_cast<float>(top - first + 1);
}

// Places the onset within the last kOnsetLookBack samples: the first 32-sample block whose
// energy rises well above the blocks before the attack.
std::int64_t PitchDetector::Impl::locate_onset() const {
    constexpr int blocks = kOnsetLookBack / kOnsetBlock;
    std::array<float, blocks> e{};
    const std::int64_t start = written - kOnsetLookBack;
    for (int b = 0; b < blocks; ++b) {
        float sum = 0.0f;
        for (int i = 0; i < kOnsetBlock; ++i) {
            const float v = sample_at(start + b * kOnsetBlock + i);
            sum += v * v;
        }
        e[static_cast<size_t>(b)] = sum;
    }
    const float base = (e[0] + e[1] + e[2]) / 3.0f;
    const float peak = *std::max_element(e.begin(), e.end());
    if (peak < 2.0f * base) {
        return written - 256;
    }
    const float threshold = base + 0.2f * (peak - base);
    for (int b = 1; b < blocks; ++b) {
        if (e[static_cast<size_t>(b)] > threshold && e[static_cast<size_t>(b)] > 2.0f * base) {
            return start + b * kOnsetBlock;
        }
    }
    return written - 256;
}

void PitchDetector::Impl::hop() {
    const float rms = gate_rms();
    if (calibrating) {
        calib_sum += rms;
        ++calib_hops;
        return;
    }
    const float threshold = gate_threshold();
    bool opened = false;
    if (!gate_open && rms > threshold) {
        gate_open = true;
        opened = true;
        quiet_hops = 0;
    } else if (gate_open) {
        quiet_hops = rms < 0.6f * threshold ? quiet_hops + 1 : 0;
        if (quiet_hops >= 4) {
            gate_open = false;
        }
    }

    float energy = 0.0f;
    const float flux = spectral_flux(&energy);
    if (!gate_open) {
        if (state == State::Acquiring && written - onset >= kShortestUnsure) {
            not_sure();
        }
        end_current_note();
        state = State::Idle;
        flux_avg = 0.0f;
        energy_avg = energy;
        recent_energy[static_cast<size_t>(recent_index)] = energy;
        recent_index = (recent_index + 1) % static_cast<int>(recent_energy.size());
        return;
    }
    // A note onset is new spectral content (flux well above its recent level) arriving with
    // more energy. Small flux alone also comes from a fading note and noise; a large jump
    // counts on its own (legato into a note no louder than the last).
    // A bass note's period is longer than the 512-sample frame, so the frame's level swings
    // with the phase. The rise is measured against the loudest hop of the last 2048 samples
    // (over a full period of A0), and it may trail the flux spike by up to two hops.
    const bool refractory = written - last_onset < cfg.sample_rate * kOnsetRefractoryMs / 1000;
    const bool spike = flux > kOnsetFluxMin && flux > kOnsetFluxRise * flux_avg;
    if (spike && spike_hops == 0) {
        spike_hops = 3;
        spike_energy = *std::max_element(recent_energy.begin(), recent_energy.end());
    }
    const bool flux_onset = !refractory && ((spike && flux > 0.3f) ||
                                            (spike_hops > 0 && energy > 1.2f * spike_energy));
    spike_hops = flux_onset ? 0 : std::max(0, spike_hops - 1);
    if (opened || flux_onset) {
        start_note(locate_onset(), flux_onset);
    } else {
        flux_avg = 0.9f * flux_avg + 0.1f * flux;
    }
    energy_avg = 0.8f * energy_avg + 0.2f * energy;
    recent_energy[static_cast<size_t>(recent_index)] = energy;
    recent_index = (recent_index + 1) % static_cast<int>(recent_energy.size());

    switch (state) {
    case State::Acquiring:
        if (expected_count > 0) {
            expected_step();
        } else {
            acquire_step();
        }
        break;
    case State::Sounding:
        track_step();
        break;
    case State::Idle:
    case State::Done:
        break;
    }
}

} // namespace etude
