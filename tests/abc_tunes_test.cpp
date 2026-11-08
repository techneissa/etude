// SPDX-License-Identifier: GPL-3.0-or-later
// abc: tunes (split from abc_test.cpp).
#include "etude/abc.hpp"
#include "etude/content_hash.hpp"
#include "etude/hand_split.hpp"
#include "etude/library.hpp"
#include "etude/library_state.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

#include "abc_test_support.hpp"

using namespace etude;
using namespace abc_test_support;

// ---- Text handling

TEST_CASE("abc: line ends, a byte order mark, comments and continuation lines") {
    const std::string unix_text = "X:1\nT:t\nM:4/4\nL:1/8\nK:C\nCDEF GABc |]\n";
    const ScoreLoadResult a = abc(unix_text);
    REQUIRE(a.ok);
    for (const char* eol : {"\r\n", "\r"}) {
        std::string text;
        for (char c : unix_text) {
            if (c == '\n') {
                text += eol;
            } else {
                text += c;
            }
        }
        const ScoreLoadResult r = abc(text);
        REQUIRE(r.ok);
        CHECK(pitches(r.song) == pitches(a.song));
    }
    const ScoreLoadResult bom = abc("\xEF\xBB\xBF" + unix_text);
    REQUIRE(bom.ok);
    CHECK(pitches(bom.song) == pitches(a.song));
    const ScoreLoadResult comments =
        abc("X:1 % the number\nT:t % the title\nK:C\nCDEF % to here\\\nGABc |]\n");
    REQUIRE(comments.ok);
    CHECK(pitches(comments.song) == pitches(a.song));
    CHECK(comments.title == "t");
    // The last line needs no end of line.
    const ScoreLoadResult bare = abc("X:1\nT:t\nK:C\nCDEF");
    REQUIRE(bare.ok);
    CHECK(bare.song.notes.size() == 4);
}

TEST_CASE("abc: a title that is not UTF-8 is read as Latin-1") {
    const ScoreLoadResult r = abc("X:1\nT:Caf\xE9 du Nord\nK:C\nCDEF|]\n");
    REQUIRE(r.ok);
    CHECK(r.title == "Caf\xC3\xA9 du Nord");
    const ScoreLoadResult utf8 = abc("X:1\nT:Caf\xC3\xA9\nK:C\nCDEF|]\n");
    REQUIRE(utf8.ok);
    CHECK(utf8.title == "Caf\xC3\xA9");
}

TEST_CASE("abc: a blank line ends the tune, and a missing K: is a warning") {
    const ScoreLoadResult r =
        abc("X:1\nT:t\nM:4/4\nL:1/8\nK:C\nCDEF GABc|]\n\nthis is free text CDEF\n");
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 8);
    const ScoreLoadResult nok = abc("X:1\nT:t\nM:4/4\nL:1/4\nCDEF|]\n");
    REQUIRE(nok.ok);
    CHECK(nok.song.notes.size() == 4);
    CHECK(mentions(nok.warnings, "K:"));
}

TEST_CASE("abc: the file header sets defaults for every tune") {
    const ScoreLoadResult r = abc("M:3/4\nL:1/4\nQ:1/4=60\nK:G\n\nX:1\nT:t\nK:G\nF G A|]\n");
    REQUIRE(r.ok);
    CHECK(length_of(r.song, r.song.notes[0]) == doctest::Approx(1.0));
    CHECK(r.song.measures[0].numerator == 3);
    CHECK(r.song.tempo.changes[0].us_per_quarter == 1000000);
    // A tune's own fields win.
    const ScoreLoadResult own = abc("L:1/4\nQ:1/4=60\n\nX:1\nT:t\nL:1/8\nQ:1/4=120\nK:C\nCDEF|]\n");
    REQUIRE(own.ok);
    CHECK(length_of(own.song, own.song.notes[0]) == doctest::Approx(0.5));
    REQUIRE(own.song.tempo.changes.size() == 1);
    CHECK(own.song.tempo.changes[0].us_per_quarter == 500000);
}

// ---- Several tunes in a file

