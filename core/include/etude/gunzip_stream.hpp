// SPDX-License-Identifier: GPL-3.0-or-later
// gunzip for input and output too large to hold: the caller supplies the compressed bytes in pieces
// and takes the decompressed bytes in pieces, and memory stays at a few tens of kilobytes whatever the
// size. The same refusals as gunzip (tar_archive.hpp): one member, the CRC-32 and size in its trailer
// checked, nothing after it, and a limit on what comes out (checked as it comes, since the size the
// trailer claims is not known until the end).
#pragma once

#include "etude/tar_archive.hpp"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>

namespace etude {

// Fills `into` with the next compressed bytes and returns how many (at most `into.size()`), 0 at the
// end of the input, a negative number when the input cannot be read.
using GunzipRead = std::function<std::ptrdiff_t(std::span<std::uint8_t> into)>;
// Takes the next decompressed bytes; false when they cannot be kept.
using GunzipWrite = std::function<bool(std::span<const std::uint8_t> piece)>;

// ReadFailed and WriteFailed say the caller's side gave up; the others are as for gunzip.
GunzipError gunzip_stream(const GunzipRead& read, const GunzipWrite& write, std::uint64_t max_bytes);

} // namespace etude
