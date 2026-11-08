// SPDX-License-Identifier: GPL-3.0-or-later
// Reads a WAV file for --mic-wav, which feeds a recording through the microphone path. File access
// stays here in app/, never in core.
#pragma once

#include <filesystem>
#include <string>
#include <vector>

namespace etude::app {

struct WavData {
    int sample_rate = 0;
    std::vector<float> mono;
};

// PCM 16/24/32-bit integer or 32-bit float, any channel count (averaged to mono). On failure
// returns false and says why in `error`.
bool read_wav_file(const std::filesystem::path& path, WavData& wav, std::string& error);

// Linear resampling; returns `in` unchanged when the rates are equal.
std::vector<float> resample_linear(const std::vector<float>& in, int from_hz, int to_hz);

} // namespace etude::app