TEST_CASE("abc: list_abc_tunes addresses each tune of a file") {
    const Bytes data = read_data("abc-collection.abc");
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == 3);
    CHECK(tunes[0].address == "X=1");
    CHECK(tunes[1].address == "X=2");
    CHECK(tunes[2].address == "X=10");
    CHECK(tunes[0].title == "Scale Up");
    CHECK(tunes[1].title == "Scale Down");
    CHECK(tunes[2].title == "Arpeggio");
    CHECK(tunes[2].number == 10);
    CHECK(tunes[0].header_end == tunes[0].begin);
    CHECK(tunes[0].line > 1);
    // The tunes tile the file after its header.
    CHECK(tunes[0].end == tunes[1].begin);
    CHECK(tunes[1].end == tunes[2].begin);
    CHECK(tunes[2].end == data.size());
    std::vector<std::uint64_t> hashes;
    for (const AbcTune& t : tunes) {
        hashes.push_back(t.hash);
    }
    CHECK(hashes[0] != hashes[1]);
    CHECK(hashes[1] != hashes[2]);
}

TEST_CASE("abc: each tune loads by its address, with the file header's defaults") {
    const Bytes data = read_data("abc-collection.abc");
    AbcOptions options;
    options.tune = "X=2";
    ScoreLoadResult two = load_abc(data, options);
    REQUIRE(two.ok);
    CHECK(two.title == "Scale Down");
    CHECK(pitches(two.song) == std::vector<int>{72, 71, 69, 67, 65, 64, 62, 60});
    expect_lengths(two.song, {1, 1, 1, 1, 1, 1, 1, 1}); // the file header says L:1/4
    options.tune = "X=10";
    ScoreLoadResult ten = load_abc(data, options);
    REQUIRE(ten.ok);
    CHECK(ten.title == "Arpeggio");
    CHECK(pitches(ten.song) == std::vector<int>{67, 71, 74, 79, 74, 71, 67, 71});
    // No address: the first tune.
    ScoreLoadResult first = load_abc(data);
    REQUIRE(first.ok);
    CHECK(first.title == "Scale Up");
    // A tune the file does not have is an error, not another tune.
    options.tune = "X=3";
    ScoreLoadResult none = load_abc(data, options);
    CHECK_FALSE(none.ok);
    CHECK(none.error == ScoreFileError::NotScore);
    CHECK_FALSE(none.message.empty());
    // A listed tune loads without listing again.
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    AbcOptions listed;
    listed.listed = &tunes[1];
    ScoreLoadResult again = load_abc(data, listed);
    REQUIRE(again.ok);
    CHECK(again.title == "Scale Down");
    // A reference that does not fit the bytes is refused.
    AbcTune stale = tunes[2];
    stale.end = data.size() + 100;
    listed.listed = &stale;
    CHECK_FALSE(load_abc(data, listed).ok);
}

TEST_CASE("abc: a tune's identity is the hash of the file header and its own text") {
    const std::string one = "X:1\nT:solo\nK:C\nCDEF|]\n";
    const Bytes solo(one.begin(), one.end());
    const std::vector<AbcTune> a = list_abc_tunes(solo);
    REQUIRE(a.size() == 1);
    // A file with one tune and no header is hashed as the whole file, as every other format is.
    CHECK(a[0].hash == content_hash(solo.data(), solo.size()));

    const std::string two_tunes = "X:1\nT:solo\nK:C\nCDEF|]\n\nX:2\nT:other\nK:G\nGABc|]\n";
    const Bytes both(two_tunes.begin(), two_tunes.end());
    const std::vector<AbcTune> b = list_abc_tunes(both);
    REQUIRE(b.size() == 2);
    // Adding or editing another tune leaves a tune's identity alone ...
    const std::string edited =
        "X:1\nT:solo\nK:C\nCDEF|]\n\nX:2\nT:other\nK:G\nGABd|]\nX:3\nT:new\nK:D\nD|]\n";
    const std::vector<AbcTune> c = list_abc_tunes(Bytes(edited.begin(), edited.end()));
    REQUIRE(c.size() == 3);
    CHECK(c[0].hash == b[0].hash);
    CHECK(c[1].hash != b[1].hash);
    // ... and a tune whose own text changes, even its X: number, is another song.
    const std::string moved = "X:7\nT:other\nK:G\nGABc|]\n";
    CHECK(list_abc_tunes(Bytes(moved.begin(), moved.end()))[0].hash != b[1].hash);
    // The file header is part of the identity: it changes how the tune sounds.
    const std::string with_header = "L:1/4\n\n" + one;
    const std::vector<AbcTune> d = list_abc_tunes(Bytes(with_header.begin(), with_header.end()));
    REQUIRE(d.size() == 1);
    CHECK(d[0].hash != a[0].hash);
}

