// SPDX-License-Identifier: GPL-3.0-or-later
// File access for a program that runs as root and reads and writes in folders the app's user can
// write to. Everything the app can name is opened by one plain file name inside a folder that is
// already open, without following a link and without waiting on a pipe, and judged from the open
// descriptor (`fstat`), never from the path: a link, a FIFO, a device, a folder, a second hard link
// or another user's file is refused before a byte is read, and nothing can be swapped between the
// check and the read. Part of the root update helper (helper/main.cpp).
#pragma once

#include "fd.hpp"

#include <sys/types.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace etude::helper {

// "Not a plain file name": empty, `.`, `..`, a `/` or a control character.
bool plain_name(std::string_view name);

// The words for an errno, e.g. "No space left on device".
std::string errno_text(int error);

enum class OpenStatus : std::uint8_t {
    Ok,
    Missing,    // there is no such file
    NotPlain,   // a link, a pipe, a device, a folder: anything but a regular file
    TooLarge,   // over the limit
    WrongOwner, // another user's, or with more than one hard link
    Failed,     // the open itself failed (`error` says why)
};

struct FileLimits {
    std::uint64_t max_bytes = 0;
    bool check_owner = false; // the file must belong to `owner` and have one hard link
    uid_t owner = 0;
    bool allow_group_other_write = true; // false: refuse a file group or others can write
};

struct PlainFile {
    Fd fd;
    OpenStatus status = OpenStatus::Failed;
    int error = 0;
    std::uint64_t size = 0;
};

// Opens `name` in the folder `dir_fd` (AT_FDCWD for a path), read-only, with O_NOFOLLOW and
// O_NONBLOCK, and checks the open descriptor against `limits`. With `dir_fd` a descriptor `name`
// must be a plain file name.
PlainFile open_plain_file(int dir_fd, const std::string& name, const FileLimits& limits);

const char* open_status_text(OpenStatus status);

// Reads the whole of `fd` (at most `max_bytes`; more is an error) into `out`.
bool read_all(int fd, std::uint64_t max_bytes, std::vector<std::uint8_t>& out, std::string& error);
bool read_all_text(int fd, std::uint64_t max_bytes, std::string& out, std::string& error);

// Copies `src` to a new file `name` (mode 0600, never an existing one, no link followed) in the
// folder `dir_fd`, flushes it to disk and closes it; removes it again when anything fails.
bool copy_to_new_file(int src, int dir_fd, const std::string& name, std::uint64_t max_bytes,
                      std::string& error);

// Writes `data` to a new file beside `name` and renames it over `name`, flushed first: a reader sees
// the old file or the whole new one. A `name` that is a folder is left alone and reported.
bool write_file_atomic(int dir_fd, const std::string& name, std::string_view data, mode_t mode,
                       std::string& error);

// Opens a folder by path (the last part is not followed) or by name inside another.
Fd open_directory(const std::string& path, std::string& error);
Fd open_directory_at(int dir_fd, const std::string& name, std::string& error);

// Removes the file, link or folder `name` and all that is in it, below `dir_fd`, without following
// a link. A missing one is not an error.
bool remove_tree_at(int dir_fd, const std::string& name, std::string& error);

bool sync_directory(int dir_fd);

} // namespace etude::helper
