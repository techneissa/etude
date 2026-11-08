// SPDX-License-Identifier: GPL-3.0-or-later
#include "unpack.hpp"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstring>
#include <system_error>

namespace etude::helper {

namespace {

namespace fs = std::filesystem;

UnpackResult failure(const std::string& what, int error) {
    return {false, "Could not write " + what + ": " + std::strerror(error)};
}

UnpackResult failure(const std::string& what, const std::error_code& error) {
    return {false, "Could not write " + what + ": " + error.message()};
}

UnpackResult write_file(const fs::path& file, const TarEntry& entry,
                        std::span<const std::uint8_t> tar) {
    const int fd = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0) {
        return failure(entry.path, errno);
    }
    const std::uint8_t* at = tar.data() + entry.offset;
    std::size_t left = entry.size;
    while (left > 0) {
        const ssize_t n = ::write(fd, at, left);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            const int error = errno;
            ::close(fd);
            return failure(entry.path, error);
        }
        at += n;
        left -= static_cast<std::size_t>(n);
    }
    if (::fchmod(fd, static_cast<mode_t>(entry.mode)) != 0) {
        const int error = errno;
        ::close(fd);
        return failure(entry.path, error);
    }
    if (::close(fd) != 0) {
        return failure(entry.path, errno);
    }
    return {true, {}};
}

UnpackResult make_folder(const fs::path& folder, const std::string& what) {
    std::error_code ec;
    fs::create_directories(folder, ec);
    if (ec) {
        return failure(what, ec);
    }
    if (::chmod(folder.c_str(), 0755) != 0) {
        return failure(what, errno);
    }
    return {true, {}};
}

// The folders above `target` that the archive did not list are made too, and made 0755.
UnpackResult make_parents(const fs::path& dest, const fs::path& target) {
    for (fs::path folder = target.parent_path(); folder != dest && folder.has_relative_path();
         folder = folder.parent_path()) {
        std::error_code ec;
        if (fs::is_directory(folder, ec)) {
            continue;
        }
        const UnpackResult made = make_folder(folder, fs::relative(folder, dest, ec).string());
        if (!made.ok) {
            return made;
        }
    }
    return {true, {}};
}

} // namespace

UnpackResult unpack_tar(const TarArchive& archive, std::span<const std::uint8_t> tar,
                        const fs::path& dest) {
    std::error_code ec;
    const fs::file_status there = fs::symlink_status(dest, ec);
    if (there.type() != fs::file_type::not_found) {
        return {false, "Could not write " + dest.string() + ": it is already there"};
    }
    const UnpackResult top = make_folder(dest, dest.string());
    if (!top.ok) {
        return top;
    }
    for (const TarEntry& entry : archive.entries) {
        if (entry.path.empty() || entry.kind == TarKind::Symlink) {
            continue; // the top folder is `dest`; links come last
        }
        const fs::path target = dest / entry.path;
        if (entry.kind == TarKind::Directory) {
            const UnpackResult made = make_folder(target, entry.path);
            if (!made.ok) {
                return made;
            }
            continue;
        }
        const UnpackResult parents = make_parents(dest, target);
        if (!parents.ok) {
            return parents;
        }
        const UnpackResult written = write_file(target, entry, tar);
        if (!written.ok) {
            return written;
        }
    }
    for (const TarEntry& entry : archive.entries) {
        if (entry.kind != TarKind::Symlink || entry.path.empty()) {
            continue;
        }
        const fs::path target = dest / entry.path;
        const UnpackResult parents = make_parents(dest, target);
        if (!parents.ok) {
            return parents;
        }
        if (::symlink(entry.link.c_str(), target.c_str()) != 0) {
            return failure(entry.path, errno);
        }
    }
    return {true, {}};
}

} // namespace etude::helper
