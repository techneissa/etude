// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/library_search.hpp"

#include "alloc_count.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <string>
#include <vector>

using namespace etude;

namespace {

struct Song {
    std::string title;
    std::string composer;
    std::string tags;
    std::string folder;
    std::string file;
};

SearchIndex index_of(const std::vector<Song>& songs) {
    std::vector<SearchDoc> docs;
    for (const Song& s : songs) {
        docs.push_back({s.title, s.composer, s.tags, s.folder, s.file});
    }
    SearchIndex index;
    index.build(docs);
    return index;
}

std::vector<std::uint32_t> songs_of(const std::vector<SearchHit>& hits) {
    std::vector<std::uint32_t> out;
    for (const SearchHit& h : hits) {
        out.push_back(h.song);
    }
    return out;
}

const SearchHit* find(const std::vector<SearchHit>& hits, std::uint32_t song) {
    for (const SearchHit& h : hits) {
        if (h.song == song) {
            return &h;
        }
    }
    return nullptr;
}

const std::vector<Song> kSongs = {
    {"Für Elise", "Ludwig van Beethoven", "", "Classical", "fur_elise.mid"},           // 0
    {"Moonlight Sonata", "Ludwig van Beethoven", "", "Classical", "moonlight.mid"},    // 1
    {"Piano Sonata No. 14, Op. 27 No. 2", "Ludwig van Beethoven", "", "", "op27.mxl"}, // 2
    {"Gymnopédie No. 1", "Erik Satie", "", "", "gymno.mid"},                           // 3
    {"Prelude in C", "Johann Sebastian Bach", "Baroque", "", "bwv846.mid"},            // 4
    {"Mood Indigo", "Duke Ellington", "Jazz", "", "mood.mid"},                         // 5
    {"Jesu, Joy of Man's Desiring", "Bach", "", "", "jesu.mid"},                       // 6
    {"Waltz", "", "", "Recital/Spring", "waltz-in-a.mid"},                             // 7
};

} // namespace

TEST_CASE("search: case and accents do not matter") {
    SearchIndex index = index_of(kSongs);
    CHECK(songs_of(index.search("fur elise")) == std::vector<std::uint32_t>{0});
    CHECK(songs_of(index.search("FÜR ELISE")) == std::vector<std::uint32_t>{0});
    CHECK(songs_of(index.search("gymnopedie")) == std::vector<std::uint32_t>{3});
}

TEST_CASE("search: a query word finds the words it begins") {
    SearchIndex index = index_of(kSongs);
    const auto& hits = index.search("moon");
    REQUIRE(!hits.empty());
    CHECK(hits.front().song == 1);
    CHECK(hits.front().rank == MatchRank::TitlePrefix);
    // "sona" begins "Sonata" in two titles, neither at its start.
    const auto& sona = index.search("sona");
    REQUIRE(find(sona, 1) != nullptr);
    CHECK(find(sona, 1)->rank == MatchRank::TitleWord);
    CHECK(find(sona, 2) != nullptr);
}

TEST_CASE("search: every query word must match") {
    SearchIndex index = index_of(kSongs);
    CHECK(songs_of(index.search("moonlight elise")).empty());
    CHECK(songs_of(index.search("sonata beethoven")) == std::vector<std::uint32_t>{1, 2});
}

TEST_CASE("search: initials find the title") {
    SearchIndex index = index_of(kSongs);
    const auto& hits = index.search("ms");
    REQUIRE(find(hits, 1) != nullptr);
    CHECK(find(hits, 1)->rank == MatchRank::TitleWord);
    CHECK(find(hits, 1)->cost == 1);
    // Words with digits are not part of the initials: Piano Sonata No. 14, Op. 27 No. 2 is "psnon".
    CHECK(find(index.search("psno"), 2) != nullptr);
    // One letter is not an initials search.
    CHECK(find(index.search("m"), 1) != nullptr); // it begins "Moonlight" instead
    CHECK(find(index.search("j"), 1) == nullptr);
}

TEST_CASE("search: typos are forgiven by the length of the word") {
    SearchIndex index = index_of(kSongs);
    // Four letters: one edit.
    CHECK(find(index.search("moob"), 1) != nullptr);
    CHECK(find(index.search("moob"), 1)->rank == MatchRank::Typo);
    // Three letters: none ("mon" begins no word; "moo" would).
    CHECK(find(index.search("mon"), 1) == nullptr);
    // Seven letters: one edit, not two.
    CHECK(find(index.search("beethovn"), 0) != nullptr); // 8 letters, 1 edit
    CHECK(find(index.search("bethovn"), 0) == nullptr);  // 7 letters, 2 edits from any beginning
    CHECK(typos_allowed(3) == 0);
    CHECK(typos_allowed(4) == 1);
    CHECK(typos_allowed(7) == 1);
    CHECK(typos_allowed(8) == 2);
    // A beginning of the word counts: "bethov" is one edit from "beethov".
    CHECK(prefix_edit_distance("bethovn", "beethoven", 2) == 2);
    CHECK(prefix_edit_distance("bethov", "beethoven", 1) == 1);
    // Eight letters: two edits.
    CHECK(find(index.search("moonlite"), 1) != nullptr);
    CHECK(find(index.search("moonlite"), 1)->rank == MatchRank::Typo);
    CHECK(find(index.search("moonlite"), 1)->cost == 5);
    // Three edits are too many even for a long word.
    CHECK(find(index.search("mxxxlight"), 1) == nullptr);
}

