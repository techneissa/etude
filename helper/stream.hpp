// SPDX-License-Identifier: GPL-3.0-or-later
// A download is never held in memory whole: it is read from the helper's own copy in pieces. The
// signature is checked from its BLAKE2b-512 hash (the kind of signature minisign writes by default,
// which covers the hash and not the bytes), the gzip is unpacked into a file of the helper's own, and
// that file is mapped read-only so the tar reader and the unpacker work on it as on memory while the
// system keeps the pages on disk. Memory stays at a few hundred kilobytes whatever the size. Part of
// the root update helper (helper/main.cpp).
#pragma once

#include "etude/minisign.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>

namespace etude::helper {

// The size of one read from the copy.
inline constexpr std::size_t kStreamChunk = 256 * 1024;

// BLAKE2b-512 of the whole of `fd`, from its start whatever it was read to before. A SIGTERM to the helper ends it (`error` says "interrupted").
bool digest_file(int fd, Digest512& digest, std::string& error);

// Unpacks the gzip in `src` into a new file `name` (0600, never an existing one, no link followed) in
// the folder `dir_fd` (read from the start of `src`), at most `max_bytes` out. Removes the file again on any failure.
bool gunzip_to_file(int src, int dir_fd, const std::string& name, std::uint64_t max_bytes,
                    std::string& error);

// A file mapped read-only into memory, for as long as this lives.
class MappedFile {
  public:
    MappedFile() = default;
    ~MappedFile();
    MappedFile(const MappedFile&) = delete;
    MappedFile& operator=(const MappedFile&) = delete;

    // Maps the whole of `fd`. An empty file maps to an empty span.
    bool map(int fd, std::string& error);
    std::span<const std::uint8_t> bytes() const { return {static_cast<const std::uint8_t*>(at_), size_}; }

  private:
    void* at_ = nullptr;
    std::size_t size_ = 0;
};

} // namespace etude::helper
