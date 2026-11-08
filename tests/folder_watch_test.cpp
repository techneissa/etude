// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/folder_watch.hpp"

#include <doctest/doctest.h>

#include <vector>

using namespace etude;

namespace {

using Listing = std::vector<WatchEntry>;

WatchEntry file(const char* path, std::uint64_t size = 100, std::int64_t mtime = 1) {
    return {path, size, mtime};
}

} // namespace

TEST_CASE("watched paths: songs, PDFs and images, nothing hidden") {
    CHECK(is_watched_path("a.mid"));
    CHECK(is_watched_path("Classical/Bach/prelude.MXL"));
    CHECK(is_watched_path("Inbox/scan.pdf"));
    CHECK(is_watched_path("Inbox/photo.heic"));
    CHECK_FALSE(is_watched_path(""));
    CHECK_FALSE(is_watched_path("/a.mid"));
    CHECK_FALSE(is_watched_path("notes.txt"));
    CHECK_FALSE(is_watched_path("a.mid.part"));
    CHECK_FALSE(is_watched_path(".DS_Store"));
    CHECK_FALSE(is_watched_path("._song.mid"));
    CHECK_FALSE(is_watched_path("Inbox/.incoming/x.mid"));
    CHECK_FALSE(is_watched_path(".hidden/a.mid"));
    CHECK_FALSE(is_watched_path("a//b.mid"));
}

TEST_CASE("folder watch: the first listing is what was already there") {
    WatchTracker tracker;
    const Listing first{file("a.mid"), file("b.mid")};
    CHECK(tracker.observe(first, 0).empty());
    CHECK(tracker.known() == 2);
    CHECK_FALSE(tracker.settling());
    CHECK(tracker.observe(first, 5000).empty());
}

TEST_CASE("folder watch: a new file is reported once its size has stopped changing") {
    WatchTracker tracker(1500);
    tracker.observe({}, 0);
    const Listing with{file("new.mid", 300, 5)};
    CHECK(tracker.observe(with, 1000).empty());
    CHECK(tracker.settling());
    CHECK(tracker.observe(with, 1000 + 1499).empty()); // one millisecond short
    const WatchReport done = tracker.observe(with, 1000 + 1500);
    REQUIRE(done.added.size() == 1);
    CHECK(done.added[0] == "new.mid");
    CHECK(done.new_songs == 1);
    CHECK_FALSE(tracker.settling());
    CHECK(tracker.observe(with, 20000).empty()); // reported once
}

TEST_CASE("folder watch: a file still growing waits, and its clock starts again at each change") {
    WatchTracker tracker(1500);
    tracker.observe({}, 0);
    CHECK(tracker.observe(Listing{file("big.mid", 100, 1)}, 1000).empty());
    CHECK(tracker.observe(Listing{file("big.mid", 5000, 2)}, 2400).empty()); // grew
    CHECK(tracker.observe(Listing{file("big.mid", 9000, 3)}, 3800).empty()); // grew again
    CHECK(tracker.observe(Listing{file("big.mid", 9000, 3)}, 3800 + 1499).empty());
    const WatchReport done = tracker.observe(Listing{file("big.mid", 9000, 3)}, 3800 + 1500);
    CHECK(done.added.size() == 1);
}

TEST_CASE("folder watch: a copy that is abandoned never shows") {
    WatchTracker tracker(1500);
    tracker.observe({}, 0);
    tracker.observe(Listing{file("half.mid", 10, 1)}, 100);
    CHECK(tracker.settling());
    const WatchReport gone = tracker.observe({}, 200);
    CHECK(gone.empty());
    CHECK_FALSE(tracker.settling());
}

