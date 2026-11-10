// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/folder_watcher.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

FolderWatcher::Options quick(const fs::path& root, bool native, std::int64_t settle_ms = 40) {
    FolderWatcher::Options o;
    o.root = root;
    o.native = native;
    o.settle_ms = settle_ms;
    o.poll_ms = 10;
    o.settling_ms = 5;
    o.min_gap_ms = 2;
    o.safety_poll_ms = native ? 60'000 : 10; // the system's word, or nothing, must be what finds it
    return o;
}

// What the watcher reports within `limit_ms`, merged; stops at the first report with `want_added`
// songs or when time is up.
WatchReport collect(FolderWatcher& watcher, std::size_t want_added, int limit_ms) {
    WatchReport all;
    const auto end = std::chrono::steady_clock::now() + std::chrono::milliseconds(limit_ms);
    while (std::chrono::steady_clock::now() < end) {
        if (auto r = watcher.take()) {
            all.added.insert(all.added.end(), r->added.begin(), r->added.end());
            all.changed.insert(all.changed.end(), r->changed.begin(), r->changed.end());
            all.removed.insert(all.removed.end(), r->removed.begin(), r->removed.end());
            all.moved += r->moved;
            all.new_songs += r->new_songs;
            if (all.added.size() >= want_added && want_added > 0) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return all;
}

void wait_listed(const FolderWatcher& watcher) {
    for (int i = 0; i < 400 && watcher.listings() == 0; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

} // namespace

TEST_CASE("list_watched_folder: songs, PDFs and images; no hidden folders or files") {
    TempDir dir;
    write_file(dir.path / "a.mid", "abc");
    write_file(dir.path / "sub/b.mxl");
    write_file(dir.path / "Inbox/scan.pdf");
    write_file(dir.path / "notes.txt");
    write_file(dir.path / ".hidden/c.mid");
    write_file(dir.path / "._d.mid");
    write_file(dir.path / "Inbox/.incoming/.x.part");
    write_file(dir.path / "Inbox/.incoming/y.mid");
    const std::vector<WatchEntry> files = list_watched_folder(dir.path);
    std::vector<std::string> names;
    for (const WatchEntry& f : files) {
        names.push_back(f.path);
    }
    std::sort(names.begin(), names.end());
    CHECK(names == std::vector<std::string>{"Inbox/scan.pdf", "a.mid", "sub/b.mxl"});
    for (const WatchEntry& f : files) {
        if (f.path == "a.mid") {
            CHECK(f.size == 3);
        }
    }
    CHECK(list_watched_folder(dir.path / "missing").empty());
}

TEST_CASE("folder watcher (polling): new songs are reported once, with the words for the toast") {
    TempDir dir;
    write_file(dir.path / "old.mid");
    FolderWatcher watcher(quick(dir.path, false));
    wait_listed(watcher);
    CHECK(std::string(watcher.backend()) == "polling");
    write_file(dir.path / "Inbox/a.mid");
    write_file(dir.path / "Inbox/b.mid");
    write_file(dir.path / "c.abc");
    write_file(dir.path / "Inbox/.incoming/.t.part");
    const WatchReport report = collect(watcher, 3, 5000);
    CHECK(report.added.size() == 3);
    CHECK(report.new_songs == 3);
    CHECK(new_songs_text(report.new_songs) == "3 new songs");
    // Reported once.
    CHECK(collect(watcher, 0, 100).empty());
}

TEST_CASE("folder watcher: a folder that appears later is picked up") {
    TempDir dir;
    FolderWatcher watcher(quick(dir.path / "Songs", false));
    wait_listed(watcher); // the first look finds no folder at all
    write_file(dir.path / "Songs/first.mid");
    CHECK(collect(watcher, 1, 5000).added.size() == 1);
    write_file(dir.path / "Songs/second.mid");
    CHECK(collect(watcher, 1, 5000).added.size() == 1);
}

TEST_CASE("folder watcher: stops at once when destroyed, and reports a listing count") {
    TempDir dir;
    {
        FolderWatcher::Options o = quick(dir.path, false);
        o.poll_ms = 60'000;
        FolderWatcher watcher(o);
        wait_listed(watcher);
        CHECK(watcher.listings() >= 1);
        watcher.nudge();
    }
}
