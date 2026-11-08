// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/resampler.hpp"

#include <algorithm>
#include <cmath>

namespace etude {

void StreamResampler::reset(int in_rate_hz, int out_rate_hz) {
    passthrough_ = in_rate_hz > 0 && in_rate_hz == out_rate_hz;
    step_ = in_rate_hz > 0 && out_rate_hz > 0 ? static_cast<double>(in_rate_hz) / out_rate_hz : 0.0;
    phase_ = 0.0;
    for (float& v : h_) {
        v = 0.0f;
    }
}

std::size_t StreamResampler::max_output(std::size_t in_count) const {
    if (step_ <= 0.0) {
        return 0;
    }
    if (passthrough_) {
        return in_count;
    }
    return static_cast<std::size_t>(std::ceil(static_cast<double>(in_count) / step_)) + 2;
}

std::size_t StreamResampler::process(const float* in, std::size_t in_count, float* out) {
    if (step_ <= 0.0 || in == nullptr || out == nullptr) {
        return 0;
    }
    if (passthrough_) {
        std::copy(in, in + in_count, out);
        return in_count;
    }
    std::size_t written = 0;
    for (std::size_t i = 0; i < in_count; ++i) {
        h_[0] = h_[1];
        h_[1] = h_[2];
        h_[2] = h_[3];
        h_[3] = in[i];
        while (phase_ < 1.0) {
            const float t = static_cast<float>(phase_);
            const float a = -0.5f * h_[0] + 1.5f * h_[1] - 1.5f * h_[2] + 0.5f * h_[3];
            const float b = h_[0] - 2.5f * h_[1] + 2.0f * h_[2] - 0.5f * h_[3];
            const float c = -0.5f * h_[0] + 0.5f * h_[2];
            out[written++] = ((a * t + b) * t + c) * t + h_[1];
            phase_ += step_;
        }
        phase_ -= 1.0;
    }
    return written;
}

} // namespace etude
