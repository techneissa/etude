// SPDX-License-Identifier: GPL-3.0-or-later
// Builds tar archives in memory for the tests of the update's archive checks, and wraps bytes in a
// gzip member made of one stored (uncompressed) block, so a test needs no compressor.
#pragma once

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace tar_test {

// Builds a tar archive in memory, entry by entry, with real checksums.
class Tar {
  public:
    Tar& add(const std::string& name, char type = '0', const std::string& data = "",
             const std::string& link = "", unsigned mode = 0644, const std::string& prefix = "",
             bool posix = false) {
        std::vector<std::uint8_t> h(512, 0);
        const auto put = [&](std::size_t at, const std::string& s, std::size_t max) {
            std::memcpy(h.data() + at, s.data(), std::min(s.size(), max));
        };
        const auto octal = [&](std::size_t at, std::size_t width, std::uint64_t v) {
            char buf[32];
            std::snprintf(buf, sizeof buf, "%0*llo", static_cast<int>(width - 1),
                          static_cast<unsigned long long>(v));
            std::memcpy(h.data() + at, buf, width - 1);
        };
        put(0, name, 100);
        octal(100, 8, mode);
        octal(108, 8, 0);
        octal(116, 8, 0);
        octal(124, 12, data.size());
        octal(136, 12, 0);
        h[156] = static_cast<std::uint8_t>(type);
        put(157, link, 100);
        std::memcpy(h.data() + 257, posix ? "ustar\0" "00" : "ustar  ", 8);
        if (posix) {
            put(345, prefix, 155);
        }
        std::memset(h.data() + 148, ' ', 8);
        unsigned sum = 0;
        for (const std::uint8_t b : h) {
            sum += b;
        }
        char buf[16];
        std::snprintf(buf, sizeof buf, "%06o", sum);
        std::memcpy(h.data() + 148, buf, 6);
        h[154] = 0;
        h[155] = ' ';
        bytes_.insert(bytes_.end(), h.begin(), h.end());
        bytes_.insert(bytes_.end(), data.begin(), data.end());
        bytes_.insert(bytes_.end(), (512 - data.size() % 512) % 512, 0);
        return *this;
    }
    Tar& dir(const std::string& name) { return add(name + "/", '5', "", "", 0755); }
    Tar& file(const std::string& name, const std::string& data = "x", unsigned mode = 0644) {
        return add(name, '0', data, "", mode);
    }
    Tar& symlink(const std::string& name, const std::string& target) {
        return add(name, '2', "", target, 0777);
    }
    std::vector<std::uint8_t> done() const {
        std::vector<std::uint8_t> out = bytes_;
        out.insert(out.end(), 1024, 0);
        return out;
    }
    const std::vector<std::uint8_t>& raw() const { return bytes_; }

  private:
    std::vector<std::uint8_t> bytes_;
};

// A gzip member holding `data` in stored blocks (at most 65535 bytes each), with a real CRC-32.
inline std::vector<std::uint8_t> gzip_stored(const std::vector<std::uint8_t>& data) {
    std::vector<std::uint8_t> out{0x1f, 0x8b, 8, 0, 0, 0, 0, 0, 0, 0xff};
    std::size_t at = 0;
    do {
        const std::size_t n = std::min<std::size_t>(65535, data.size() - at);
        const bool last = at + n == data.size();
        out.push_back(last ? 1 : 0);
        out.push_back(static_cast<std::uint8_t>(n & 0xff));
        out.push_back(static_cast<std::uint8_t>(n >> 8));
        out.push_back(static_cast<std::uint8_t>(~n & 0xff));
        out.push_back(static_cast<std::uint8_t>((~n >> 8) & 0xff));
        out.insert(out.end(), data.begin() + static_cast<long>(at),
                   data.begin() + static_cast<long>(at + n));
        at += n;
    } while (at < data.size());
    std::uint32_t crc = 0xffffffffu;
    for (const std::uint8_t b : data) {
        crc ^= b;
        for (int k = 0; k < 8; ++k) {
            crc = (crc >> 1) ^ (0xedb88320u & (~(crc & 1) + 1));
        }
    }
    crc = ~crc;
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(crc >> (8 * i)));
    }
    for (int i = 0; i < 4; ++i) {
        out.push_back(static_cast<std::uint8_t>(data.size() >> (8 * i)));
    }
    return out;
}

} // namespace tar_test
