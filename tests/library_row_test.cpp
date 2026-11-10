// SPDX-License-Identifier: GPL-3.0-or-later
// The saved list of the player's added songs (app/library_row.hpp): an added song whose file was
// edited is found by its path and follows its new hash, and one that is not in the library is
// hidden, never dropped from the list.
#include "library_row.hpp"

#include <doctest/doctest.h>

#include <cstdint>
#include <string>
#include <utility>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

// A read song with the facts the sort reads.
LibraryItem song(std::uint64_t hash, std::string title, int half_stars, bool left, int keys, int bars = 24,
                 int notes = 40) {
    LibraryItem item;
    item.relative = title + ".mid";
    item.stamp = hash * 7 + 1;
    item.state = EntryState::Loaded;
    item.entry.hash = hash;
    item.entry.title = std::move(title);
    item.entry.half_stars = half_stars;
    item.entry.difficulty = level_of_half_stars(half_stars);
    item.entry.right_notes = 20;
    item.entry.left_notes = left ? 10 : 0;
    item.entry.right_keys = keys;
    item.entry.left_keys = left ? keys : 0;
    item.entry.bar_count = bars;
    item.entry.note_count = notes;
    return item;
}

struct Fixture {
    LibraryState library;
    std::vector<RunRecord> runs;
    LibraryCatalogue::Prefs prefs;
    LibraryCatalogue cat;

    void sync() {
        cat.library_changed();
        cat.prefs_changed();
        cat.history_changed();
        REQUIRE(cat.sync(library, runs, prefs));
        cat.refresh_versions(); // the groups of versions laid over the songs, as the Melodies page asks
    }
    LibraryRow row(const MelodyAdded& added = {}) {
        sync();
        LibraryRow out;
        build_library_row(cat, added, out);
        return out;
    }
};

} // namespace

TEST_CASE("library row: an added song whose file was edited is found by its path and follows its new "
          "hash") {
    Fixture f;
    f.library.items = {song(1, "A", 3, false, 8), song(44, "D", 3, false, 8)}; // D was hash 4
    MelodyAdded added;
    added.set(4, 2, "D.mid");
    const LibraryRow row = f.row(added);
    REQUIRE(row.followed.size() == 1);
    CHECK(row.followed[0].old_hash == 4);
    CHECK(row.followed[0].new_hash == 44);
    CHECK(row.followed[0].path == "D.mid");
    CHECK(row.hidden.empty());
    // It keeps the step the player chose.
    REQUIRE(row.songs.size() == 2);
    CHECK(row.songs[1].hash == 44);
    CHECK(row.songs[1].added);
    CHECK(row.songs[1].step == 2);
    // The list follows (what the screen does with `followed`) and the next row finds it by its hash.
    MelodyAdded followed = added;
    CHECK(followed.rebind(4, 44, "D.mid"));
    const LibraryRow again = f.row(followed);
    CHECK(again.followed.empty());
    REQUIRE(again.songs.size() == 2);
    CHECK(again.songs[1].hash == 44);
    CHECK(again.songs[1].step == 2);
}

TEST_CASE("library row: an added song with neither its hash nor its path in the library is hidden, "
          "and counts for nothing") {
    Fixture f;
    f.library.items = {song(1, "A", 3, false, 8)};
    MelodyAdded added;
    added.set(4, 2, "D.mid");
    added.set(6, 3); // no path to find it by
    const LibraryRow row = f.row(added);
    CHECK(row.hidden == std::vector<std::uint64_t>{4, 6});
    CHECK(row.songs.size() == 1); // only A
    CHECK(hidden_added(f.cat, added) == std::vector<std::uint64_t>{4, 6});
    // The file comes back under its old hash: the entry shows again, where it was.
    f.library.items.push_back(song(4, "D", 3, false, 8));
    const LibraryRow back = f.row(added);
    CHECK(back.hidden == std::vector<std::uint64_t>{6});
    REQUIRE(back.songs.size() == 2);
    CHECK(back.songs[1].hash == 4);
    CHECK(back.songs[1].step == 2);
    CHECK(back.songs[1].added);
}