TEST_CASE("folder watch: a changed file is reported as changed, after it settles") {
    WatchTracker tracker(1500);
    tracker.observe(Listing{file("a.mid", 100, 1)}, 0);
    CHECK(tracker.observe(Listing{file("a.mid", 150, 2)}, 5000).empty());
    const WatchReport done = tracker.observe(Listing{file("a.mid", 150, 2)}, 6500);
    CHECK(done.added.empty());
    REQUIRE(done.changed.size() == 1);
    CHECK(done.changed[0] == "a.mid");
    CHECK(done.new_songs == 0);
    // A file that changes and changes back in the wait reports nothing.
    tracker.observe(Listing{file("a.mid", 999, 9)}, 8000);
    CHECK(tracker.observe(Listing{file("a.mid", 150, 2)}, 8100).empty());
    CHECK_FALSE(tracker.settling());
}

TEST_CASE("folder watch: a removed file is reported at once") {
    WatchTracker tracker;
    tracker.observe(Listing{file("a.mid"), file("b.mid")}, 0);
    const WatchReport r = tracker.observe(Listing{file("b.mid")}, 10);
    REQUIRE(r.removed.size() == 1);
    CHECK(r.removed[0] == "a.mid");
    CHECK(r.new_songs == 0);
}

TEST_CASE("folder watch: a move or a rename is not a new song") {
    WatchTracker tracker(1500);
    tracker.observe(Listing{file("Inbox/a.mid", 300, 7)}, 0);
    const WatchReport gone = tracker.observe({}, 1000);
    CHECK(gone.removed.size() == 1);
    // It reappears with the same size and time in another folder, in the next look.
    const WatchReport here = tracker.observe(Listing{file("Classical/a.mid", 300, 7)}, 1200);
    CHECK(here.moved == 1);
    CHECK(here.added.empty());
    CHECK(here.new_songs == 0);
    CHECK_FALSE(tracker.settling());
    // A different file with the same name is new.
    tracker.observe({}, 2000);
    tracker.observe(Listing{file("x/a.mid", 301, 7)}, 2100);
    const WatchReport fresh = tracker.observe(Listing{file("x/a.mid", 301, 7)}, 3600);
    CHECK(fresh.new_songs == 1);
}

TEST_CASE("folder watch: a removal remembered too long is no move") {
    WatchTracker tracker(1500);
    tracker.observe(Listing{file("a.mid", 300, 7)}, 0);
    tracker.observe({}, 1000);
    const std::int64_t late = 1000 + kWatchMoveMs + 1;
    tracker.observe(Listing{file("copy.mid", 300, 7)}, late);
    const WatchReport done = tracker.observe(Listing{file("copy.mid", 300, 7)}, late + 1500);
    CHECK(done.new_songs == 1);
    CHECK(done.moved == 0);
}

TEST_CASE("folder watch: three new songs, a PDF, and hidden files in one batch") {
    WatchTracker tracker(1500);
    tracker.observe({}, 0);
    const Listing batch{file("a.mid"),
                        file("b.mxl"),
                        file("c.abc"),
                        file("scan.pdf"),
                        file("._a.mid"),
                        file(".DS_Store"),
                        file("Inbox/.incoming/d.mid"),
                        file("notes.txt")};
    CHECK(tracker.observe(batch, 100).empty());
    const WatchReport done = tracker.observe(batch, 1600);
    CHECK(done.added.size() == 4); // three songs and the PDF
    CHECK(done.new_songs == 3);
    CHECK(new_songs_text(done.new_songs) == "3 new songs");
}

TEST_CASE("folder watch: toast words") {
    CHECK(new_songs_text(0).empty());
    CHECK(new_songs_text(1) == "1 new song");
    CHECK(new_songs_text(2) == "2 new songs");
    CHECK(new_songs_text(120) == "120 new songs");
}

TEST_CASE("folder watch: the app's own downloads are not told again, other songs are") {
    OwnDownloads own;
    own.landed(1);
    CHECK(own.take(1) == 0); // the download itself
    CHECK(own.take(1) == 1); // a song from the phone straight after it
    own.landed(2);
    CHECK(own.take(3) == 1); // both downloads and one more in the same report
    own.landed(1);
    own.forget(); // a download the watcher never reported
    CHECK(own.take(1) == 1);
}
