// SPDX-License-Identifier: GPL-3.0-or-later
// Exact (unstretched) harmonic test tones shared by the pitch tests.
#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <vector>

namespace pitch_test {

inline constexpr int kRate = 48000;
inline constexpr double kTau = 6.283185307179586;

// `amps[n-1]` is the amplitude of partial n; steady, exactly harmonic, silent before `start`.
inline std::vector<float> tone(double f0, const std::vector<double>& amps, int length,
                               int start = 0) {
    std::vector<float> out(static_cast<size_t>(length), 0.0f);
    for (int i = start; i < length; ++i) {
        const double t = static_cast<double>(i - start) / kRate;
        double v = 0.0;
        for (size_t n = 0; n < amps.size(); ++n) {
            v += amps[n] * std::sin(kTau * f0 * static_cast<double>(n + 1) * t +
                                    0.3 * static_cast<double>(n));
        }
        out[static_cast<size_t>(i)] = static_cast<float>(v);
    }
    return out;
}

// Piano-like partial amplitudes: `a`/n for partials 1..8, with the odd partials scaled by
// `odd_scale`. Scales between about 0.08 and 0.12 leave the octave test unable to decide between
// the note and the octave above.
inline std::vector<double> rich(double a, double odd_scale = 1.0) {
    std::vector<double> amps;
    for (int n = 1; n <= 8; ++n) {
        amps.push_back(n % 2 == 1 ? a * odd_scale / n : a / n);
    }
    return amps;
}

// Frequency of `note` (A4 = 440 Hz), raised by `cents`.
inline double grid_hz(int note, double cents = 0.0) {
    return 440.0 * std::exp2((note - 69) / 12.0 + cents / 1200.0);
}

// Fades `samples` linearly to silence over `fade` samples starting at `from`, silent after.
inline void fade_out(std::vector<float>& samples, int from, int fade) {
    for (int i = from; i < static_cast<int>(samples.size()); ++i) {
        const double k = static_cast<double>(i - from) / fade;
        samples[static_cast<size_t>(i)] *= static_cast<float>(std::max(0.0, 1.0 - k));
    }
}

// Adds the odd partials of the octave below `f0` (the ones `f0`'s own partials do not cover) to a
// tone made with the same `start`. At `level` near 0.45 they are strong enough that the octave
// below is neither ruled out nor the answer.
inline void add_sub_octave(std::vector<float>& samples, double f0, double level, int start) {
    std::vector<double> odd(15, 0.0);
    for (int n = 1; n <= 15; n += 2) {
        odd[static_cast<size_t>(n - 1)] = 0.3 * level / n;
    }
    const auto below = tone(0.5 * f0, odd, static_cast<int>(samples.size()), start);
    for (size_t i = 0; i < samples.size(); ++i) {
        samples[i] += below[i];
    }
}

// Deterministic white noise in [-amplitude, amplitude], silent before `start`.
inline std::vector<float> noise(int length, float amplitude, std::uint32_t seed, int start = 0) {
    std::vector<float> out(static_cast<size_t>(length), 0.0f);
    std::uint32_t state = seed;
    for (int i = start; i < length; ++i) {
        state = state * 1664525u + 1013904223u;
        out[static_cast<size_t>(i)] =
            (static_cast<float>(state >> 8) / 16777216.0f - 0.5f) * 2.0f * amplitude;
    }
    return out;
}

} // namespace pitch_test
