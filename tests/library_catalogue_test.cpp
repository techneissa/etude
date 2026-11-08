// SPDX-License-Identifier: GPL-3.0-or-later
#include "library_catalogue.hpp"

#include "alloc_count.hpp"
#include "song_library.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <fstream>
#include <iterator>
#include <optional>
#include <string>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

LibraryItem loaded(std::string relative, std::uint64_t hash, std::string title,
                   std::string composer = {}, int difficulty = 2, double seconds = 60.0) {
    LibraryItem item;
    item.relative = std::move(relative);
    item.stamp = hash * 7 + 1;
    item.state = EntryState::Loaded;
    item.entry.hash = hash;
    item.entry.title = std::move(title);
    item.entry.composer = std::move(composer);
    item.entry.difficulty = difficulty;
    item.entry.duration_s = seconds;
    item.entry.left_notes = 10;
    item.entry.right_notes = 20;
    return item;
}

LibraryItem pending(std::string relative) {
    LibraryItem item;
    item.relative = std::move(relative);
    item.stamp = 99;
    return item;
}

std::vector<std::string> titles(const LibraryCatalogue& c) {
    std::vector<std::string> out;
    for (const std::uint32_t i : c.shown()) {
        out.push_back(c.song(i).title);
    }
    return out;
}

struct Fixture {
    LibraryState library;
    std::vector<RunRecord> runs;
    LibraryCatalogue::Prefs prefs;
    LibraryCatalogue cat;

    Fixture() {
        library.items = {
            loaded("Classical/fur_elise.mid", 1, "Für Elise", "Ludwig van Beethoven", 2, 180),
            loaded("Classical/moonlight.mid", 2, "Moonlight Sonata", "Ludwig van Beethoven", 4,
                   360),
            loaded("Folk/greensleeves.abc", 3, "Greensleeves", "", 1, 90),
            loaded("gymnopedie.mid", 4, "Gymnopédie No. 1", "Erik Satie", 2, 200),
            pending("Jazz/04_take_five.mid"),
        };
    }
    void sync() {
        cat.library_changed();
        cat.prefs_changed();
        cat.history_changed();
        REQUIRE(cat.sync(library, runs, prefs));
        cat.update_view(20000, 100.0);
    }
};

} // namespace

TEST_CASE("catalogue: names, composers, genres from folders, a pending song by its file name") {
    Fixture f;
    f.sync();
    REQUIRE(f.cat.size() == 5);
    CHECK(f.cat.song(0).title == "Für Elise");
    CHECK(f.cat.song(0).composer == "Ludwig van Beethoven");
    CHECK(f.cat.song(0).genre == "Classical");
    CHECK(f.cat.song(0).folder == "Classical");
    CHECK(f.cat.song(0).file_name == "fur_elise.mid");
    CHECK(f.cat.song(3).genre.empty()); // in no folder: "Other"
    CHECK(f.cat.song(4).title == "Take Five");
    CHECK(f.cat.song(4).difficulty == 0);
    CHECK(f.cat.pending() == 1);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Greensleeves", "Gymnopédie No. 1",
                                                    "Moonlight Sonata", "Take Five"});
}

