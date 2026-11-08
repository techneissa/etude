// SPDX-License-Identifier: GPL-3.0-or-later
// Reading a release's tarball for an update: gunzip it, then list what a tar archive holds and
// refuse anything an update has no business doing. Pure memory in, a checked list out: nothing is
// written here (app/update_unpack.hpp writes the files).
//
// The archive of a release is one top folder, `etude-<version>/`, with folders, plain files and
// links inside. An entry is refused when it:
//  - names an absolute path, a `.` or `..` part, an empty part, a backslash or a control character;
//  - lies outside the top folder, or is a second top-level name;
//  - is a hard link, a device, a named pipe or any other kind than file, folder and symbolic link;
//  - is a link with no target, an absolute target, or a target that leaves the top folder (resolved
//    from the folder the link is in);
//  - repeats a path, or lies below a file or link;
//  - makes the archive too large (entry count, one file, or all files together).
// A file's mode is cut to what a program needs (executable or not); set-uid, set-gid and sticky
// bits never come through.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace etude {

enum class GunzipError : std::uint8_t {
    None,
    NotGzip,   // not a gzip header, or a method other than deflate
    Truncated, // the header, the data or the trailer is cut off
    TooLarge,  // the size it says is over the limit, or the data runs past it
    Corrupt,   // the deflate data is bad, or the size or CRC-32 does not match the trailer
    ReadFailed,  // gunzip_stream: the caller could not supply the input
    WriteFailed, // gunzip_stream: the caller could not take the output
};

struct GunzipResult {
    std::vector<std::uint8_t> data;
    GunzipError error = GunzipError::None;

    bool ok() const { return error == GunzipError::None; }
};

// Decompresses one gzip member held in memory, at most `max_bytes` out.
GunzipResult gunzip(std::span<const std::uint8_t> gz, std::size_t max_bytes);

enum class TarKind : std::uint8_t { File, Directory, Symlink };

struct TarEntry {
    std::string path; // below the top folder, '/' separated; empty for the top folder itself
    TarKind kind = TarKind::File;
    std::uint32_t mode = 0644; // 0755 for a folder or an executable file, else 0644
    std::size_t offset = 0;    // a file's bytes: where they start in the archive
    std::size_t size = 0;      // and how many
    std::string link;          // a symbolic link's target
};

struct TarLimits {
    std::size_t max_entries = 100'000;
    std::size_t max_file_bytes = 768ULL * 1024 * 1024;
    std::size_t max_total_bytes = 1024ULL * 1024 * 1024;
};

enum class TarError : std::uint8_t {
    None,
    Truncated,       // the archive ends inside an entry
    BadHeader,       // a header whose checksum does not match, or a size that is not a number
    Unsupported,     // a hard link, a device, a pipe or another kind of entry
    BadPath,         // an absolute path, a `.` or `..` part, an empty part, a control character
    WrongTop,        // an entry outside the top folder
    BadLink,         // a link with no target, an absolute one, or one that leaves the top folder
    Duplicate,       // a path twice
    UnderFile,       // an entry below a file or a link
    TooBig,          // too many entries, a file or the total over the limits
    MissingTop,      // no entry at all
};

struct TarArchive {
    std::vector<TarEntry> entries; // in archive order, folders before what is in them
};

struct TarResult {
    TarArchive archive;
    TarError error = TarError::None;
    std::string path; // the path at fault, when there is one

    bool ok() const { return error == TarError::None; }
};

// Reads the headers of `tar` (the uncompressed bytes) and checks every entry. `top` is the one
// top folder name allowed ("etude-1.2.3"). Understands ustar, GNU (long names and links) and
// pax (path and linkpath) headers.
TarResult read_tar(std::span<const std::uint8_t> tar, std::string_view top,
                   const TarLimits& limits = {});

// A short sentence for the player or the log: "a link leaves the folder: share/x".
std::string tar_error_text(const TarResult& result);

} // namespace etude