TEST_CASE("abc: tune numbers that repeat get a second address") {
    const std::string text =
        "X:1\nT:a\nK:C\nC|]\n\nX:1\nT:b\nK:C\nD|]\n\nX:2\nT:c\nK:C\nE|]\n\nX:1\nT:d\nK:C\nF|]\n";
    const Bytes data(text.begin(), text.end());
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == 4);
    CHECK(tunes[0].address == "X=1");
    CHECK(tunes[1].address == "X=1.2");
    CHECK(tunes[2].address == "X=2");
    CHECK(tunes[3].address == "X=1.3");
    AbcOptions options;
    options.tune = "X=1.2";
    ScoreLoadResult r = load_abc(data, options);
    REQUIRE(r.ok);
    CHECK(r.title == "b");
    CHECK(pitches(r.song) == std::vector<int>{62});
}

TEST_CASE("abc: no X: line, no tune") {
    CHECK(list_abc_tunes(Bytes{}).empty());
    const std::string text = "T:no number\nK:C\nCDEF|]\n";
    CHECK(list_abc_tunes(Bytes(text.begin(), text.end())).empty());
    const ScoreLoadResult r = abc(text);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NotScore);
    CHECK_FALSE(abc("").ok);
    CHECK_FALSE(load_abc(nullptr, 0).ok);
}

TEST_CASE("abc: a collection lists a thousand tunes quickly and caps at kMaxAbcTunes") {
    std::string text;
    for (int i = 1; i <= 1200; ++i) {
        text += "X:" + std::to_string(i) + "\nT:tune " + std::to_string(i) + "\nK:C\nCDEF|]\n\n";
    }
    const Bytes data(text.begin(), text.end());
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == 1200);
    CHECK(tunes[1199].address == "X=1200");
    AbcOptions options;
    options.listed = &tunes[777];
    const ScoreLoadResult r = load_abc(data, options);
    REQUIRE(r.ok);
    CHECK(r.title == "tune 778");

    std::string big;
    for (std::size_t i = 0; i < kMaxAbcTunes + 50; ++i) {
        big += "X:" + std::to_string(i) + "\nK:C\nC|]\n";
    }
    CHECK(list_abc_tunes(Bytes(big.begin(), big.end())).size() == kMaxAbcTunes);
}

TEST_CASE("abc: the last tune under the cap ends where the next one starts") {
    // Tune i plays one note whose pitch tells the tune: 40 + i % 40.
    std::string text;
    std::vector<std::size_t> starts;
    for (std::size_t i = 1; i <= kMaxAbcTunes + 20; ++i) {
        starts.push_back(text.size());
        text += "X:" + std::to_string(i) + "\nT:tune " + std::to_string(i) + "\nK:C\n" +
                std::string(1, "CDEFGAB"[i % 7]) + "|]\n\n";
    }
    const Bytes data(text.begin(), text.end());
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == kMaxAbcTunes);
    const AbcTune& last = tunes.back();
    CHECK(last.address == "X=" + std::to_string(kMaxAbcTunes));
    CHECK(last.begin == starts[kMaxAbcTunes - 1]);
    CHECK(last.end == starts[kMaxAbcTunes]); // not the end of the file
    CHECK(last.hash == content_hash(data.data() + last.begin, last.end - last.begin));
    CHECK(last.more_tunes_follow);
    CHECK_FALSE(tunes[kMaxAbcTunes - 2].more_tunes_follow);
    AbcOptions options;
    options.tune = last.address;
    const ScoreLoadResult r = load_abc(data, options);
    REQUIRE(r.ok);
    CHECK(r.title == "tune 5000");
    CHECK(r.song.notes.size() == 1); // its own note, not the twenty tunes after it
    CHECK(pitches(r.song) ==
          std::vector<int>{60 + std::vector<int>{0, 2, 4, 5, 7, 9, 11}[kMaxAbcTunes % 7]});
    CHECK(mentions(r.warnings, "Only the first 5000 tunes"));
    // A file of exactly the cap lists them all and says nothing.
    const std::string exact = text.substr(0, starts[kMaxAbcTunes]);
    const std::vector<AbcTune> all = list_abc_tunes(Bytes(exact.begin(), exact.end()));
    REQUIRE(all.size() == kMaxAbcTunes);
    CHECK_FALSE(all.back().more_tunes_follow);
    CHECK(all.back().end == exact.size());
}