TEST_CASE("catalogue: the player's name, composer, genre and star win over the file's") {
    Fixture f;
    f.prefs[2] = SongPrefs{"Sonata quasi una fantasia", "Beethoven", "Romantic", true, 500};
    f.sync();
    CHECK(f.cat.song(1).title == "Sonata quasi una fantasia");
    CHECK(f.cat.song(1).renamed);
    CHECK(f.cat.song(1).file_title == "Moonlight Sonata");
    CHECK(f.cat.song(1).composer == "Beethoven");
    CHECK(f.cat.song(1).genre == "Romantic");
    CHECK(f.cat.song(1).favourite);
    CHECK(f.cat.song(1).added_s == 500);
    // Searching finds the new name, not the old one.
    f.cat.set_query("quasi");
    f.cat.update_view(20000, 101.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Sonata quasi una fantasia"});
    // The others have no date added yet: reported once each for the history to store.
    std::vector<std::uint64_t> unseen = f.cat.take_unseen();
    std::sort(unseen.begin(), unseen.end());
    CHECK(unseen == std::vector<std::uint64_t>{1, 3, 4});
    CHECK(f.cat.take_unseen().empty());
}

TEST_CASE("catalogue: sort, group and filter, with headers") {
    Fixture f;
    f.prefs[3] = SongPrefs{"", "", "", true, 0};
    f.sync();
    f.cat.set_order({BrowseSort::Length});
    f.cat.update_view(20000, 101.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Greensleeves", "Für Elise", "Gymnopédie No. 1",
                                                    "Moonlight Sonata", "Take Five"});
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::Composer});
    f.cat.update_view(20000, 102.0);
    REQUIRE(f.cat.sections().size() == 3);
    CHECK(f.cat.section_label(f.cat.sections()[0]) == "Ludwig van Beethoven");
    CHECK(f.cat.section_label(f.cat.sections()[1]) == "Erik Satie");
    CHECK(f.cat.section_label(f.cat.sections()[2]) == "Unknown composer");
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::Genre});
    f.cat.update_view(20000, 103.0);
    REQUIRE(f.cat.sections().size() == 4);
    CHECK(f.cat.section_label(f.cat.sections()[0]) == "Classical");
    CHECK(f.cat.section_label(f.cat.sections()[1]) == "Folk");
    CHECK(f.cat.section_label(f.cat.sections()[2]) == "Jazz");
    CHECK(f.cat.section_label(f.cat.sections()[3]) == "Other");
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::Difficulty});
    f.cat.update_view(20000, 104.0);
    CHECK(f.cat.section_label(f.cat.sections().front()) == "Level 1 \xC2\xB7 Easy");
    CHECK(f.cat.section_label(f.cat.sections().back()) == "Not read yet");
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Favourites});
    f.cat.update_view(20000, 105.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Greensleeves"});
    CHECK(f.cat.section_label(f.cat.sections().front()).empty());
}

