// SPDX-License-Identifier: GPL-3.0-or-later
// The "From your library" row (app/library_row.hpp): which songs it shows, at which step, that the
// player's own additions come first and keep the step they chose (also when the file is edited or
// gone), that a piece with versions has one tile, and what building it costs on a large library.
#include "library_row.hpp"

#include "timing_factor.hpp"

#include "etude/song_versions.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
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

std::vector<std::string> titles(const Fixture& f, const LibraryRow& row) {
    std::vector<std::string> out;
    for (const LibraryRowSong& s : row.songs) {
        out.push_back(f.cat.song(s.index).title);
    }
    return out;
}

// A pass's answer: each group's members as (content hash, chip word), easiest first.
std::shared_ptr<const VersionIndex>
index_of(const std::vector<std::vector<std::pair<std::uint64_t, std::string>>>& groups) {
    auto index = std::make_shared<VersionIndex>();
    for (const auto& g : groups) {
        auto& out = index->groups.emplace_back();
        for (const auto& [hash, name] : g) {
            out.push_back({hash, name, false});
            index->where[hash] = {static_cast<std::uint32_t>(index->groups.size() - 1),
                                  static_cast<std::uint32_t>(out.size() - 1)};
        }
    }
    return index;
}

} // namespace

TEST_CASE("library row: songs are sorted into steps and shown by step") {
    Fixture f;
    f.library.items = {
        song(1, "Both hands", 4, true, 25),    // step 4: the two hands cover 50 keys
        song(2, "Few keys", 3, false, 8),      // step 1
        song(3, "Wider", 4, false, 12),        // step 2
        song(4, "Too hard", 10, true, 30),     // no step
        song(5, "One note", 3, false, 1, 24, 1), // a test file, not a piece
    };
    const LibraryRow row = f.row();
    REQUIRE(row.songs.size() == 3);
    CHECK(row.sorted == 3);
    CHECK(row.songs[0].step == 1);
    CHECK(row.songs[1].step == 2);
    CHECK(row.songs[2].step == 4);
    CHECK(row.songs[0].added == false);
}

TEST_CASE("library row: a step shows a few songs, a file held twice counts once, unread songs wait") {
    Fixture f;
    for (std::uint64_t i = 1; i <= 6; ++i) {
        f.library.items.push_back(song(i, "Tune " + std::to_string(i), 3, false, 8));
    }
    f.library.items.push_back(song(1, "Tune 1 copy", 3, false, 8)); // the same content hash
    LibraryItem waiting;
    waiting.relative = "waiting.mid";
    waiting.stamp = 5;
    f.library.items.push_back(waiting);
    const LibraryRow row = f.row();
    CHECK(row.sorted == 6);
    CHECK(row.songs.size() == kLibraryRowPerStep);
}

TEST_CASE("library row: the library's folded title decides, not the raw one") {
    Fixture f;
    // Raw order would be Apple, Banana, zebra, \xC3\x89clair: the folded one is Apple, Banana, \xC3\x89clair, zebra.
    f.library.items = {
        song(1, "zebra", 3, false, 8),
        song(2, "Apple", 3, false, 8),
        song(3, "\xC3\x89" "clair", 3, false, 8),
        song(4, "Banana", 3, false, 8),
    };
    const LibraryRow row = f.row();
    CHECK(titles(f, row) == std::vector<std::string>{"Apple", "Banana", "\xC3\x89" "clair"});
}

TEST_CASE("library row: the player's additions come first, at the step they chose, even when the "
          "sort would not take them") {
    Fixture f;
    f.library.items = {
        song(1, "A", 3, false, 8),
        song(2, "B", 3, false, 8),
        song(3, "C", 3, false, 8),
        song(4, "D", 3, false, 8),
        song(5, "Far too hard", 10, true, 40),
    };
    MelodyAdded added;
    added.set(4, 1, "D.mid");
    added.set(5, 5, "Far too hard.mid");
    const LibraryRow row = f.row(added);
    REQUIRE(row.songs.size() == 5);
    CHECK(row.songs[0].hash == 4);
    CHECK(row.songs[0].added);
    CHECK(row.songs[0].step == 1);
    CHECK(row.songs[4].hash == 5);
    CHECK(row.songs[4].step == 5);
    CHECK(row.sorted == 5);
    CHECK(row.hidden.empty());
    CHECK(row.followed.empty());
}

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
    // It still has its tile, at the step the player chose (the row goes by step: A, step 1, first).
    REQUIRE(row.songs.size() == 2);
    CHECK(row.songs[0].hash == 1);
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
    CHECK(row.sorted == 1);
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

