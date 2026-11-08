// SPDX-License-Identifier: GPL-3.0-or-later
#include "handoff_files.hpp"

#include "safe_io.hpp"

#include "etude/update_handoff.hpp"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>
#include <string>

namespace etude::helper {

namespace {

static_assert(std::string_view(kRequestFile) == kUpdateRequestFile &&
              std::string_view(kReleaseFile) == kUpdateTarballFile &&
              std::string_view(kSignatureFile) == kUpdateSignatureFile &&
              std::string_view(kReadyFile) == kUpdateReadyFile &&
              std::string_view(kStatusFile) == kUpdateStatusFile);

constexpr std::uint64_t kMaxSmallFile = 256;

FileLimits app_file_limits(const Context& c, std::uint64_t max_bytes) {
    FileLimits limits;
    limits.max_bytes = max_bytes;
    limits.check_owner = true;
    limits.owner = c.user.uid;
    return limits;
}

std::string one_line(std::string text) {
    for (char& ch : text) {
        if (static_cast<unsigned char>(ch) < 0x20 || ch == 0x7F) {
            ch = ' ';
        }
    }
    return text;
}

} // namespace

Fd open_update_dir(const Context& c, std::string& error) {
    return open_directory(c.update_dir(), error);
}

// The request is gone from its name whatever it was: removed, or, when it cannot be removed (a folder
// the app made there), renamed aside, so the path unit, which waits for the name, does not fire again.
void remove_request(int update_fd) {
    if (::unlinkat(update_fd, kRequestFile, 0) == 0 || errno == ENOENT) {
        return;
    }
    if (::unlinkat(update_fd, kRequestFile, AT_REMOVEDIR) == 0) {
        return; // an empty folder
    }
    for (int n = 0; n < 50; ++n) {
        const std::string aside = std::string(kRequestFile) + ".refused" + (n == 0 ? "" : "." + std::to_string(n));
        if (::renameat(update_fd, kRequestFile, update_fd, aside.c_str()) == 0) {
            return;
        }
    }
}

std::string take_request(const Context& c, int update_fd, std::string& error) {
    error.clear();
    PlainFile file = open_plain_file(update_fd, kRequestFile, app_file_limits(c, kMaxSmallFile));
    if (file.status == OpenStatus::Missing) {
        return {};
    }
    std::string text;
    std::string why;
    const bool read = file.status == OpenStatus::Ok &&
                      read_all_text(file.fd.get(), kMaxSmallFile, text, why);
    remove_request(update_fd); // gone whatever it said (a folder is renamed aside): it cannot start a second run
    if (file.status != OpenStatus::Ok) {
        error = std::string("the request was refused: ") + open_status_text(file.status);
        return {};
    }
    if (!read) {
        error = "the request could not be read: " + why;
        return {};
    }
    const std::string line = text.substr(0, text.find('\n'));
    constexpr std::string_view kKey = "version=";
    if (line.compare(0, kKey.size(), kKey) != 0 ||
        format_update_request(line.substr(kKey.size())).empty() ||
        text != format_update_request(line.substr(kKey.size()))) {
        error = "the request is not `version=X.Y.Z` and nothing else";
        return {};
    }
    return line.substr(kKey.size());
}

bool ready_for(const Context& c, int update_fd, const std::string& version) {
    PlainFile file = open_plain_file(update_fd, kReadyFile, app_file_limits(c, 64));
    if (file.status != OpenStatus::Ok) {
        return false;
    }
    std::string text;
    std::string why;
    return read_all_text(file.fd.get(), 64, text, why) && text == format_update_ready(version);
}

void clear_ready(int update_fd) {
    ::unlinkat(update_fd, kReadyFile, 0);
}

UpdateStatus read_status(const Context& c) {
    std::string error;
    const Fd dir = open_update_dir(c, error);
    if (!dir.ok()) {
        return {};
    }
    FileLimits limits;
    limits.max_bytes = 2048;
    limits.check_owner = true;
    limits.owner = ::geteuid();
    PlainFile file = open_plain_file(dir.get(), kStatusFile, limits);
    std::string text;
    if (file.status != OpenStatus::Ok || !read_all_text(file.fd.get(), 2048, text, error)) {
        return {};
    }
    return parse_update_status(text);
}

void write_status(const Context& c, const std::string& state, const std::string& version,
                  const std::string& from, const std::string& reason, const std::string& data) {
    std::string error;
    const Fd dir = open_update_dir(c, error);
    if (!dir.ok()) {
        note("no status written: " + error);
        return;
    }
    const long long now = std::chrono::duration_cast<std::chrono::seconds>(
                              std::chrono::system_clock::now().time_since_epoch())
                              .count();
    const std::string text = "state=" + state + "\nversion=" + version + "\nfrom=" + from +
                             "\nreason=" + one_line(reason) +
                             (data.empty() ? std::string() : "\ndata=" + one_line(data)) +
                             "\ntime=" + std::to_string(now) + "\n";
    if (!write_file_atomic(dir.get(), kStatusFile, text, 0644, error)) {
        note("no status written: " + error);
    }
}

} // namespace etude::helper
