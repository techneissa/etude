// SPDX-License-Identifier: GPL-3.0-or-later
// Writes a release's checked tarball (core read_tar) into a new folder: the one place an update's
// files meet the file system. The folder is made by the root helper, below a folder only root can
// write to, so nothing here races anyone. Everything it writes was listed by read_tar, so no path
// leaves the folder; it still creates every file with O_EXCL and O_NOFOLLOW, makes links last, and
// fails on any chmod that does not work (a file that kept a mode it should not have is an error, not
// a thing to carry on past). No SDL.
#pragma once

#include "etude/tar_archive.hpp"

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace etude::helper {

struct UnpackResult {
    bool ok = false;
    std::string reason; // when not ok: "Could not write bin/etude: No space left on device"
};

// Creates `dest` (it must not exist) and writes the entries of `archive`, whose file bytes are in
// `tar`, below it: folders 0755, files 0755 or 0644 as listed, links as listed. On a failure what
// was written is left for the caller to remove.
UnpackResult unpack_tar(const TarArchive& archive, std::span<const std::uint8_t> tar,
                        const std::filesystem::path& dest);

} // namespace etude::helper