TEST_CASE("search: a typo is only forgiven in the title, the composer and the tags") {
    SearchIndex index = index_of(kSongs);
    // "Recital" is a folder: found as a beginning, never with a typo.
    CHECK(find(index.search("recit"), 7) != nullptr);
    CHECK(find(index.search("recitel"), 7) == nullptr);
}

TEST_CASE("search: numbers and number words") {
    SearchIndex index = index_of(kSongs);
    CHECK(songs_of(index.search("op 27")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("Op. 27")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("opus 27")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("op27")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("no 2")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("number 14")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("nº 14")) == std::vector<std::uint32_t>{2});
    // A digit is never a typo: 28 is not 27.
    CHECK(songs_of(index.search("op 28")).empty());
    // A number begins a longer one as a word begins a word.
    CHECK(songs_of(index.search("op 2")) == std::vector<std::uint32_t>{2});
}

TEST_CASE("search: an apostrophe does not split a word") {
    SearchIndex index = index_of(kSongs);
    CHECK(songs_of(index.search("mans")) == std::vector<std::uint32_t>{6});
    CHECK(songs_of(index.search("man's")) == std::vector<std::uint32_t>{6});
    CHECK(songs_of(index.search("man’s desiring")) == std::vector<std::uint32_t>{6});
}

TEST_CASE("search: the ranks come in the order of the rules") {
    const std::vector<Song> songs = {
        {"Moon River Dance", "", "", "", ""},  // 0 title word ("river" not at the start)
        {"River", "", "", "", ""},             // 1 exact title
        {"Rivers of Babylon", "", "", "", ""}, // 2 title prefix
        {"Song", "Bob River", "", "", ""},     // 3 composer
        {"Rover", "", "", "", ""},             // 4 a typo: "river" has 5 letters, "rover" is 1 edit
        {"Waltz", "", "", "River songs", ""},  // 5 folder
        {"Tune", "", "riverside", "", ""},     // 6 tag
    };
    SearchIndex index = index_of(songs);
    const auto& hits = index.search("river");
    REQUIRE(hits.size() == 7);
    CHECK(hits[0].song == 1);
    CHECK(hits[0].rank == MatchRank::ExactTitle);
    CHECK(hits[1].song == 2);
    CHECK(hits[1].rank == MatchRank::TitlePrefix);
    CHECK(hits[2].song == 0);
    CHECK(hits[2].rank == MatchRank::TitleWord);
    // Composer and tags share a rank; an exact word costs less than a beginning.
    CHECK(hits[3].song == 3);
    CHECK(hits[3].rank == MatchRank::Composer);
    CHECK(hits[4].song == 6);
    CHECK(hits[4].rank == MatchRank::Composer);
    CHECK(hits[5].song == 4);
    CHECK(hits[5].rank == MatchRank::Typo);
    CHECK(hits[6].song == 5);
    CHECK(hits[6].rank == MatchRank::FolderOrFile);
}

TEST_CASE("search: a whole title typed out ranks above one it begins") {
    const std::vector<Song> songs = {{"Waltz in A minor", "", "", "", ""},
                                     {"Waltz", "", "", "", ""}};
    SearchIndex index = index_of(songs);
    CHECK(songs_of(index.search("waltz")) == std::vector<std::uint32_t>{1, 0});
    CHECK(index.search("waltz in a min").front().rank == MatchRank::TitlePrefix);
    CHECK(index.search("waltz in a minor").front().rank == MatchRank::ExactTitle);
}

TEST_CASE("search: ties go to the recently played, then the favourites, then the order given") {
    const std::vector<Song> songs = {{"Etude 1", "", "", "", ""},
                                     {"Etude 2", "", "", "", ""},
                                     {"Etude 3", "", "", "", ""},
                                     {"Etude 4", "", "", "", ""}};
    SearchIndex index = index_of(songs);
    CHECK(songs_of(index.search("etude")) == std::vector<std::uint32_t>{0, 1, 2, 3});
    const std::vector<std::int32_t> recency = {-1, 5, -1, 9};
    const std::vector<std::uint8_t> favourite = {0, 0, 1, 0};
    CHECK(songs_of(index.search("etude", {recency, favourite})) ==
          std::vector<std::uint32_t>{3, 1, 2, 0});
}

TEST_CASE("search: `include` leaves songs out, an empty query finds nothing") {
    SearchIndex index = index_of(kSongs);
    const std::vector<std::uint8_t> include = {1, 0, 1, 1, 1, 1, 1, 1};
    CHECK(songs_of(index.search("beethoven", {}, include)) == std::vector<std::uint32_t>{0, 2});
    CHECK(index.search("").empty());
    CHECK(index.search("  ...  ").empty());
}

