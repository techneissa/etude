// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_state.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;

namespace {

LibraryFile file(const char* name, std::uint64_t size = 100, std::int64_t mtime = 7) {
    return LibraryFile{name, size, mtime};
}

SongEntry entry_titled(const char* title) {
    SongEntry e;
    e.title = title;
    e.duration_s = 12.0;
    return e;
}

std::vector<std::string> names(const LibraryState& s) {
    std::vector<std::string> out;
    for (const LibraryItem& i : s.items) {
        out.push_back(i.relative);
    }
    return out;
}

} // namespace

TEST_CASE("a stamp changes with the size and with the time") {
    CHECK(file_stamp(100, 7) == file_stamp(100, 7));
    CHECK(file_stamp(100, 7) != file_stamp(101, 7));
    CHECK(file_stamp(100, 7) != file_stamp(100, 8));
    CHECK(file_stamp(100, 7) != file_stamp(7, 100));
}

TEST_CASE("a listing is every file pending, in name order, without reading any") {
    const std::vector<LibraryFile> files = {file("b.mid"), file("Zed.mid"), file("A/c.mid"),
                                            file("\xC3\xA9tude.mid"), file("a.mid")};
    const LibraryState s = list_state(files);
    CHECK(s.items.size() == 5);
    CHECK(s.pending() == 5);
    CHECK(s.loaded() == 0);
    CHECK(s.failed() == 0);
    CHECK_FALSE(s.settled());
    // Folded names (case and accents ignored) decide; the exact name breaks ties.
    CHECK(names(s) ==
          std::vector<std::string>{"a.mid", "A/c.mid", "b.mid", "\xC3\xA9tude.mid", "Zed.mid"});
    for (const LibraryItem& i : s.items) {
        CHECK(i.state == EntryState::Pending);
    }
    CHECK(list_state({}).settled());
}

TEST_CASE("results are recorded by index and stamp; a stale result is dropped") {
    LibraryState s =
        list_state(std::vector<LibraryFile>{file("a.mid"), file("b.mid"), file("c.mid")});
    CHECK(next_pending(s) == std::size_t{0});
    CHECK(apply_loaded(s, 0, s.items[0].stamp, entry_titled("A")));
    CHECK(s.items[0].state == EntryState::Loaded);
    CHECK(s.items[0].entry.title == "A");
    CHECK(next_pending(s) == std::size_t{1});
    CHECK(apply_failed(s, 1, s.items[1].stamp, "not a song file"));
    CHECK(s.items[1].state == EntryState::Failed);
    CHECK(s.items[1].error == "not a song file");
    CHECK(next_pending(s) == std::size_t{2});
    CHECK(next_pending(s, 3) == std::nullopt);

    // A result read under an older stamp, or for an index that is gone, changes nothing.
    CHECK_FALSE(apply_loaded(s, 2, s.items[2].stamp + 1, entry_titled("old")));
    CHECK_FALSE(apply_failed(s, 9, 0, "x"));
    CHECK(s.items[2].state == EntryState::Pending);
    CHECK(s.pending() == 1);
    CHECK(s.loaded() == 1);
    CHECK(s.failed() == 1);
    CHECK_FALSE(s.settled());
    CHECK(apply_loaded(s, 2, s.items[2].stamp, entry_titled("C")));
    CHECK(s.settled());
}

