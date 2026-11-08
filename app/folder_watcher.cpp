// SPDX-License-Identifier: GPL-3.0-or-later
#include "folder_watcher.hpp"
#include "crash_thread.hpp"

#include <algorithm>
#include <iterator>
#include <string>
#include <utility>

#if defined(__APPLE__)
#include <sys/mount.h>
#include <sys/param.h>
#elif defined(__linux__)
#include <sys/vfs.h>
#endif

namespace etude::app {

namespace fs = std::filesystem;

namespace {

std::int64_t now_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

} // namespace

bool is_network_folder(const fs::path& folder) {
#if defined(__APPLE__)
    struct statfs info{};
    if (statfs(folder.c_str(), &info) != 0) {
        return false;
    }
    const std::string type = info.f_fstypename;
    return type == "smbfs" || type == "nfs" || type == "afpfs" || type == "webdav" ||
           type == "cifs";
#elif defined(__linux__)
    struct statfs info{};
    if (statfs(folder.c_str(), &info) != 0) {
        return false;
    }
    // SMB, CIFS (both generations), NFS and FUSE (sshfs and the like).
    const auto type = static_cast<unsigned long>(info.f_type);
    return type == 0x517BUL || type == 0xFF534D42UL || type == 0xFE534D42UL || type == 0x6969UL ||
           type == 0x65735546UL;
#else
    (void)folder;
    return false;
#endif
}

std::vector<WatchEntry> list_watched_folder(const fs::path& root) {
    std::vector<WatchEntry> files;
    std::error_code ec;
    if (!fs::is_directory(root, ec)) {
        return files;
    }
    fs::recursive_directory_iterator it(root, fs::directory_options::skip_permission_denied, ec);
    for (; !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        const std::string name = it->path().filename().string();
        std::error_code entry_ec;
        if (!name.empty() && name.front() == '.') {
            if (it->is_directory(entry_ec)) {
                it.disable_recursion_pending();
            }
            continue;
        }
        if (!it->is_regular_file(entry_ec)) {
            continue;
        }
        std::string relative = it->path().lexically_relative(root).generic_string();
        if (!is_watched_path(relative)) {
            continue;
        }
        const std::uintmax_t size = it->file_size(entry_ec);
        const auto time = it->last_write_time(entry_ec);
        if (entry_ec) {
            continue; // gone while listing; the next look finds it or not
        }
        files.push_back({std::move(relative), static_cast<std::uint64_t>(size),
                         static_cast<std::int64_t>(time.time_since_epoch().count())});
    }
    return files;
}

FolderWatcher::FolderWatcher(Options options) : options_(std::move(options)) {
    thread_ = spawn_thread([this] { run(); });
}

FolderWatcher::~FolderWatcher() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        quit_ = true;
    }
    cv_.notify_all();
    thread_.join();
}

std::optional<WatchReport> FolderWatcher::take() {
    std::unique_lock<std::mutex> lock(mutex_, std::try_to_lock);
    if (!lock.owns_lock() || pending_.empty()) {
        return std::nullopt;
    }
    return std::exchange(pending_, WatchReport{});
}

void FolderWatcher::nudge() {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        dirty_ = true;
    }
    cv_.notify_all();
}

const char* FolderWatcher::backend() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return backend_;
}

std::size_t FolderWatcher::listings() const {
    const std::lock_guard<std::mutex> lock(mutex_);
    return listings_;
}

bool FolderWatcher::wait(std::int64_t timeout_ms) {
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait_for(lock, std::chrono::milliseconds(std::max<std::int64_t>(timeout_ms, 1)),
                 [this] { return dirty_ || quit_; });
    dirty_ = false;
    return !quit_;
}

void FolderWatcher::publish(WatchReport report) {
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        auto append = [](std::vector<std::string>& to, std::vector<std::string>& from) {
            to.insert(to.end(), std::make_move_iterator(from.begin()),
                      std::make_move_iterator(from.end()));
        };
        append(pending_.added, report.added);
        append(pending_.changed, report.changed);
        append(pending_.removed, report.removed);
        pending_.moved += report.moved;
        pending_.new_songs += report.new_songs;
    }
    if (options_.on_report) {
        options_.on_report();
    }
}

void FolderWatcher::run() {
    WatchTracker tracker(options_.settle_ms);
    std::unique_ptr<FolderEvents> events;
    const auto try_native = [&] {
        if (!options_.native || events != nullptr || is_network_folder(options_.root)) {
            return;
        }
        std::unique_ptr<FolderEvents> candidate = make_native_events();
        if (candidate && candidate->start(options_.root, [this] { nudge(); })) {
            events = std::move(candidate);
            const std::lock_guard<std::mutex> lock(mutex_);
            backend_ = events->name();
        }
    };
    try_native();
    std::int64_t last_list_ms = -options_.min_gap_ms;
    for (;;) {
        const std::int64_t wait_from = now_ms();
        const std::int64_t spacing = last_list_ms + options_.min_gap_ms - wait_from;
        if (spacing > 0) {
            // A listing was made a moment ago: let writes pile up before the next one.
            std::unique_lock<std::mutex> lock(mutex_);
            if (cv_.wait_for(lock, std::chrono::milliseconds(spacing), [this] { return quit_; })) {
                break;
            }
        }
        {
            const std::lock_guard<std::mutex> lock(mutex_);
            if (quit_) {
                break;
            }
        }
        last_list_ms = now_ms();
        WatchReport report = tracker.observe(list_watched_folder(options_.root), last_list_ms);
        {
            // Counted once the listing is in the tracker: a caller that waits for the first one
            // knows the baseline is made.
            const std::lock_guard<std::mutex> lock(mutex_);
            ++listings_;
        }
        if (!report.empty()) {
            publish(std::move(report));
        }
        try_native(); // the folder may have appeared since
        const std::int64_t timeout = tracker.settling()  ? options_.settling_ms
                                     : events != nullptr ? options_.safety_poll_ms
                                                         : options_.poll_ms;
        if (!wait(timeout)) {
            break;
        }
    }
    if (events) {
        events->stop();
    }
}

} // namespace etude::app
