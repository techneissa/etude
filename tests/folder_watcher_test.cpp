// SPDX-License-Identifier: GPL-3.0-or-later
#include "../app/folder_watcher.hpp"
#include "timing_factor.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

FolderWatcher::Options quick(const fs::path& root, bool native, std::int64_t settle_ms = 150) {
    FolderWatcher::Options o;
    o.root = root;
    o.native = native;
    o.settle_ms = settle_ms;
    o.poll_ms = 40;
    o.settling_ms = 20;
    o.min_gap_ms = 10;
    o.safety_poll_ms = native ? 60'000 : 40; // the system's word, or nothing, must be what finds it
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
    CHECK(collect(watcher, 0, 400).empty());
}

TEST_CASE("folder watcher (polling): a file still being copied is held back until it stops growing") {
    using Clock = std::chrono::steady_clock;
    TempDir dir;
    FolderWatcher watcher(quick(dir.path, false, 400)); // a pause of 400 ms would end a copy
    wait_listed(watcher);
    const fs::path file = dir.path / "big.mid";
    std::string bytes;
    const auto start = Clock::now();
    auto last_write = start;
    for (int i = 0; i < 12; ++i) { // grows for about 600 ms, with no pause near the 400 ms settle
        const auto before_write = Clock::now();
        // A pause of 300 ms or more between two writes (the machine stalled this thread) is a
        // pause the watcher may rightly call the end of the copy: no claim about it then.
        const bool kept_up = before_write - last_write < std::chrono::milliseconds(300);
        bytes += std::string(100, 'x');
        write_file(file, bytes);
        last_write = Clock::now();
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        if (i < 8 && kept_up && Clock::now() - last_write < std::chrono::milliseconds(300)) {
            CHECK_FALSE(watcher.take().has_value()); // never reported half done
        }
    }
    const auto grown = Clock::now() - start;
    const WatchReport report = collect(watcher, 1, 5000);
    REQUIRE(report.added.size() == 1); // and reported once, after it stopped
    CHECK(report.added[0] == "big.mid");
    CHECK(grown >= std::chrono::milliseconds(500));
}

TEST_CASE("folder watcher (polling): removed, changed and moved files") {
    TempDir dir;
    write_file(dir.path / "a.mid", "1");
    write_file(dir.path / "b.mid", "1");
    write_file(dir.path / "c.mid", "1");
    FolderWatcher watcher(quick(dir.path, false));
    wait_listed(watcher);
    fs::remove(dir.path / "a.mid");
    WatchReport r = collect(watcher, 0, 600);
    REQUIRE(r.removed.size() == 1);
    CHECK(r.removed[0] == "a.mid");
    write_file(dir.path / "b.mid", "12345678");
    r = collect(watcher, 0, 800);
    REQUIRE(r.changed.size() == 1);
    CHECK(r.changed[0] == "b.mid");
    CHECK(r.new_songs == 0);
    // A rename keeps size and time, so it is not a new song.
    fs::create_directories(dir.path / "Folder");
    fs::rename(dir.path / "c.mid", dir.path / "Folder/c.mid");
    r = collect(watcher, 0, 800);
    CHECK(r.new_songs == 0);
    CHECK(r.added.empty());
}

TEST_CASE("folder watcher: the system's own word finds a new file, without the timer") {
    TempDir dir;
    FolderWatcher watcher(quick(dir.path, true));
    wait_listed(watcher);
    for (int i = 0; i < 400 && std::string(watcher.backend()) == "polling"; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    const std::string backend = watcher.backend();
    if (backend == "polling") {
        WARN_MESSAGE(false, "no native folder events here; the polling test above covers it");
        return;
    }
    CHECK((backend == "fsevents" || backend == "inotify"));
    // The safety timer is a minute, so only the system's event can find these in time. A folder
    // made later is watched too.
    write_file(dir.path / "new.mid");
    WatchReport report = collect(watcher, 1, 10000);
    CHECK(report.added.size() == 1);
    fs::create_directories(dir.path / "made later");
    std::this_thread::sleep_for(std::chrono::milliseconds(700));
    write_file(dir.path / "made later/deep.mid");
    report = collect(watcher, 1, 10000);
    REQUIRE(report.added.size() == 1);
    CHECK(report.added[0] == "made later/deep.mid");
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
    const auto start = std::chrono::steady_clock::now();
    {
        FolderWatcher::Options o = quick(dir.path, false);
        o.poll_ms = 60'000;
        FolderWatcher watcher(o);
        wait_listed(watcher);
        CHECK(watcher.listings() >= 1);
        watcher.nudge();
    }
    const auto took = std::chrono::steady_clock::now() - start;
    CHECK(took < std::chrono::seconds(10)); // the poll sleeps a minute
    CHECK_TIMING(took < std::chrono::seconds(3));
}
