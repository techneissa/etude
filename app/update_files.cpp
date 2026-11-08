// SPDX-License-Identifier: GPL-3.0-or-later
#include "update_files.hpp"

#include "etude/update_handoff.hpp"

#include <cerrno>
#include <fstream>
#include <system_error>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>
#endif

namespace etude::app {

#if defined(_WIN32)
bool write_file_atomic(const std::filesystem::path& path, const std::string& text, int* error) {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".part";
    {
        std::ofstream out(tmp, std::ios::binary | std::ios::trunc);
        out << text;
        if (!out) {
            if (error != nullptr) {
                *error = EIO;
            }
            return false;
        }
    }
    std::filesystem::rename(tmp, path, ec);
    if (ec) {
        if (error != nullptr) {
            *error = ec.value();
        }
        std::filesystem::remove(tmp, ec);
        return false;
    }
    return true;
}
#else
bool write_file_atomic_with(const FileSteps& steps, const std::filesystem::path& path,
                            const std::string& text, int* error) {
    if (error != nullptr) {
        *error = 0;
    }
    const auto fail = [&](int why, const std::filesystem::path& part) {
        if (error != nullptr) {
            *error = why;
        }
        std::error_code ec;
        std::filesystem::remove(part, ec);
        return false;
    };
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    std::filesystem::path tmp = path;
    tmp += ".part";
    const int fd = ::open(tmp.c_str(), O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC, 0644);
    if (fd < 0) {
        return fail(errno, tmp);
    }
    int why = 0;
    for (std::size_t done = 0; why == 0 && done < text.size();) {
        const ssize_t n = steps.write(fd, text.data() + done, text.size() - done);
        if (n > 0) {
            done += static_cast<std::size_t>(n);
        } else {
            why = errno != 0 ? errno : EIO;
        }
    }
    if (why == 0 && steps.fsync(fd) != 0) {
        why = errno;
    }
    if (::close(fd) != 0 && why == 0) {
        why = errno;
    }
    if (why != 0) {
        return fail(why, tmp);
    }
    if (steps.rename(tmp.c_str(), path.c_str()) != 0) {
        return fail(errno, tmp);
    }
    const int dir = ::open(path.parent_path().c_str(), O_RDONLY | O_CLOEXEC);
    if (dir >= 0) {
        (void)steps.fsync(dir); // best effort: some file systems refuse it
        ::close(dir);
    }
    return true;
}

bool write_file_atomic(const std::filesystem::path& path, const std::string& text, int* error) {
    return write_file_atomic_with(FileSteps{}, path, text, error);
}
#endif

bool write_update_ready(const std::filesystem::path& update_dir, std::string_view version) {
    std::error_code ec;
    if (update_dir.empty() || !std::filesystem::is_directory(update_dir, ec)) {
        return false;
    }
    const std::string text = format_update_ready(version);
    return !text.empty() && write_file_atomic(update_dir / std::string(kUpdateReadyFile), text);
}

} // namespace etude::app
