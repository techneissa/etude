// SPDX-License-Identifier: GPL-3.0-or-later
#include "backup.hpp"

#include "safe_io.hpp"

#include <dirent.h>
#include <fcntl.h>
#include <sys/stat.h>

#include <cerrno>
#include <vector>

namespace etude::helper {

namespace {

constexpr std::uint64_t kMaxDataFileBytes = 64ULL * 1024 * 1024 * 1024;

FileLimits any_size() {
    FileLimits limits;
    limits.max_bytes = kMaxDataFileBytes;
    return limits;
}

std::string failure(const std::string& what, const std::string& why) { return what + ": " + why; }

// The names in the folder `dir_fd`.
std::vector<std::string> names_in(int dir_fd) {
    std::vector<std::string> names;
    const int dup_fd = ::fcntl(dir_fd, F_DUPFD_CLOEXEC, 3);
    DIR* dir = dup_fd >= 0 ? ::fdopendir(dup_fd) : nullptr;
    if (dir == nullptr) {
        if (dup_fd >= 0) {
            ::close(dup_fd);
        }
        return names;
    }
    while (const dirent* entry = ::readdir(dir)) {
        const std::string name = entry->d_name;
        if (name != "." && name != "..") {
            names.push_back(name);
        }
    }
    ::closedir(dir);
    return names;
}

// The folder `name` inside `dir_fd`, made (private to the app's user) when it is not there.
Fd ensure_folder(int dir_fd, const std::string& name, std::string& error) {
    if (::mkdirat(dir_fd, name.c_str(), 0700) != 0 && errno != EEXIST) {
        error = failure(name, errno_text(errno));
        return Fd();
    }
    return open_directory_at(dir_fd, name, error);
}

// Copies the data file `name` of the folder `from` into the folder `to` under `target`, whole and
// flushed. Missing is not an error (a first run has no WAL); anything but a plain file is.
std::string copy_data_file(int from, int to, const std::string& name, const std::string& target,
                           bool& copied) {
    copied = false;
    PlainFile source = open_plain_file(from, name, any_size());
    if (source.status == OpenStatus::Missing) {
        return {};
    }
    if (source.status != OpenStatus::Ok) {
        return failure(name, source.status == OpenStatus::Failed ? errno_text(source.error)
                                                                 : open_status_text(source.status));
    }
    std::string error;
    ::unlinkat(to, target.c_str(), 0); // a leftover of an earlier try
    if (!copy_to_new_file(source.fd.get(), to, target, kMaxDataFileBytes, error)) {
        return error;
    }
    copied = true;
    return {};
}

} // namespace

std::string backup_data(const std::string& data_dir, const std::string& version) {
    std::string error;
    Fd data = open_directory(data_dir, error);
    if (!data.ok()) {
        return errno == ENOENT ? std::string() : error;
    }
    Fd root = ensure_folder(data.get(), std::string(kBackupFolder), error);
    if (!root.ok()) {
        return error;
    }
    const std::string fresh = version + ".new";
    if (!remove_tree_at(root.get(), fresh, error)) {
        return error;
    }
    Fd dir = ensure_folder(root.get(), fresh, error);
    if (!dir.ok()) {
        return error;
    }
    const auto abandon = [&](const std::string& why) {
        std::string ignore;
        remove_tree_at(root.get(), fresh, ignore);
        return why;
    };
    for (const std::string_view name : kBackedUp) {
        bool copied = false;
        const std::string why = copy_data_file(data.get(), dir.get(), std::string(name),
                                               std::string(name), copied);
        if (!why.empty()) {
            return abandon("could not back up " + why);
        }
    }
    std::string marker_error;
    if (!write_file_atomic(dir.get(), std::string(kBackupMarker), "version=" + version + "\n", 0600,
                           marker_error)) {
        return abandon("could not back up " + marker_error);
    }
    sync_directory(dir.get());
    // The backup is whole: it takes the place of the one of this version and of every older one.
    if (!remove_tree_at(root.get(), version, error)) {
        return abandon("could not replace the old backup: " + error);
    }
    if (::renameat(root.get(), fresh.c_str(), root.get(), version.c_str()) != 0) {
        return abandon("could not back up " + failure(version, errno_text(errno)));
    }
    for (const std::string& other : names_in(root.get())) {
        if (other != version) {
            std::string ignore;
            remove_tree_at(root.get(), other, ignore);
        }
    }
    sync_directory(root.get());
    return {};
}

std::string restore_data(const std::string& data_dir, const std::string& version) {
    std::string error;
    Fd data = open_directory(data_dir, error);
    if (!data.ok()) {
        return "the data folder is not there: " + error;
    }
    Fd root = open_directory_at(data.get(), std::string(kBackupFolder), error);
    Fd backup = root.ok() ? open_directory_at(root.get(), version, error) : Fd();
    if (!backup.ok() && root.ok()) {
        // A power cut between the old backup's removal and the new one's rename leaves the whole new
        // one under its temporary name.
        backup = open_directory_at(root.get(), version + ".new", error);
    }
    if (!backup.ok()) {
        return "no backup of " + version + " to restore from";
    }
    PlainFile marker = open_plain_file(backup.get(), std::string(kBackupMarker), any_size());
    if (marker.status != OpenStatus::Ok) {
        return "the backup of " + version + " is not complete";
    }
    // 1. Every file the backup has, copied beside its place under a temporary name.
    std::vector<std::pair<std::string, bool>> plan; // name, whether the backup has it
    for (const std::string_view name : kBackedUp) {
        bool copied = false;
        const std::string temp = "." + std::string(name) + ".restore";
        const std::string why = copy_data_file(backup.get(), data.get(), std::string(name), temp, copied);
        if (!why.empty()) {
            for (const auto& step : plan) {
                ::unlinkat(data.get(), ("." + step.first + ".restore").c_str(), 0);
            }
            return "could not restore " + why;
        }
        plan.emplace_back(std::string(name), copied);
    }
    // 2. All in place at once: renames, then the removal of what the backup has not.
    for (const auto& [name, has] : plan) {
        if (has && ::renameat(data.get(), ("." + name + ".restore").c_str(), data.get(), name.c_str()) != 0) {
            return "could not restore " + failure(name, errno_text(errno));
        }
        if (!has && ::unlinkat(data.get(), name.c_str(), 0) != 0 && errno != ENOENT) {
            return "could not remove " + failure(name, errno_text(errno));
        }
    }
    sync_directory(data.get());
    return {};
}

} // namespace etude::helper
