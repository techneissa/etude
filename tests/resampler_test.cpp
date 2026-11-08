// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/resampler.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <vector>

using namespace etude;

namespace {

std::vector<float> sine(double hz, int rate, int count, double amp = 0.5) {
    std::vector<float> v(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i) {
        v[static_cast<std::size_t>(i)] =
            static_cast<float>(amp * std::sin(6.283185307179586 * hz * i / rate));
    }
    return v;
}

std::vector<float> run(StreamResampler& r, const std::vector<float>& in, std::size_t chunk) {
    std::vector<float> out;
    std::vector<float> buf(r.max_output(chunk));
    for (std::size_t i = 0; i < in.size(); i += chunk) {
        const std::size_t n = std::min(chunk, in.size() - i);
        buf.resize(std::max(buf.size(), r.max_output(n)));
        const std::size_t got = r.process(in.data() + i, n, buf.data());
        REQUIRE(got <= r.max_output(n));
        out.insert(out.end(), buf.begin(), buf.begin() + static_cast<std::ptrdiff_t>(got));
    }
    return out;
}

} // namespace

TEST_CASE("resampler: 44.1 kHz to 48 kHz keeps the frequency, amplitude and length") {
    StreamResampler r(44100, 48000);
    const auto in = sine(440.0, 44100, 44100);
    const auto out = run(r, in, 256);
    CHECK(std::abs(static_cast<int>(out.size()) - 48000) <= 2);
    // Frequency: count rising zero crossings in the second half.
    int crossings = 0;
    const std::size_t half = out.size() / 2;
    for (std::size_t i = half + 1; i < out.size(); ++i) {
        crossings += out[i - 1] < 0.0f && out[i] >= 0.0f ? 1 : 0;
    }
    const double seconds = static_cast<double>(out.size() - half) / 48000.0;
    CHECK(crossings / seconds == doctest::Approx(440.0).epsilon(0.01));
    double peak = 0.0;
    for (std::size_t i = half; i < out.size(); ++i) {
        peak = std::max(peak, static_cast<double>(std::fabs(out[i])));
    }
    CHECK(peak == doctest::Approx(0.5).epsilon(0.02));
}

TEST_CASE("resampler: any chunking gives the same output") {
    const auto in = sine(1000.0, 32000, 8000);
    StreamResampler a(32000, 48000);
    StreamResampler b(32000, 48000);
    const auto whole = run(a, in, in.size());
    const auto pieces = run(b, in, 37);
    REQUIRE(whole.size() == pieces.size());
    for (std::size_t i = 0; i < whole.size(); ++i) {
        CHECK(whole[i] == doctest::Approx(pieces[i]).epsilon(1e-5));
    }
}

TEST_CASE("resampler: equal rates pass the samples through unchanged, across chunk boundaries") {
    std::vector<float> in(1000);
    for (std::size_t i = 0; i < in.size(); ++i) {
        in[i] = std::sin(0.37f * static_cast<float>(i)) * 0.8f + 0.001f * static_cast<float>(i % 7);
    }
    for (const std::size_t chunk : {std::size_t{1}, std::size_t{37}, std::size_t{256}, in.size()}) {
        StreamResampler r(48000, 48000);
        CHECK(r.max_output(chunk) >= chunk);
        const auto out = run(r, in, chunk);
        REQUIRE(out.size() == in.size());
        for (std::size_t i = 0; i < in.size(); ++i) {
            CHECK(out[i] == in[i]); // bit for bit, and no delay
        }
    }
}

TEST_CASE("resampler: down to a lower rate, and an invalid rate passes nothing") {
    StreamResampler r(96000, 48000);
    const auto out = run(r, sine(440.0, 96000, 96000), 480);
    CHECK(std::abs(static_cast<int>(out.size()) - 48000) <= 2);
    StreamResampler bad(0, 48000);
    float x = 0.0f;
    CHECK(bad.process(&x, 1, &x) == 0);
    CHECK(bad.max_output(100) == 0);
}
