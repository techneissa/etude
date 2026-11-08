// SPDX-License-Identifier: GPL-3.0-or-later
#include "safe_io.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <cstring>

namespace etude::helper {

namespace {

constexpr std::size_t kChunk = 256 * 1024;
constexpr int kMaxDepth = 64;

bool write_all(int fd, const char* data, std::size_t size, int& error) {
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errno;
            return false;
        }
        data += n;
        size -= static_cast<std::size_t>(n);
    }
    return true;
}

bool remove_tree_depth(int dir_fd, const std::string& name, int depth, std::string& error);

bool remove_tree_below(int dir_fd, int depth, std::string& error) {
    if (depth > kMaxDepth) {
        error = "folders nested too deep";
        return false;
    }
    const int dup_fd = ::fcntl(dir_fd, F_DUPFD_CLOEXEC, 3);
    DIR* dir = dup_fd >= 0 ? ::fdopendir(dup_fd) : nullptr;
    if (dir == nullptr) {
        if (dup_fd >= 0) {
            ::close(dup_fd);
        }
        error = errno_text(errno);
        return false;
    }
    std::vector<std::string> names;
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    ::closedir(dir);
    for (const std::string& name : names) {
        if (!remove_tree_depth(dir_fd, name, depth, error)) {
            return false;
        }
    }
    return true;
}

bool remove_tree_depth(int dir_fd, const std::string& name, int depth, std::string& error) {
    struct stat st{};
    if (::fstatat(dir_fd, name.c_str(), &st, AT_SYMLINK_NOFOLLOW) != 0) {
        if (errno == ENOENT) {
            return true;
        }
        error = name + ": " + errno_text(errno);
        return false;
    }
    if (!S_ISDIR(st.st_mode)) {
        if (::unlinkat(dir_fd, name.c_str(), 0) != 0 && errno != ENOENT) {
            error = name + ": " + errno_text(errno);
            return false;
        }
        return true;
    }
    const int raw = ::openat(dir_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (raw < 0) {
        error = name + ": " + errno_text(errno);
        return false;
    }
    const Fd inner(raw);
    if (!remove_tree_below(inner.get(), depth + 1, error)) {
        return false;
    }
    if (::unlinkat(dir_fd, name.c_str(), AT_REMOVEDIR) != 0 && errno != ENOENT) {
        error = name + ": " + errno_text(errno);
        return false;
    }
    return true;
}

} // namespace

bool plain_name(std::string_view name) {
    if (name.empty() || name == "." || name == "..") {
        return false;
    }
    for (const char c : name) {
        if (c == '/' || static_cast<unsigned char>(c) < 0x20 || c == 0x7F) {
            return false;
        }
    }
    return true;
}

std::string errno_text(int error) { return std::strerror(error); }

const char* open_status_text(OpenStatus status) {
    switch (status) {
    case OpenStatus::Ok:
        return "ok";
    case OpenStatus::Missing:
        return "it is not there";
    case OpenStatus::NotPlain:
        return "it is not a plain file";
    case OpenStatus::TooLarge:
        return "it is too large";
    case OpenStatus::WrongOwner:
        return "it is not the app's own file";
    case OpenStatus::Failed:
        return "it could not be opened";
    }
    return "";
}

PlainFile open_plain_file(int dir_fd, const std::string& name, const FileLimits& limits) {
    PlainFile out;
    if (dir_fd != AT_FDCWD && !plain_name(name)) {
        out.error = EINVAL;
        return out;
    }
    const int raw = ::openat(dir_fd, name.c_str(),
                             O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (raw < 0) {
        out.error = errno;
        out.status = errno == ENOENT ? OpenStatus::Missing
                     : errno == ELOOP ? OpenStatus::NotPlain
                                      : OpenStatus::Failed;
        return out;
    }
    out.fd.reset(raw);
    struct stat st{};
    if (::fstat(raw, &st) != 0) {
        out.error = errno;
        out.status = OpenStatus::Failed;
        out.fd.reset();
        return out;
    }
    out.size = static_cast<std::uint64_t>(st.st_size);
    if (!S_ISREG(st.st_mode)) {
        out.status = OpenStatus::NotPlain;
    } else if (out.size > limits.max_bytes) {
        out.status = OpenStatus::TooLarge;
    } else if ((limits.check_owner && (st.st_uid != limits.owner || st.st_nlink != 1)) ||
               (!limits.allow_group_other_write && (st.st_mode & (S_IWGRP | S_IWOTH)) != 0)) {
        out.status = OpenStatus::WrongOwner;
    } else {
        out.status = OpenStatus::Ok;
        return out;
    }
    out.fd.reset();
    return out;
}

bool read_all(int fd, std::uint64_t max_bytes, std::vector<std::uint8_t>& out, std::string& error) {
    out.clear();
    std::vector<std::uint8_t> chunk(kChunk);
    for (;;) {
        const ssize_t n = ::read(fd, chunk.data(), chunk.size());
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            error = errno_text(errno);
            return false;
        }
        if (n == 0) {
            return true;
        }
        if (out.size() + static_cast<std::size_t>(n) > max_bytes) {
            error = "it is larger than it may be";
            return false;
        }
        out.insert(out.end(), chunk.begin(), chunk.begin() + n);
    }
}