TEST_CASE("catalogue: the history gives the best, the runs and recency; search ties use it") {
    Fixture f;
    RunRecord r;
    r.song_hash = 4;
    r.all = {.graded = 40, .hit = 36};
    r.local_day = 19990;
    f.runs.push_back(r);
    f.sync();
    CHECK(f.cat.song(3).runs == 1);
    CHECK(f.cat.song(3).best_permille == 900);
    CHECK(f.cat.song(3).recency == 0);
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Recent});
    f.cat.update_view(20000, 101.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Gymnopédie No. 1"});
    // A run added later is seen after history_changed().
    f.cat.set_order({});
    f.cat.set_query("beethoven");
    f.cat.update_view(20000, 102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Moonlight Sonata"});
    // A run of the sonata, seen after history_changed(), puts it first: a tie goes to the
    // recently played.
    r.song_hash = 2;
    f.runs.push_back(r);
    f.cat.history_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.cat.set_query("beethove");
    f.cat.update_view(20000, 103.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Moonlight Sonata", "Für Elise"});
}

TEST_CASE("catalogue: nothing found offers the nearest songs") {
    Fixture f;
    f.sync();
    f.cat.set_query("mxxnlxght");
    f.cat.update_view(20000, 101.0);
    CHECK(f.cat.searching());
    CHECK(f.cat.shown().empty());
    REQUIRE(!f.cat.near().empty());
    CHECK(f.cat.song(f.cat.near().front()).title == "Moonlight Sonata");
}

TEST_CASE("catalogue: a list that changes keeps each song under its name") {
    Fixture f;
    f.sync();
    // The pending song is read, and an ABC file splits into two tunes.
    f.library.items[4] = loaded("Jazz/04_take_five.mid", 5, "Take Five", "Paul Desmond", 3, 300);
    f.library.items[2] = loaded("Folk/greensleeves.abc#X=1", 3, "Greensleeves");
    f.library.items.insert(f.library.items.begin() + 3,
                           loaded("Folk/greensleeves.abc#X=2", 6, "Scarborough Fair"));
    f.cat.library_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.cat.update_view(20000, 101.0);
    REQUIRE(f.cat.size() == 6);
    CHECK(f.cat.song(3).title == "Scarborough Fair");
    // A screen holding a song by its name finds it at its new place (Take Five moved from 4 to 5).
    CHECK(f.cat.index_of("Jazz/04_take_five.mid") == std::optional<std::size_t>{5});
    CHECK(f.cat.index_of("Folk/greensleeves.abc#X=2") == std::optional<std::size_t>{3});
    CHECK_FALSE(f.cat.index_of("Folk/greensleeves.abc").has_value());
    CHECK(f.cat.song(5).composer == "Paul Desmond");
    CHECK(f.cat.pending() == 0);
    f.cat.set_query("scarb");
    f.cat.update_view(20000, 102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Scarborough Fair"});
}

TEST_CASE("catalogue: a query of punctuation alone shows every song, not nothing found") {
    Fixture f;
    f.sync();
    const std::size_t all = f.cat.shown().size();
    for (const char* q : {"-", "'", " . "}) {
        f.cat.set_query(q);
        f.cat.update_view(20000, 101.0);
        CHECK_FALSE(f.cat.searching());
        CHECK(f.cat.shown().size() == all);
        CHECK(f.cat.near().empty());
    }
}

TEST_CASE("catalogue: a shorter list is shown at once, never through the old indices") {
    Fixture f;
    f.sync();
    REQUIRE(f.cat.shown().size() == 5);
    f.library.items.resize(2);
    f.cat.library_changed();
    // Partly synced, inside the re-rank interval: the old shown list must not survive it.
    f.cat.sync(f.library, f.runs, f.prefs, 1);
    CHECK(f.cat.view_stale());
    f.cat.update_view(20000, 100.01);
    CHECK_FALSE(f.cat.view_stale());
    for (const std::uint32_t i : f.cat.shown()) {
        CHECK(i < f.cat.size());
    }
    CHECK(f.cat.shown().size() == 2);
}

TEST_CASE("catalogue: after the list is rearranged, search never finds a row by its old song") {
    Fixture f;
    // Forty songs before Greensleeves, so a slice of the rebuild (16 songs) does not reach it.
    for (std::uint64_t n = 0; n < 40; ++n) {
        f.library.items.insert(
            f.library.items.begin(),
            loaded("A/" + std::to_string(n) + ".mid", 100 + n, "Etude " + std::to_string(n)));
    }
    f.sync();
    // A song listed first shifts every row; one slice of the rebuild runs before the search.
    f.library.items.insert(f.library.items.begin(), loaded("Album/aria.mid", 9, "Aria"));
    f.cat.library_changed();
    f.cat.sync(f.library, f.runs, f.prefs, 1);
    REQUIRE_FALSE(f.cat.caught_up());
    f.cat.set_query("greensleeves");
    f.cat.update_view(20000, 101.0);
    for (const std::uint32_t i : f.cat.shown()) {
        CHECK(f.cat.song(i).title == "Greensleeves");
    }
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.cat.update_view(20000, 102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Greensleeves"});
}

TEST_CASE("catalogue: a big library is built in slices and a keystroke allocates nothing") {
    LibraryState library;
    for (std::uint64_t i = 1; i <= 3000; ++i) {
        library.items.push_back(
            loaded("Set " + std::to_string(i % 20) + "/song" + std::to_string(i) + ".mid", i,
                   "Sonata No. " + std::to_string(i), "Composer " + std::to_string(i % 97)));
    }
    LibraryCatalogue cat;
    LibraryCatalogue::Prefs prefs;
    cat.library_changed();
    int slices = 0;
    while (!cat.sync(library, {}, prefs, 1)) { // a 1 ns budget: one small slice per call
        ++slices;
        REQUIRE(slices < 10000);
    }
    CHECK(slices > 10);
    cat.update_view(20000, 100.0);
    CHECK(cat.shown().size() == 3000);
    cat.set_query("warm up");
    cat.update_view(20000, 101.0);
    etude::test::start_counting_allocations();
    double t = 102.0;
    for (const char* q : {"s", "so", "son", "sona", "sonata", "sonata 12", "sonata 123", "sonnata",
                          "composer 4", "xqzv"}) {
        cat.set_query(q);
        cat.update_view(20000, t);
        t += 1.0;
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(cat.shown().empty()); // the last query found nothing
}

TEST_CASE("catalogue: a collection, the Inbox and a group as the scope") {
    Fixture f;
    f.sync();
    LibraryScope folder;
    folder.kind = LibraryScope::Kind::Folder;
    folder.path = "Classical";
    f.cat.set_scope(folder);
    f.cat.update_view(20000, 101.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Moonlight Sonata"});
    // The groups: one holding Greensleeves and Gymnopédie (by hash).
    LibraryGroups groups;
    GroupEdit make;
    make.group.id = 77;
    make.group.name = "Recital";
    groups.apply(make);
    for (const std::uint64_t h : {3u, 4u}) {
        GroupEdit add;
        add.kind = GroupEdit::Kind::Add;
        add.group.id = 77;
        add.hash = h;
        groups.apply(add);
    }
    f.cat.set_groups(groups);
    LibraryScope group;
    group.kind = LibraryScope::Kind::Group;
    group.group = 77;
    f.cat.set_scope(group);
    f.cat.update_view(20000, 102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Greensleeves", "Gymnopédie No. 1"});
    CHECK(f.cat.group_index(77) == std::optional<std::size_t>{0});
    CHECK(f.cat.group_name(0) == "Recital");
    // My groups as the grouping: the group, then the songs in none.
    f.cat.set_scope({});
    BrowseOrder o;
    o.group = BrowseGroup::MyGroups;
    f.cat.set_order(o);
    f.cat.update_view(20000, 103.0);
    REQUIRE(f.cat.sections().size() == 2);
    CHECK(f.cat.section_label(f.cat.sections()[0]) == "Recital");
    CHECK(f.cat.section_label(f.cat.sections()[1]) == "Not in a group");
    // The Inbox: songs listed lately, never played, in no group (Für Elise and Moonlight).
    f.prefs[1].added_utc_s = 1'000'000;
    f.prefs[2].added_utc_s = 1'000'000;
    f.prefs[3].added_utc_s = 1'000'000; // grouped: not new
    f.cat.set_order({});
    f.cat.set_now(1'000'000 + 3600 * 5);
    f.sync();
    CHECK(f.cat.inbox_count() == 2);
    LibraryScope inbox;
    inbox.kind = LibraryScope::Kind::Inbox;
    f.cat.set_scope(inbox);
    f.cat.update_view(20000, 104.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Moonlight Sonata"});
}

TEST_CASE("catalogue: shelves: recent, favourites, in progress, the Inbox, collections, groups") {
    Fixture f;
    f.prefs[2].favourite = true;
    f.sync();
    std::vector<std::string> labels;
    for (const LibraryShelf& s : f.cat.shelves()) {
        labels.emplace_back(f.cat.shelf_label(s));
    }
    // No runs, no Inbox dates: the favourites, then the collections (folders at the top), then
    // the songs in none of them.
    CHECK(labels ==
          std::vector<std::string>{"Favourites", "Classical", "Folk", "Jazz", "Songs"});
    const LibraryShelf& classical = f.cat.shelves()[1];
    CHECK(classical.total == 2);
    CHECK(f.cat.collections().size() == 3);
    CHECK(f.cat.shelves()[4].kind == LibraryShelf::Kind::Here);
    // Inside a collection, its own songs are the shelf named after it.
    LibraryScope folk;
    folk.kind = LibraryScope::Kind::Folder;
    folk.path = "Folk";
    f.cat.set_scope(folk);
    f.cat.update_view(20000, 100.5);
    REQUIRE_FALSE(f.cat.shelves().empty());
    CHECK(f.cat.shelf_label(f.cat.shelves().back()) == "Folk");
    f.cat.set_scope({});
    // Grouped, the shelves are the sections.
    BrowseOrder o;
    o.group = BrowseGroup::Difficulty;
    f.cat.set_order(o);
    f.cat.update_view(20000, 101.0);
    CHECK(f.cat.shelves().size() == f.cat.sections().size());
    CHECK(f.cat.shelves()[0].kind == LibraryShelf::Kind::Section);
}

TEST_CASE("catalogue: the A to Z index, only for a list sorted by name") {
    Fixture f;
    f.sync();
    REQUIRE(f.cat.has_alpha());
    CHECK(f.cat.alpha()[static_cast<std::size_t>('f' - 'a')] == 0); // Für Elise first
    CHECK(f.cat.alpha()[static_cast<std::size_t>('m' - 'a')] >= 0);
    BrowseOrder o;
    o.sort = BrowseSort::Length;
    f.cat.set_order(o);
    f.cat.update_view(20000, 101.0);
    CHECK_FALSE(f.cat.has_alpha());
}

// ---- Versions of one piece (#164)

namespace {

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

// Which of the two versions the list shows.
const char* shown_version(const LibraryCatalogue& c) {
    const std::vector<std::string> t = titles(c);
    const bool elise = std::find(t.begin(), t.end(), "Für Elise") != t.end();
    const bool moonlight = std::find(t.begin(), t.end(), "Moonlight Sonata") != t.end();
    if (elise && !moonlight) {
        return "Für Elise";
    }
    return moonlight && !elise ? "Moonlight Sonata" : "both or neither";
}

// The fixture's Für Elise (1) and Moonlight Sonata (2) as two versions of one piece.
struct VersionFixture : Fixture {
    VersionFixture() { cat.set_versions(index_of({{{1, "Easy"}, {2, "Hard"}}})); }
    void again(double t = 101.0) {
        cat.update_view(20000, t);
    }
};

} // namespace

TEST_CASE("catalogue versions: the versions of one piece are one card, the easiest by default") {
    VersionFixture f;
    f.sync();
    CHECK(f.cat.has_versions());
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Greensleeves", "Gymnopédie No. 1",
                                                    "Take Five"});
    const std::span<const std::uint32_t> versions = f.cat.versions_of(0);
    REQUIRE(versions.size() == 2);
    CHECK(versions[0] == 0);
    CHECK(versions[1] == 1);
    CHECK(f.cat.versions_of(1).size() == 2); // from either version
    CHECK(f.cat.version_name(0) == "Easy");
    CHECK(f.cat.version_name(1) == "Hard");
    CHECK(f.cat.card_of(1) == 0);
    CHECK(f.cat.card_of(2) == 2); // on its own
    CHECK(f.cat.versions_of(2).empty());
    CHECK(f.cat.version_name(2).empty());
}

TEST_CASE("catalogue versions: the card shows the version chosen last, else the one played last") {
    VersionFixture f;
    f.sync();
    CHECK(f.cat.card_of(0) == 0);
    // Played last: a run of the hard one.
    RunRecord r;
    r.song_hash = 2;
    r.all = {.graded = 40, .hit = 36};
    r.local_day = 19990;
    f.runs.push_back(r);
    f.cat.history_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again();
    CHECK(std::string(shown_version(f.cat)) == "Moonlight Sonata");
    // Chosen: the easy one, whatever was played.
    f.prefs[1].chosen_utc_s = 1000;
    f.cat.prefs_changed(1);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again(102.0);
    CHECK(std::string(shown_version(f.cat)) == "Für Elise");
    // The later choice wins.
    f.prefs[2].chosen_utc_s = 1001;
    f.cat.prefs_changed(2);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again(103.0);
    CHECK(std::string(shown_version(f.cat)) == "Moonlight Sonata");
    CHECK(f.cat.card_of(0) == 1);
    CHECK(f.cat.song(1).chosen_s == 1001);
}

TEST_CASE("catalogue versions: the words on the chips and the card are made once, and follow the scores and names") {
    VersionFixture f;
    f.sync();
    const std::uint64_t words = f.cat.version_words_version();
    CHECK(f.cat.version_name(1) == "Hard");
    CHECK(f.cat.version_scored(1) == "Hard"); // no run yet: no score
    // Both versions show the shortest of their titles; the same piece name from either.
    CHECK(f.cat.card_title(0) == "Für Elise");
    CHECK(f.cat.card_title(1) == "Für Elise");
    CHECK(f.cat.card_title(2) == "Greensleeves"); // on its own: its own title
    // A read does not make the words again.
    f.again(102.0);
    CHECK(f.cat.version_words_version() == words);
    // A run: the best score joins the word.
    RunRecord r;
    r.song_hash = 2;
    r.all = {.graded = 40, .hit = 36};
    r.local_day = 19990;
    f.runs.push_back(r);
    f.cat.history_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again(103.0);
    CHECK(f.cat.version_words_version() != words);
    CHECK(f.cat.version_scored(1) == "Hard " + std::to_string((f.cat.song(1).best_permille + 5) / 10) + "%");
    CHECK(f.cat.version_scored(0) == "Easy");
    // A name the player gave: a level word on it is not part of the piece's name.
    f.prefs[1].title = "Für Elise (Easy)";
    f.cat.prefs_changed(1);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again(104.0);
    CHECK(f.cat.card_title(0) == "Für Elise");
    CHECK(f.cat.card_title(1) == "Für Elise");
    // Setting the grouping off gives every song its own title and no words.
    f.cat.set_group_versions(false);
    f.again(105.0);
    CHECK(f.cat.version_name(1).empty());
    CHECK(f.cat.card_title(1) == "Moonlight Sonata");
}

TEST_CASE("catalogue versions: with the setting off every file is a card of its own") {
    VersionFixture f;
    f.sync();
    f.cat.set_group_versions(false);
    f.again();
    CHECK_FALSE(f.cat.has_versions());
    CHECK(titles(f.cat).size() == 5);
    CHECK(f.cat.versions_of(0).empty());
    CHECK(f.cat.card_of(1) == 1);
    f.cat.set_group_versions(true);
    f.again(102.0);
    CHECK(titles(f.cat).size() == 4);
}

TEST_CASE("catalogue versions: a filter keeps the version that passes it") {
    VersionFixture f;
    f.prefs[2] = SongPrefs{"", "", "", true, 0}; // the hard one is a favourite
    f.prefs[1].chosen_utc_s = 5000;                // and the easy one is chosen
    f.sync();
    CHECK(std::string(shown_version(f.cat)) == "Für Elise");
    // The piece is a favourite when any version is (#215), so the card stays the one chosen last.
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::Favourites});
    f.again();
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise"});
    // The easy one at level 2 only: the level filter leaves the hard one (level 4) out.
    f.cat.set_order({BrowseSort::Name, false, BrowseGroup::None, BrowseFilter::All, 0b00010});
    f.again(102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise", "Gymnopédie No. 1"});
}

TEST_CASE("catalogue versions: a search finds a piece by any of its versions, once") {
    VersionFixture f;
    f.sync();
    // Both versions match: the card.
    f.cat.set_query("beethoven");
    f.again();
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise"});
    // Only the hard one matches: it stands for the piece, though the card is the easy one.
    f.cat.set_query("moonlight");
    f.again(102.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Moonlight Sonata"});
    // The card is the hard one now, and the easy one's name finds the piece as the hard one.
    f.prefs[2].chosen_utc_s = 7;
    f.cat.prefs_changed(2);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.cat.set_query("elise");
    f.again(103.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Für Elise"}); // only the easy one matches
    f.cat.set_query("beethoven");
    f.again(104.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Moonlight Sonata"});
    f.cat.set_query("greensleeves");
    f.again(105.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Greensleeves"});
}

namespace {

// A group holding the songs of these content hashes.
LibraryGroups groups_of(std::uint64_t id, const char* name, std::initializer_list<std::uint64_t> hashes) {
    LibraryGroups groups;
    GroupEdit make;
    make.group.id = id;
    make.group.name = name;
    groups.apply(make);
    for (const std::uint64_t h : hashes) {
        GroupEdit add;
        add.kind = GroupEdit::Kind::Add;
        add.group.id = id;
        add.hash = h;
        groups.apply(add);
    }
    return groups;
}

} // namespace

TEST_CASE("catalogue versions: a piece is in the player's group when any version is, shown once") {
    VersionFixture f;
    // Moonlight Sonata (the hard version of the piece) is in the group; the card is Für Elise.
    f.cat.set_groups(groups_of(77, "Recital", {2}));
    f.sync();
    BrowseOrder o;
    o.group = BrowseGroup::MyGroups;
    f.cat.set_order(o);
    f.again();
    REQUIRE(f.cat.sections().size() == 2);
    CHECK(f.cat.section_label(f.cat.sections()[0]) == "Recital");
    CHECK(f.cat.sections()[0].end - f.cat.sections()[0].begin == 1);
    CHECK(f.cat.song(f.cat.shown()[f.cat.sections()[0].begin]).title == "Für Elise");
    // The others, in none; the piece is not listed twice.
    CHECK(f.cat.sections()[1].end - f.cat.sections()[1].begin == 3);
    CHECK(f.cat.shown().size() == 4);
    // Both versions in it: still one card.
    f.cat.set_groups(groups_of(77, "Recital", {1, 2}));
    f.again(102.0);
    CHECK(f.cat.sections()[0].end - f.cat.sections()[0].begin == 1);
    // The group itself as the scope shows the version that is in it, whichever was chosen last.
    f.cat.set_groups(groups_of(77, "Recital", {2}));
    f.prefs[1].chosen_utc_s = 9000; // the easy one is chosen, but it is not in the group
    f.cat.prefs_changed(1);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    LibraryScope group;
    group.kind = LibraryScope::Kind::Group;
    group.group = 77;
    f.cat.set_order({});
    f.cat.set_scope(group);
    f.again(103.0);
    CHECK(titles(f.cat) == std::vector<std::string>{"Moonlight Sonata"});
}

TEST_CASE("catalogue versions: a piece with one version in the library, or a file held twice, is not a group") {
    Fixture f;
    // The pass knew a third version that is not in the library.
    f.cat.set_versions(index_of({{{1, "Easy"}, {99, "Hard"}}}));
    f.sync();
    CHECK_FALSE(f.cat.has_versions());
    CHECK(f.cat.versions_of(0).empty());
    CHECK(titles(f.cat).size() == 5);
    // The same bytes under two names: neither is a version of anything, each is a card of its own
    // (a folder shows the copy that is in it).
    f.library.items.push_back(loaded("Copies/elise.mid", 1, "Für Elise (copy)", "Ludwig van Beethoven"));
    f.cat.set_versions(index_of({{{1, "Easy"}, {2, "Hard"}}}));
    f.cat.library_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.cat.update_view(20000, 101.0);
    CHECK_FALSE(f.cat.has_versions());
    CHECK(f.cat.versions_of(0).empty());
    CHECK(f.cat.versions_of(5).empty());
    CHECK(titles(f.cat).size() == 6);
}

TEST_CASE("catalogue versions: what grouping reads bumps the facts' version, a star does not") {
    Fixture f;
    f.sync();
    const std::uint64_t v0 = f.cat.facts_version();
    f.prefs[3].favourite = true;
    f.cat.prefs_changed(3);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    CHECK(f.cat.facts_version() == v0);
    f.prefs[3].title = "Greensleeves (Easy)";
    f.cat.prefs_changed(3);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    CHECK(f.cat.facts_version() > v0);
    const std::uint64_t v1 = f.cat.facts_version();
    f.library.items[3].entry.half_stars = 7;
    f.library.items[3].stamp += 1;
    f.cat.library_changed();
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    CHECK(f.cat.facts_version() > v1);
}

TEST_CASE("catalogue versions: a keystroke still allocates nothing with the pieces grouped") {
    LibraryState library;
    std::vector<std::vector<std::pair<std::uint64_t, std::string>>> groups;
    for (std::uint64_t i = 1; i <= 3000; ++i) {
        library.items.push_back(
            loaded("Set " + std::to_string(i % 20) + "/song" + std::to_string(i) + ".mid", i,
                   "Sonata No. " + std::to_string(i / 3), "Composer " + std::to_string(i % 97)));
        if (i % 3 == 0) {
            groups.push_back({{i - 2, "Easy"}, {i - 1, "Medium"}, {i, "Hard"}});
        }
    }
    LibraryCatalogue cat;
    LibraryCatalogue::Prefs prefs;
    cat.set_versions(index_of(groups));
    cat.library_changed();
    while (!cat.sync(library, {}, prefs, 1'000'000)) {
    }
    cat.update_view(20000, 100.0);
    CHECK(cat.has_versions());
    CHECK(cat.shown().size() == 1000);
    cat.set_query("warm up");
    cat.update_view(20000, 101.0);
    etude::test::start_counting_allocations();
    double t = 102.0;
    for (const char* q : {"s", "so", "son", "sona", "sonata", "sonata 12", "sonata 123", "sonnata",
                          "composer 4", "xqzv", "sonata"}) {
        cat.set_query(q);
        cat.update_view(20000, t);
        t += 1.0;
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(cat.shown().size() == 1000); // each piece once for "sonata"
}

TEST_CASE("catalogue versions: a piece is a favourite, and in the Favourites filter, when any version is") {
    VersionFixture f; // Für Elise (1) and Moonlight Sonata (2) as one piece
    f.prefs[2].favourite = true;
    f.cat.prefs_changed(2);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    BrowseOrder o;
    o.filter = BrowseFilter::Favourites;
    f.cat.set_order(o);
    f.again();
    // Only the hard version is a favourite, but the heart is on for both and the piece shows.
    CHECK_FALSE(f.cat.song(0).favourite);
    CHECK(f.cat.song(0).piece_favourite);
    CHECK(f.cat.song(1).piece_favourite);
    CHECK_FALSE(f.cat.song(2).piece_favourite);
    REQUIRE(f.cat.shown().size() == 1);
    CHECK(f.cat.has_versions());
    std::vector<std::size_t> piece;
    f.cat.piece_songs(0, piece);
    CHECK(piece == std::vector<std::size_t>{0, 1});
    f.cat.piece_songs(2, piece);
    CHECK(piece == std::vector<std::size_t>{2});
    // Cleared on every version: gone from the filter.
    f.prefs[2].favourite = false;
    f.cat.prefs_changed(2);
    REQUIRE(f.cat.sync(f.library, f.runs, f.prefs));
    f.again(102.0);
    CHECK(f.cat.shown().empty());
}

// ---- What the Melodies row reads

TEST_CASE("catalogue: the facts version changes when the hands, the keys, the bars or the notes change, "
          "not only the rating (an ABC tune's chords turn on a left hand)") {
    // The same tune read with the abc_chords setting off and on: one content hash, the same rating
    // (checked below), but a left hand where there was none.
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/tests/data/abc-reel-repeats.abc", std::ios::binary);
    REQUIRE(in.good());
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const std::vector<std::uint8_t> bytes(text.begin(), text.end());
    const SongFileLoad plain = load_song_bytes(bytes, "reel.abc");
    SongLoadOptions on;
    on.abc_chords = true;
    const SongFileLoad chords = load_song_bytes(bytes, "reel.abc", on);
    REQUIRE(plain.ok);
    REQUIRE(chords.ok);
    REQUIRE(chords.entry.hash == plain.entry.hash);
    REQUIRE(chords.entry.left_notes > 0);
    REQUIRE(plain.entry.left_notes == 0);

    LibraryState library;
    LibraryItem item;
    item.relative = "reel.abc";
    item.stamp = 1;
    item.state = EntryState::Loaded;
    item.entry = plain.entry;
    library.items = {item};
    std::vector<RunRecord> runs;
    LibraryCatalogue::Prefs prefs;
    LibraryCatalogue cat;
    const auto sync = [&] {
        cat.library_changed();
        REQUIRE(cat.sync(library, runs, prefs));
    };
    sync();
    const std::uint64_t first = cat.facts_version();
    sync();
    CHECK(cat.facts_version() == first); // nothing changed: nothing to pass on
    // The chords on: the same hash and rating, a left hand, more notes.
    item.entry = chords.entry;
    item.entry.half_stars = plain.entry.half_stars;
    library.items = {item};
    sync();
    CHECK(cat.facts_version() > first);
    CHECK(cat.song(0).left);
    // Each fact alone, with everything else as it was.
    const auto bumps = [&](auto change) {
        const std::uint64_t before = cat.facts_version();
        change(library.items[0].entry);
        sync();
        return cat.facts_version() > before;
    };
    CHECK(bumps([](SongEntry& e) { e.left_keys += 3; }));
    CHECK(bumps([](SongEntry& e) { e.right_keys += 3; }));
    CHECK(bumps([](SongEntry& e) { e.bar_count += 2; }));
    CHECK(bumps([](SongEntry& e) { e.note_count += 5; }));
    CHECK(bumps([](SongEntry& e) { e.left_notes = 0; }));
    CHECK_FALSE(bumps([](SongEntry& e) { e.duration_s += 1.0; })); // a length is no fact the row reads
}