TEST_CASE("library row: a passed song gives its place to one not passed") {
    Fixture f;
    f.library.items = {
        song(1, "A", 3, false, 8),
        song(2, "B", 3, false, 8),
        song(3, "C", 3, false, 8),
        song(4, "D", 3, false, 8),
    };
    // The catalogue reads best scores from the runs; one song is passed.
    RunRecord r;
    r.song_hash = 1;
    r.all = {.graded = 40, .hit = 38};
    f.runs.push_back(r);
    const LibraryRow row = f.row();
    REQUIRE(row.songs.size() == 3);
    for (const LibraryRowSong& s : row.songs) {
        CHECK(s.hash != 1);
    }
}

TEST_CASE("library row: a piece with versions has one tile, the version the player has practised "
          "most, then the easiest") {
    Fixture f;
    f.library.items = {
        song(1, "Piece easy", 3, false, 8),
        song(2, "Piece medium", 4, false, 12),
        song(3, "Piece original", 4, false, 13),
        song(4, "Other tune", 3, false, 9),
    };
    f.cat.set_versions(index_of({{{1, "Easy"}, {2, "Medium"}, {3, "Original"}}}));
    // Nothing practised: the easiest version stands for the piece, and the other tune keeps its place.
    LibraryRow row = f.row();
    CHECK(titles(f, row) == std::vector<std::string>{"Other tune", "Piece easy"});
    CHECK(row.sorted == 2);
    // The medium one practised most.
    RunRecord r;
    r.song_hash = 2;
    r.all = {.graded = 40, .hit = 20};
    f.runs.push_back(r);
    f.runs.push_back(r);
    row = f.row();
    CHECK(titles(f, row) == std::vector<std::string>{"Other tune", "Piece medium"});
    // One the player added (a version that is not the practised one): it has the piece's tile.
    MelodyAdded added;
    added.set(3, 2, "Piece original.mid");
    row = f.row(added);
    REQUIRE(row.songs.size() == 2);
    CHECK(f.cat.song(row.songs[0].index).title == "Other tune");
    CHECK(f.cat.song(row.songs[1].index).title == "Piece original");
    CHECK(row.songs[1].added);
}

TEST_CASE("library row: versions do not take more than one of a step's places") {
    Fixture f;
    f.library.items = {
        song(1, "Piece a", 3, false, 8),  song(2, "Piece b", 3, false, 8), song(3, "Piece c", 3, false, 8),
        song(4, "Other 1", 3, false, 8),  song(5, "Other 2", 3, false, 8),
    };
    f.cat.set_versions(index_of({{{1, "A"}, {2, "B"}, {3, "C"}}}));
    const LibraryRow row = f.row();
    REQUIRE(row.songs.size() == kLibraryRowPerStep);
    CHECK(titles(f, row) == std::vector<std::string>{"Other 1", "Other 2", "Piece a"});
}

TEST_CASE("library row: building it for 20,000 songs reads only what the scan stored and is quick" *
          TIMING_ONLY) {
    Fixture f;
    f.library.items.reserve(20000);
    for (std::uint64_t i = 1; i <= 20000; ++i) {
        f.library.items.push_back(song(i, "Song " + std::to_string(i), 3 + static_cast<int>(i % 5), i % 3 == 0,
                                       6 + static_cast<int>(i % 30), 8 + static_cast<int>(i % 40)));
    }
    f.sync();
    MelodyAdded added;
    for (std::uint64_t i = 1; i <= 10; ++i) {
        added.set(i * 1000, 1 + static_cast<int>(i % 5), "Song " + std::to_string(i * 1000) + ".mid");
    }
    LibraryRow row;
    double best_ms = 1e9;
    for (int run = 0; run < 5; ++run) {
        const auto began = std::chrono::steady_clock::now();
        build_library_row(f.cat, added, row);
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - began).count();
        best_ms = std::min(best_ms, ms);
    }
    std::printf("library row: 20000 songs, %zu sorted, %zu shown, built in %.2f ms\n", row.sorted,
                row.songs.size(), best_ms);
    CHECK(row.songs.size() >= 10);
    CHECK_TIMING(best_ms < 200.0); // generous: the claim is the cost is a pass over the songs
}