TEST_CASE("search: a song can be replaced and the index grows") {
    SearchIndex index = index_of(kSongs);
    index.set(3, {"Arabesque No. 1", "Claude Debussy", "", "", ""});
    CHECK(songs_of(index.search("gymnopedie")).empty());
    CHECK(songs_of(index.search("debussy")) == std::vector<std::uint32_t>{3});
    index.set(10, {"Clair de Lune", "Claude Debussy", "", "", ""});
    CHECK(index.size() == 11);
    CHECK(songs_of(index.search("debussy")) == std::vector<std::uint32_t>{3, 10});
    // Many replacements compact the word lists and keep every song findable.
    for (int i = 0; i < 50; ++i) {
        index.set(10, {"Clair de Lune " + std::to_string(i), "Claude Debussy", "", "", ""});
    }
    CHECK(songs_of(index.search("clair 49")) == std::vector<std::uint32_t>{10});
    CHECK(songs_of(index.search("fur elise")) == std::vector<std::uint32_t>{0});
    index.resize(4);
    CHECK(songs_of(index.search("debussy")) == std::vector<std::uint32_t>{3});
}

TEST_CASE("search: closest matches for a query nothing matches") {
    SearchIndex index = index_of(kSongs);
    CHECK(index.search("mxxnlxght sxnxta").empty());
    const auto& near = index.closest("mxxnlxght sxnxta", 3);
    REQUIRE(!near.empty());
    CHECK(near.front().song == 1);
    CHECK(near.front().rank == MatchRank::Typo);
    // One word of two found still counts, after songs that found both.
    const auto& half = index.closest("moonlight zzzz", 3);
    REQUIRE(!half.empty());
    CHECK(half.front().song == 1);
    CHECK(half.front().cost == kClosestMissCost);
    CHECK(index.closest("qqqqqqqq", 3).empty());
    CHECK(index.closest("sonata", 1).size() == 1);
}

TEST_CASE("search: the edit distance") {
    CHECK(bounded_edit_distance("kitten", "sitting", 3) == 3);
    CHECK(bounded_edit_distance("kitten", "sitting", 2) == 3); // over: max + 1
    CHECK(bounded_edit_distance("", "abc", 5) == 3);
    CHECK(bounded_edit_distance("abc", "abc", 0) == 0);
    CHECK(bounded_edit_distance("abc", "abd", 0) == 1);
    CHECK(prefix_edit_distance("moonlite", "moonlight", 2) == 2);
    CHECK(prefix_edit_distance("moon", "moonlight", 0) == 0);
    CHECK(prefix_edit_distance("", "anything", 0) == 0);
    CHECK(prefix_edit_distance("abcdefgh", "abc", 2) == 3);
}

TEST_CASE("search: a keystroke allocates nothing once the index is built") {
    std::vector<Song> songs;
    for (int i = 0; i < 400; ++i) {
        songs.push_back({"Sonata No. " + std::to_string(i) + " in C",
                         "Composer " + std::to_string(i % 37), "", "Folder",
                         "file" + std::to_string(i) + ".mid"});
    }
    SearchIndex index = index_of(songs);
    const std::vector<std::int32_t> recency(songs.size(), -1);
    const std::vector<std::uint8_t> favourite(songs.size(), 0);
    const std::vector<std::uint8_t> include(songs.size(), 1);
    const SearchTies ties{recency, favourite};
    index.search("warm up", ties, include);
    index.closest("warm up", 6, ties, include);
    etude::test::start_counting_allocations();
    for (const char* q : {"s", "so", "son", "sona", "sonat", "sonata", "sonata n", "sonata no 1",
                          "sonata no 12 composer 4", "sonnata", "xyzzy qq", "op 27 no. 2"}) {
        index.search(q, ties, include);
        index.closest(q, 6, ties, include);
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
}

TEST_CASE("search: punctuation beyond ASCII ends a word; other characters are letters") {
    SearchIndex index = index_of({
        {"Prelude\xE2\x80\x93"
         "Fugue",
         "", "", "", ""}, // 0: an en dash between the words
        {"Waltz \xC2\xAB"
         "Minute\xC2\xBB",
         "", "", "", ""}, // 1: guillemets
        {"Etude 3\xC3\x97"
         "4",
         "", "", "", ""},                                         // 2: a multiplication sign
        {"\xE3\x81\x95\xE3\x81\x8F\xE3\x82\x89", "", "", "", ""}, // 3: Japanese letters, one word
        {"Sonata n\xC2\xBA 3", "", "", "", ""},                   // 4: the ordinal reads as "no"
    });
    CHECK(songs_of(index.search("fugue")) == std::vector<std::uint32_t>{0});
    CHECK(songs_of(index.search("minute")) == std::vector<std::uint32_t>{1});
    CHECK(songs_of(index.search("4")) == std::vector<std::uint32_t>{2});
    CHECK(songs_of(index.search("\xE3\x81\x95\xE3\x81\x8F")) == std::vector<std::uint32_t>{3});
    CHECK(songs_of(index.search("no 3")) == std::vector<std::uint32_t>{4});
}