TEST_CASE("abc: an X: number above the cap is addressed by its place in the file") {
    const std::string text =
        "X:7\nT:a\nK:C\nC|]\n\nX:99999999999999999999\nT:b\nK:C\nD|]\n\nX:1000001\nT:c\nK:C\nE|]"
        "\n\n"
        "X:1000000\nT:d\nK:C\nF|]\n\nX:2\nT:e\nK:C\nG|]\n\nX:99999999999\nT:f\nK:C\nA|]\n";
    const Bytes data(text.begin(), text.end());
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    REQUIRE(tunes.size() == 6);
    CHECK(tunes[0].address == "X=7");
    CHECK(tunes[1].address == "X=2"); // its place is 2: the second tune
    CHECK(tunes[1].number_replaced);
    CHECK(tunes[2].address == "X=3");
    CHECK(tunes[3].address == "X=1000000"); // the cap itself is kept as written
    CHECK_FALSE(tunes[3].number_replaced);
    CHECK(tunes[4].address == "X=2.2");
    CHECK_FALSE(tunes[4].number_replaced);
    CHECK(tunes[5].address == "X=6");
    // The addresses are distinct, so every tune loads as itself, with a warning on the replaced.
    AbcOptions options;
    for (const AbcTune& t : tunes) {
        options.tune = t.address;
        const ScoreLoadResult r = load_abc(data, options);
        REQUIRE(r.ok);
        CHECK(r.title == std::string(1, "abcdef"[&t - tunes.data()]));
        CHECK(mentions(r.warnings, "too large") == t.number_replaced);
    }
}

TEST_CASE(
    "abc: repeated X: numbers are numbered by their order in the file, so a reorder swaps them") {
    const std::string one = "X:1\nT:first\nK:C\nC|]\n\nX:1\nT:second\nK:C\nD|]\n";
    const std::string swapped = "X:1\nT:second\nK:C\nD|]\n\nX:1\nT:first\nK:C\nC|]\n";
    AbcOptions options;
    options.tune = "X=1.2";
    const ScoreLoadResult a =
        load_abc(reinterpret_cast<const std::uint8_t*>(one.data()), one.size(), options);
    const ScoreLoadResult b =
        load_abc(reinterpret_cast<const std::uint8_t*>(swapped.data()), swapped.size(), options);
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    CHECK(a.title == "second");
    CHECK(b.title == "first");
}

TEST_CASE("abc: the address in a path") {
    auto split = [](const std::string& text) {
        const AbcLocation l = split_abc_location(text);
        return std::pair<std::string, std::string>{std::string(l.file), std::string(l.tune)};
    };
    using P = std::pair<std::string, std::string>;
    CHECK(split("folk.abc#X=3") == P{"folk.abc", "X=3"});
    CHECK(split("a/b/folk.abc#X=12.2") == P{"a/b/folk.abc", "X=12.2"});
    CHECK(split("folk.abc") == P{"folk.abc", ""});
    CHECK(split("c#minor.abc") == P{"c#minor.abc", ""});
    CHECK(split("c#.abc#X=1") == P{"c#.abc", "X=1"});
    CHECK(split("folk.abc#X=") == P{"folk.abc#X=", ""});
    CHECK(split("folk.abc#X=3.") == P{"folk.abc#X=3.", ""});
    CHECK(split("folk.abc#Y=3") == P{"folk.abc#Y=3", ""});
    CHECK(split("folk.abc#X=3x") == P{"folk.abc#X=3x", ""});
    CHECK(split("") == P{"", ""});
    CHECK(join_abc_location("folk.abc", "X=3") == "folk.abc#X=3");
    CHECK(join_abc_location("folk.abc", "") == "folk.abc");
}

TEST_CASE("abc: the library lists the tunes of a file together, in tune order") {
    std::vector<LibraryFile> files = {{"b.abc", 1, 1},     {"a.abc#X=10", 1, 1},
                                      {"a.abc#X=2", 1, 1}, {"a.abc#X=1.2", 1, 1},
                                      {"a.abc#X=1", 1, 1}, {"A2.mid", 1, 1}};
    const LibraryState state = list_state(files);
    std::vector<std::string> order;
    for (const LibraryItem& i : state.items) {
        order.push_back(i.relative);
    }
    CHECK(order == std::vector<std::string>{"a.abc#X=1", "a.abc#X=1.2", "a.abc#X=2", "a.abc#X=10",
                                            "A2.mid", "b.abc"});
}

// ---- Sniffing and the library entry