bool read_all_text(int fd, std::uint64_t max_bytes, std::string& out, std::string& error) {
    std::vector<std::uint8_t> bytes;
    if (!read_all(fd, max_bytes, bytes, error)) {
        return false;
    }
    out.assign(bytes.begin(), bytes.end());
    return true;
}

bool copy_to_new_file(int src, int dir_fd, const std::string& name, std::uint64_t max_bytes,
                      std::string& error) {
    if (!plain_name(name)) {
        error = "not a plain file name";
        return false;
    }
    const int raw = ::openat(dir_fd, name.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (raw < 0) {
        error = name + ": " + errno_text(errno);
        return false;
    }
    Fd out(raw);
    std::vector<char> chunk(kChunk);
    std::uint64_t total = 0;
    bool ok = true;
    int code = 0;
    while (ok) {
        const ssize_t n = ::read(src, chunk.data(), chunk.size());
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n < 0) {
            code = errno;
            ok = false;
        } else if (n == 0) {
            break;
        } else if ((total += static_cast<std::uint64_t>(n)) > max_bytes) {
            error = name + ": it is larger than it may be";
            ok = false;
            code = -1;
        } else {
            ok = write_all(out.get(), chunk.data(), static_cast<std::size_t>(n), code);
        }
    }
    ok = ok && ::fsync(out.get()) == 0;
    if (!ok && code == 0) {
        code = errno;
    }
    out.reset();
    if (!ok) {
        if (code != -1) {
            error = name + ": " + errno_text(code);
        }
        ::unlinkat(dir_fd, name.c_str(), 0);
        return false;
    }
    return true;
}

bool write_file_atomic(int dir_fd, const std::string& name, std::string_view data, mode_t mode,
                       std::string& error) {
    if (!plain_name(name)) {
        error = "not a plain file name";
        return false;
    }
    const std::string temp = "." + name + "." + std::to_string(::getpid()) + ".part";
    ::unlinkat(dir_fd, temp.c_str(), 0);
    const int raw = ::openat(dir_fd, temp.c_str(),
                             O_WRONLY | O_CREAT | O_EXCL | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (raw < 0) {
        error = name + ": " + errno_text(errno);
        return false;
    }
    Fd out(raw);
    int code = 0;
    bool ok = write_all(out.get(), data.data(), data.size(), code);
    ok = ok && ::fchmod(out.get(), mode) == 0;
    ok = ok && ::fsync(out.get()) == 0;
    if (!ok && code == 0) {
        code = errno;
    }
    out.reset();
    if (ok && ::renameat(dir_fd, temp.c_str(), dir_fd, name.c_str()) != 0) {
        code = errno;
        ok = false;
    }
    if (!ok) {
        error = name + ": " + errno_text(code);
        ::unlinkat(dir_fd, temp.c_str(), 0);
        return false;
    }
    sync_directory(dir_fd);
    return true;
}

Fd open_directory(const std::string& path, std::string& error) {
    const int raw = ::open(path.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (raw < 0) {
        error = path + ": " + errno_text(errno);
    }
    return Fd(raw);
}

Fd open_directory_at(int dir_fd, const std::string& name, std::string& error) {
    const int raw = ::openat(dir_fd, name.c_str(), O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC);
    if (raw < 0) {
        error = name + ": " + errno_text(errno);
    }
    return Fd(raw);
}

bool remove_tree_at(int dir_fd, const std::string& name, std::string& error) {
    return remove_tree_depth(dir_fd, name, 0, error);
}

bool sync_directory(int dir_fd) { return ::fsync(dir_fd) == 0; }

} // namespace etude::helper
