// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <cstddef>

namespace etude {

// Streaming sample-rate converter for mono audio: cubic (Catmull-Rom) interpolation between
// input samples, carrying its state from one call to the next, so chunks of any size give the
// same output as one long call. Equal rates pass the samples through exactly, with no delay and
// no interpolation. No allocation, ever: the caller sizes the output buffer with
// max_output(). Meant for the microphone's analysis thread, where a device runs at a rate the
// pitch detector does not take.
class StreamResampler {
  public:
    StreamResampler() = default;
    // Output rate over input rate; both must be positive (otherwise the converter passes nothing).
    StreamResampler(int in_rate_hz, int out_rate_hz) { reset(in_rate_hz, out_rate_hz); }

    void reset(int in_rate_hz, int out_rate_hz);

    // The most samples process() can write for `in_count` input samples.
    std::size_t max_output(std::size_t in_count) const;

    // Converts `in_count` samples; writes at most max_output(in_count) to `out` and returns how
    // many.
    std::size_t process(const float* in, std::size_t in_count, float* out);

  private:
    bool passthrough_ = false; // equal rates: copy
    double step_ = 0.0;        // input samples per output sample
    double phase_ = 0.0;       // position of the next output between h_[1] and h_[2], in [0, 1)
    float h_[4] = {};          // the last four input samples, oldest first
};

} // namespace etude