TEST_CASE("abc: content sniffing") {
    auto sniff = [](const std::string& text) {
        return sniff_song_format(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    };
    CHECK(sniff("X:1\nT:t\nK:C\nCDEF|]\n") == SongFileFormat::Abc);
    CHECK(sniff("%abc-2.1\nX:1\nK:C\nC\n") == SongFileFormat::Abc);
    CHECK(sniff("\xEF\xBB\xBF"
                "X:1\nK:C\nC\n") == SongFileFormat::Abc);
    CHECK(sniff("% a comment\nL:1/8\n\nX:1\nT:t\nK:G\nG\n") == SongFileFormat::Abc);
    CHECK(sniff("hello world, this is text") == SongFileFormat::Unknown);
    CHECK(sniff("X:1\nno key line here\nCDEF\n") == SongFileFormat::Unknown);
    CHECK(sniff("K:C\nCDEF\n") == SongFileFormat::Unknown);
    CHECK(sniff(std::string("X:1\nK:C\n\0\x01\x02", 12)) == SongFileFormat::Unknown);
    CHECK(sniff("<?xml version=\"1.0\"?><a/>") == SongFileFormat::Xml);
    CHECK(sniff("MThd....") == SongFileFormat::Midi);
    const std::string abc_text = "X:1\nT:t\nK:C\nCDEF|]\n";
    const ScoreLoadResult r =
        load_score(reinterpret_cast<const std::uint8_t*>(abc_text.data()), abc_text.size());
    CHECK(r.ok);
    CHECK(r.song.notes.size() == 4);
}

TEST_CASE("abc: a title after the key line still names the tune in the library") {
    const std::string text = "X:1\nK:C\nT:Late title\nCDEF|]\n";
    const Bytes data(text.begin(), text.end());
    const LibraryLoad r = load_library_entry(data, "late.abc");
    REQUIRE(r.ok);
    CHECK(r.entry.title == "Late title");
}

TEST_CASE("abc: the library entry is named after the tune") {
    const Bytes data = read_data("abc-collection.abc");
    LibraryLoadOptions options;
    options.tune = "X=2";
    const LibraryLoad second = load_library_entry(data, "tunes.abc", options);
    REQUIRE(second.ok);
    CHECK(second.format == SongFileFormat::Abc);
    CHECK(second.entry.title == "Scale Down"); // the tune's T: wins over the file name
    CHECK(second.entry.note_count == 8);
    CHECK(second.entry.bar_count == 2);
    const std::vector<AbcTune> tunes = list_abc_tunes(data);
    CHECK(second.entry.hash == tunes[1].hash);
    const LibraryLoad first = load_library_entry(data, "tunes.abc");
    REQUIRE(first.ok);
    CHECK(first.entry.title == "Scale Up");
    CHECK(first.entry.hash == tunes[0].hash);
    options.tune = "X=9";
    CHECK_FALSE(load_library_entry(data, "tunes.abc", options).ok);

    // A tune without a T: takes the file's name, and the address when the file holds several.
    const std::string untitled = "X:1\nK:C\nCDEF|]\n\nX:2\nK:C\nGABc|]\n";
    const Bytes ub(untitled.begin(), untitled.end());
    options.tune = "X=2";
    const LibraryLoad u = load_library_entry(ub, "my_tunes.abc", options);
    REQUIRE(u.ok);
    CHECK(u.entry.title == "My Tunes X=2");
    const std::string single = "X:1\nK:C\nCDEF|]\n";
    const LibraryLoad s = load_library_entry(Bytes(single.begin(), single.end()), "lonely.abc");
    REQUIRE(s.ok);
    CHECK(s.entry.title == "Lonely");
}

TEST_CASE("abc: a single line goes whole to one hand; with chord symbols both hands play") {
    const Bytes data = read_data("abc-reel-repeats.abc");
    const LibraryLoad plain = load_library_entry(data, "reel.abc");
    REQUIRE(plain.ok);
    CHECK(plain.entry.note_count > 0);
    // The melody is one line: it is never cut between hands.
    CHECK((plain.entry.left_notes == 0 || plain.entry.right_notes == 0));
    CHECK(plain.entry.unknown_notes == 0);
    LibraryLoadOptions options;
    options.abc_chords = true;
    const LibraryLoad both = load_library_entry(data, "reel.abc", options);
    REQUIRE(both.ok);
    CHECK(both.entry.right_notes == plain.entry.note_count);
    CHECK(both.entry.left_notes > 0);
    CHECK(both.entry.unknown_notes == 0);
    CHECK(both.entry.hash == plain.entry.hash); // the same song, the same history
}
