// SPDX-License-Identifier: GPL-3.0-or-later
// The files the update code writes whole (a reader, or a power cut, sees the old file or the new
// one): the app's handoff to the root update helper in <prefix>/update/ (the downloaded tarball, its
// signature, the request, and the ready file a freshly installed version writes once its first frame
// is on screen and its history has opened and migrated), and the check's state. No SDL.
#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>

#if !defined(_WIN32)
#include <stdio.h>
#include <sys/types.h>
#include <unistd.h>
#endif

namespace etude::app {

#if !defined(_WIN32)
// The system calls the write goes through, so a test can make one of them fail.
struct FileSteps {
    ssize_t (*write)(int, const void*, std::size_t) = ::write;
    int (*fsync)(int) = ::fsync;
    int (*rename)(const char*, const char*) = ::rename;
};
#endif

// Writes `text` beside `path` (as `path` + ".part"), flushes it to disk, then renames it over `path`
// and flushes the folder. On a failure the old file is left as it was, the part is removed, and
// `*error` (when given) says why (an errno value).
bool write_file_atomic(const std::filesystem::path& path, const std::string& text,
                       int* error = nullptr);

#if !defined(_WIN32)
bool write_file_atomic_with(const FileSteps& steps, const std::filesystem::path& path,
                            const std::string& text, int* error = nullptr);
#endif

// Writes <update_dir>/ready for `version` (core format_update_ready); false when `update_dir` is not
// a folder or the file could not be written.
bool write_update_ready(const std::filesystem::path& update_dir, std::string_view version);

} // namespace etude::app
