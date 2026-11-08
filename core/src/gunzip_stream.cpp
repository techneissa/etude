// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/gunzip_stream.hpp"

#include "miniz.h"

#include <array>
#include <vector>

namespace etude {

namespace {

constexpr std::uint8_t kFlagHeaderCrc = 0x02;
constexpr std::uint8_t kFlagExtra = 0x04;
constexpr std::uint8_t kFlagName = 0x08;
constexpr std::uint8_t kFlagComment = 0x10;
constexpr std::uint8_t kFlagReserved = 0xE0;
constexpr std::size_t kInputBytes = 64 * 1024;
constexpr std::size_t kDictBytes = TINFL_LZ_DICT_SIZE;

// The compressed input, a piece at a time, with the bytes of the header and trailer handed out one
// by one.
class Input {
  public:
    explicit Input(const GunzipRead& read) : read_(read), buffer_(kInputBytes) {}

    bool failed() const { return failed_; }
    const std::uint8_t* data() const { return buffer_.data() + at_; }
    std::size_t left() const { return end_ - at_; }
    void consume(std::size_t n) { at_ += n; }

    // More input when none is left; false at the end of the input or when it cannot be read.
    bool fill() {
        if (at_ < end_) {
            return true;
        }
        at_ = end_ = 0;
        const std::ptrdiff_t n = read_(buffer_);
        if (n < 0) {
            failed_ = true;
            return false;
        }
        end_ = static_cast<std::size_t>(n) <= buffer_.size() ? static_cast<std::size_t>(n) : 0;
        return end_ > 0;
    }

    bool next(std::uint8_t& out) {
        if (!fill()) {
            return false;
        }
        out = buffer_[at_++];
        return true;
    }

    bool skip(std::size_t count) {
        for (std::uint8_t ignore = 0; count > 0; --count) {
            if (!next(ignore)) {
                return false;
            }
        }
        return true;
    }

    // Skips a zero-terminated field.
    bool skip_text() {
        for (std::uint8_t b = 1; b != 0;) {
            if (!next(b)) {
                return false;
            }
        }
        return true;
    }

  private:
    const GunzipRead& read_;
    std::vector<std::uint8_t> buffer_;
    std::size_t at_ = 0;
    std::size_t end_ = 0;
    bool failed_ = false;
};

GunzipError cut_short(const Input& in) {
    return in.failed() ? GunzipError::ReadFailed : GunzipError::Truncated;
}

// The 10-byte header and the optional fields after it.
GunzipError skip_header(Input& in) {
    std::array<std::uint8_t, 10> header{};
    for (std::size_t i = 0; i < header.size(); ++i) {
        if (!in.next(header[i])) {
            return cut_short(in);
        }
    }
    if (header[0] != 0x1F || header[1] != 0x8B || header[2] != 8 || (header[3] & kFlagReserved) != 0) {
        return GunzipError::NotGzip;
    }
    const std::uint8_t flags = header[3];
    bool ok = true;
    if ((flags & kFlagExtra) != 0) {
        std::uint8_t low = 0;
        std::uint8_t high = 0;
        ok = in.next(low) && in.next(high) &&
             in.skip(static_cast<std::size_t>(low) | (static_cast<std::size_t>(high) << 8));
    }
    ok = ok && ((flags & kFlagName) == 0 || in.skip_text());
    ok = ok && ((flags & kFlagComment) == 0 || in.skip_text());
    ok = ok && ((flags & kFlagHeaderCrc) == 0 || in.skip(2));
    return ok ? GunzipError::None : cut_short(in);
}

// The deflate data: wrapping dictionary, each piece out is checked, counted and handed on.
GunzipError inflate_member(Input& in, const GunzipWrite& write, std::uint64_t max_bytes,
                           std::uint32_t& crc, std::uint64_t& total) {
    tinfl_decompressor inflater;
    tinfl_init(&inflater);
    std::vector<std::uint8_t> dict(kDictBytes);
    std::size_t dict_at = 0;
    crc = static_cast<std::uint32_t>(MZ_CRC32_INIT);
    total = 0;
    for (;;) {
        std::size_t in_bytes = in.left();
        std::size_t out_bytes = dict.size() - dict_at;
        const tinfl_status status = tinfl_decompress(&inflater, in.data(), &in_bytes, dict.data(),
                                                     dict.data() + dict_at, &out_bytes,
                                                     TINFL_FLAG_HAS_MORE_INPUT);
        in.consume(in_bytes);
        if (out_bytes > 0) {
            total += out_bytes;
            if (total > max_bytes) {
                return GunzipError::TooLarge;
            }
            crc = static_cast<std::uint32_t>(mz_crc32(crc, dict.data() + dict_at, out_bytes));
            if (!write({dict.data() + dict_at, out_bytes})) {
                return GunzipError::WriteFailed;
            }
            dict_at = (dict_at + out_bytes) & (dict.size() - 1);
        }
        if (status == TINFL_STATUS_DONE) {
            return GunzipError::None;
        }
        if (status < 0) {
            return GunzipError::Corrupt;
        }
        if (status == TINFL_STATUS_NEEDS_MORE_INPUT && !in.fill()) {
            return cut_short(in);
        }
    }
}

// The trailer: the CRC-32 and the size (modulo 2^32) of what came out, and then nothing.
GunzipError check_trailer(Input& in, std::uint32_t crc, std::uint64_t total) {
    std::array<std::uint8_t, 8> trailer{};
    for (std::uint8_t& b : trailer) {
        if (!in.next(b)) {
            return cut_short(in);
        }
    }
    const auto word = [&](std::size_t from) {
        return static_cast<std::uint32_t>(trailer[from]) |
               (static_cast<std::uint32_t>(trailer[from + 1]) << 8) |
               (static_cast<std::uint32_t>(trailer[from + 2]) << 16) |
               (static_cast<std::uint32_t>(trailer[from + 3]) << 24);
    };
    if (word(0) != crc || word(4) != static_cast<std::uint32_t>(total & 0xFFFFFFFFu)) {
        return GunzipError::Corrupt;
    }
    if (in.fill()) {
        return GunzipError::Corrupt; // something after the member
    }
    return in.failed() ? GunzipError::ReadFailed : GunzipError::None;
}

} // namespace

GunzipError gunzip_stream(const GunzipRead& read, const GunzipWrite& write, std::uint64_t max_bytes) {
    Input in(read);
    if (const GunzipError error = skip_header(in); error != GunzipError::None) {
        return error;
    }
    std::uint32_t crc = 0;
    std::uint64_t total = 0;
    if (const GunzipError error = inflate_member(in, write, max_bytes, crc, total);
        error != GunzipError::None) {
        return error;
    }
    return check_trailer(in, crc, total);
}

} // namespace etude
