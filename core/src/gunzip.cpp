// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/tar_archive.hpp"

#include "miniz.h"

#include <cstring>

namespace etude {

namespace {

constexpr std::uint8_t kFlagHeaderCrc = 0x02;
constexpr std::uint8_t kFlagExtra = 0x04;
constexpr std::uint8_t kFlagName = 0x08;
constexpr std::uint8_t kFlagComment = 0x10;
constexpr std::uint8_t kFlagReserved = 0xE0;

GunzipResult fail(GunzipError error) {
    GunzipResult r;
    r.error = error;
    return r;
}

// The index just past a zero-terminated field starting at `at`, or `size` + 1 when it never ends.
std::size_t skip_text(std::span<const std::uint8_t> bytes, std::size_t at) {
    while (at < bytes.size() && bytes[at] != 0) {
        ++at;
    }
    return at < bytes.size() ? at + 1 : bytes.size() + 1;
}

} // namespace

GunzipResult gunzip(std::span<const std::uint8_t> gz, std::size_t max_bytes) {
    if (gz.size() < 10 || gz[0] != 0x1F || gz[1] != 0x8B || gz[2] != 8) {
        return fail(gz.size() < 10 ? GunzipError::Truncated : GunzipError::NotGzip);
    }
    const std::uint8_t flags = gz[3];
    if ((flags & kFlagReserved) != 0) {
        return fail(GunzipError::NotGzip);
    }
    std::size_t at = 10;
    if ((flags & kFlagExtra) != 0) {
        if (at + 2 > gz.size()) {
            return fail(GunzipError::Truncated);
        }
        at += 2 + (static_cast<std::size_t>(gz[at]) | (static_cast<std::size_t>(gz[at + 1]) << 8));
    }
    if ((flags & kFlagName) != 0) {
        at = skip_text(gz, at);
    }
    if ((flags & kFlagComment) != 0) {
        at = skip_text(gz, at);
    }
    if ((flags & kFlagHeaderCrc) != 0) {
        at += 2;
    }
    if (at + 8 > gz.size()) {
        return fail(GunzipError::Truncated);
    }
    const std::size_t trailer = gz.size() - 8;
    const auto word = [&](std::size_t from) {
        return static_cast<std::uint32_t>(gz[from]) | (static_cast<std::uint32_t>(gz[from + 1]) << 8) |
               (static_cast<std::uint32_t>(gz[from + 2]) << 16) |
               (static_cast<std::uint32_t>(gz[from + 3]) << 24);
    };
    const std::uint32_t crc = word(trailer);
    const std::uint32_t size = word(trailer + 4);
    if (size > max_bytes) {
        return fail(GunzipError::TooLarge);
    }
    GunzipResult r;
    r.data.resize(size);
    if (size == 0) {
        // An empty member is still a deflate stream of its own; read it the same way.
        r.data.resize(1);
    }
    tinfl_decompressor inflater;
    tinfl_init(&inflater);
    std::size_t in_bytes = trailer - at;
    std::size_t out_bytes = r.data.size();
    const tinfl_status status =
        tinfl_decompress(&inflater, gz.data() + at, &in_bytes, r.data.data(), r.data.data(),
                         &out_bytes, TINFL_FLAG_USING_NON_WRAPPING_OUTPUT_BUF);
    if (status == TINFL_STATUS_HAS_MORE_OUTPUT) {
        return fail(GunzipError::Corrupt); // more data than the trailer says
    }
    if (status == TINFL_STATUS_NEEDS_MORE_INPUT) {
        return fail(GunzipError::Truncated);
    }
    if (status != TINFL_STATUS_DONE || out_bytes != size || in_bytes != trailer - at) {
        return fail(GunzipError::Corrupt);
    }
    r.data.resize(size);
    if (static_cast<std::uint32_t>(mz_crc32(MZ_CRC32_INIT, r.data.data(), r.data.size())) != crc) {
        return fail(GunzipError::Corrupt);
    }
    return r;
}

} // namespace etude
