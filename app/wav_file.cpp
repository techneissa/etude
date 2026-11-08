// SPDX-License-Identifier: GPL-3.0-or-later
#include "wav_file.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <fstream>
#include <iterator>

namespace etude::app {
namespace {

std::uint32_t le32(const unsigned char* p) {
    return static_cast<std::uint32_t>(p[0]) | static_cast<std::uint32_t>(p[1]) << 8 |
           static_cast<std::uint32_t>(p[2]) << 16 | static_cast<std::uint32_t>(p[3]) << 24;
}
std::uint16_t le16(const unsigned char* p) { return static_cast<std::uint16_t>(p[0] | p[1] << 8); }

} // namespace

bool read_wav_file(const std::filesystem::path& path, WavData& wav, std::string& error) {
    std::ifstream in(path, std::ios::binary);
    std::vector<unsigned char> data((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (data.size() < 12 || std::memcmp(data.data(), "RIFF", 4) != 0 ||
        std::memcmp(data.data() + 8, "WAVE", 4) != 0) {
        error = "not a RIFF/WAVE file";
        return false;
    }
    int format = 0;
    int channels = 0;
    int bits = 0;
    size_t pos = 12;
    while (pos + 8 <= data.size()) {
        const std::uint32_t size = le32(data.data() + pos + 4);
        const unsigned char* body = data.data() + pos + 8;
        if (pos + 8 + size > data.size()) {
            break;
        }
        if (std::memcmp(data.data() + pos, "fmt ", 4) == 0 && size >= 16) {
            format = le16(body);
            channels = le16(body + 2);
            wav.sample_rate = static_cast<int>(le32(body + 4));
            bits = le16(body + 14);
            if (format == 0xFFFE && size >= 26) {
                format = le16(body + 24); // WAVE_FORMAT_EXTENSIBLE sub-format
            }
        } else if (std::memcmp(data.data() + pos, "data", 4) == 0) {
            if (channels <= 0 || bits <= 0) {
                error = "data before fmt";
                return false;
            }
            const int bytes = bits / 8;
            const size_t frames = size / static_cast<size_t>(bytes * channels);
            wav.mono.assign(frames, 0.0f);
            for (size_t f = 0; f < frames; ++f) {
                float sum = 0.0f;
                for (int c = 0; c < channels; ++c) {
                    const unsigned char* s =
                        body + (f * static_cast<size_t>(channels) + static_cast<size_t>(c)) *
                                   static_cast<size_t>(bytes);
                    float v = 0.0f;
                    if (format == 3 && bits == 32) {
                        std::uint32_t u = le32(s);
                        std::memcpy(&v, &u, 4);
                    } else if (format == 1 && bits == 16) {
                        v = static_cast<float>(static_cast<std::int16_t>(le16(s))) / 32768.0f;
                    } else if (format == 1 && bits == 24) {
                        const std::int32_t x =
                            static_cast<std::int32_t>(static_cast<std::uint32_t>(s[0]) << 8 |
                                                      static_cast<std::uint32_t>(s[1]) << 16 |
                                                      static_cast<std::uint32_t>(s[2]) << 24);
                        v = static_cast<float>(x) / 2147483648.0f;
                    } else if (format == 1 && bits == 32) {
                        v = static_cast<float>(static_cast<std::int32_t>(le32(s))) / 2147483648.0f;
                    } else {
                        error = "unsupported sample format";
                        return false;
                    }
                    sum += v;
                }
                wav.mono[f] = sum / static_cast<float>(channels);
            }
            return true;
        }
        pos += 8 + size + (size & 1u);
    }
    error = "no data chunk";
    return false;
}

std::vector<float> resample_linear(const std::vector<float>& in, int from, int to) {
    if (from == to || in.empty()) {
        return in;
    }
    const double ratio = static_cast<double>(from) / to;
    const auto n = static_cast<size_t>(static_cast<double>(in.size()) / ratio);
    std::vector<float> out(n);
    for (size_t i = 0; i < n; ++i) {
        const double x = static_cast<double>(i) * ratio;
        const auto k = static_cast<size_t>(x);
        const auto frac = static_cast<float>(x - static_cast<double>(k));
        const float a = in[std::min(k, in.size() - 1)];
        const float b = in[std::min(k + 1, in.size() - 1)];
        out[i] = a + (b - a) * frac;
    }
    return out;
}

} // namespace etude::app