TEST_CASE("a new listing keeps the results of files that did not change") {
    LibraryState s =
        list_state(std::vector<LibraryFile>{file("a.mid"), file("b.mid"), file("c.mid")});
    apply_loaded(s, 0, s.items[0].stamp, entry_titled("A"));
    apply_failed(s, 1, s.items[1].stamp, "bad");
    // a unchanged, b touched, c unchanged but still pending, d new, e not listed any more.
    const LibraryState next =
        list_state(std::vector<LibraryFile>{file("a.mid"), file("b.mid", 100, 8), file("c.mid"),
                                            file("d.mid")},
                   &s);
    REQUIRE(next.items.size() == 4);
    CHECK(next.items[0].state == EntryState::Loaded);
    CHECK(next.items[0].entry.title == "A");
    CHECK(next.items[1].state == EntryState::Pending); // another time: look again
    CHECK(next.items[2].state == EntryState::Pending);
    CHECK(next.items[3].state == EntryState::Pending);

    // A failure stays a failure while the file is unchanged.
    const LibraryState again =
        list_state(std::vector<LibraryFile>{file("a.mid"), file("b.mid"), file("c.mid")}, &s);
    CHECK(again.items[1].state == EntryState::Failed);
    CHECK(again.items[1].error == "bad");
}

TEST_CASE("stepping wraps, passes over failed songs and starts at the first usable one") {
    LibraryState s = list_state(
        std::vector<LibraryFile>{file("a.mid"), file("b.mid"), file("c.mid"), file("d.mid")});
    CHECK(step_item(s, std::nullopt, 1) == std::size_t{0});
    CHECK(step_item(s, std::size_t{99}, -1) == std::size_t{0});
    CHECK(step_item(s, std::size_t{0}, -1) == std::size_t{3});
    CHECK(step_item(s, std::size_t{3}, 1) == std::size_t{0});
    apply_failed(s, 1, s.items[1].stamp, "bad");
    apply_failed(s, 3, s.items[3].stamp, "bad");
    CHECK(step_item(s, std::size_t{0}, 1) == std::size_t{2});
    CHECK(step_item(s, std::size_t{2}, 1) == std::size_t{0});
    CHECK(step_item(s, std::size_t{0}, -1) == std::size_t{2});
    // Pending songs count: they may still turn out fine.
    apply_failed(s, 0, s.items[0].stamp, "bad");
    CHECK(step_item(s, std::nullopt, 1) == std::size_t{2});
    CHECK(step_item(s, std::size_t{2}, 1) == std::size_t{2});
    apply_failed(s, 2, s.items[2].stamp, "bad");
    CHECK(step_item(s, std::nullopt, 1) == std::nullopt);
    CHECK(step_item(list_state({}), std::nullopt, 1) == std::nullopt);
}

TEST_CASE("the entry cache answers for the same bytes under the same name only") {
    EntryCache cache;
    CHECK(cache.find(1, "a.mid") == nullptr);
    cache.insert(1, "a.mid", entry_titled("A"));
    REQUIRE(cache.find(1, "a.mid") != nullptr);
    CHECK(cache.find(1, "a.mid")->title == "A");
    CHECK(cache.find(1, "b.mid") == nullptr); // the title comes from the name
    CHECK(cache.find(2, "a.mid") == nullptr);
    CHECK(cache.size() == 1);
}

TEST_CASE("the entry cache drops a song's old parse when its bytes change") {
    EntryCache cache;
    cache.insert(1, "a.mid", entry_titled("old"), "/x/a.mid");
    cache.insert(7, "b.mid", entry_titled("B"), "/x/b.mid");
    cache.insert(2, "a.mid", entry_titled("new"), "/x/a.mid"); // edited: new hash, same path
    CHECK(cache.find(1, "a.mid") == nullptr);
    REQUIRE(cache.find(2, "a.mid") != nullptr);
    CHECK(cache.find(2, "a.mid")->title == "new");
    CHECK(cache.find(7, "b.mid") != nullptr);
    CHECK(cache.size() == 2);
}

TEST_CASE("the entry cache stays under its cap") {
    EntryCache cache;
    for (std::uint64_t i = 0; i < EntryCache::kMaxEntries + 10; ++i) {
        cache.insert(i, "song" + std::to_string(i) + ".mid", entry_titled("x"));
    }
    CHECK(cache.size() <= EntryCache::kMaxEntries);
    CHECK(cache.find(EntryCache::kMaxEntries + 9, "song" + std::to_string(EntryCache::kMaxEntries + 9) + ".mid") != nullptr);
}
