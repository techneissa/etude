// SPDX-License-Identifier: GPL-3.0-or-later
// Watches the Songs folder for files added, changed or removed by anyone: the network share, the
// phone upload page, a copy in Finder. A worker thread waits for the system's word (FolderEvents)
// or, where there is none or it is silent (a share mounted from another machine), for a timer; it
// lists the folder, hands the listing to the core WatchTracker and keeps what the tracker reports
// for the frame thread to take. A file still being copied is held back until it stops changing.
// The frame thread never lists a folder or waits on this. No SDL.
#pragma once

#include "folder_events.hpp"

#include "etude/folder_watch.hpp"

#include <chrono>
#include <condition_variable>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <thread>
#include <vector>

namespace etude::app {

// True for a folder on a share from another machine (SMB, NFS, ...), where the system sends no
// word of changes made elsewhere: the watcher polls it.
bool is_network_folder(const std::filesystem::path& folder);

// Every watched file under `root` (see is_watched_path): path with '/' under the root, size and
// time. Hidden folders are not entered. A missing folder lists nothing.
std::vector<WatchEntry> list_watched_folder(const std::filesystem::path& root);

class FolderWatcher {
  public:
    struct Options {
        std::filesystem::path root;
        std::int64_t settle_ms = kWatchSettleMs;
        // How often the folder is listed with the system's help: it only has to catch what the
        // system missed. Without it (no support, or `native` off) the timer is all there is.
        std::int64_t safety_poll_ms = 10'000;
        std::int64_t poll_ms = 2'000;
        // The least time between two listings, so a long copy (a callback per write) lists a few
        // times a second.
        std::int64_t min_gap_ms = 250;
        // How often to look again while a file is settling.
        std::int64_t settling_ms = 250;
        bool native = true;
        // Called from the worker thread when a report is ready (wake the frame loop).
        std::function<void()> on_report;
    };

    explicit FolderWatcher(Options options);
    ~FolderWatcher();
    FolderWatcher(const FolderWatcher&) = delete;
    FolderWatcher& operator=(const FolderWatcher&) = delete;

    // What happened since the last call, merged into one report; none when nothing did. Never
    // waits for the worker (a try-lock).
    std::optional<WatchReport> take();
    // Asks for a listing now (tests, and a caller that knows it wrote a file).
    void nudge();
    // "fsevents", "inotify" or "polling" (no support, a folder on another machine's share, or
    // `native` off); "polling" until the worker has chosen.
    const char* backend() const;
    // Listings made so far (tests).
    std::size_t listings() const;

  private:
    void run();
    bool wait(std::int64_t timeout_ms);
    void publish(WatchReport report);

    Options options_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    bool dirty_ = false;
    bool quit_ = false;
    const char* backend_ = "polling";
    std::size_t listings_ = 0;
    WatchReport pending_;
    std::thread thread_;
};

} // namespace etude::app
