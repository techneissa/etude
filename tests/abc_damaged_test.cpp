// SPDX-License-Identifier: GPL-3.0-or-later
// abc: damaged (split from abc_test.cpp).
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

namespace {

ScoreLoadResult load_data(const std::string& name, const AbcOptions& options = {}) {
    const Bytes b = read_data(name);
    return load_abc(b.data(), b.size(), options);
}

} // namespace

// ---- Damaged input

TEST_CASE("abc: damaged and odd input loads what it can, with warnings") {
    const ScoreLoadResult junk = load_music("C D # E ? F @ G ] A [ B c");
    REQUIRE(junk.ok);
    CHECK(pitches(junk.song) == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});
    CHECK(mentions(junk.warnings, "could not read"));

    const ScoreLoadResult unclosed = load_music("C D [EG F \"unclosed A {gab B !oops C |]");
    REQUIRE(unclosed.ok);
    CHECK(unclosed.song.notes.size() >= 3);

    const ScoreLoadResult bad_key = load_music("CDEF|", "M:4/4\nL:1/4\nK:Hx");
    REQUIRE(bad_key.ok);
    CHECK(mentions(bad_key.warnings, "key"));
    const ScoreLoadResult bad_len = load_music("CDEF|", "M:4/4\nL:7\nK:C");
    REQUIRE(bad_len.ok);
    CHECK(mentions(bad_len.warnings, "length"));

    // No notes is an error, with a message.
    const ScoreLoadResult empty = load_music("z8 | z8 |]");
    CHECK_FALSE(empty.ok);
    CHECK(empty.error == ScoreFileError::NoNotes);
    const ScoreLoadResult only_header = abc("X:1\nT:t\nK:C\n");
    CHECK_FALSE(only_header.ok);
    CHECK(only_header.error == ScoreFileError::NoNotes);
}

TEST_CASE("abc: notes outside the piano are left out with a count") {
    const ScoreLoadResult r = load_music("C,,,,,, C c''''''' D |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62});
    CHECK(r.stats.unreadable_notes == 2);
    CHECK(mentions(r.warnings, "range"));
}

TEST_CASE("abc: absurd numbers and lengths are clamped, never a hang or an overflow") {
    const ScoreLoadResult r =
        load_music("C99999999999999999999 D/99999999999999 E999999/1 Z999999999 F |]");
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() >= 1);
    const ScoreLoadResult tup = load_music("(99999999:99999999:99999999 CDEF |]");
    REQUIRE(tup.ok);
    const ScoreLoadResult header = load_music("CDEF|]", "M:99999999999/4\nL:99999999999/1\nK:C");
    REQUIRE(header.ok);
}

TEST_CASE("abc: a very long tune stops at the note cap with a warning") {
    std::string text = "X:1\nT:big\nM:4/4\nL:1/8\nK:C\n";
    for (std::size_t i = 0; i < kMaxScoreNotes / 8 + 2000; ++i) {
        text += "CDEF GABc |";
        if (i % 6 == 5) {
            text += "\n";
        }
    }
    text += "]\n";
    const ScoreLoadResult r = abc(text);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() <= kMaxScoreNotes);
    CHECK(r.song.notes.size() > kMaxScoreNotes - 16);
    CHECK(mentions(r.warnings, "more notes"));
}

TEST_CASE("abc: a very long line, a huge number of bars and of voices stay linear") {
    std::string line;
    for (int i = 0; i < 100000; ++i) {
        line += "C";
    }
    CHECK(load_music(line).ok);
    std::string many;
    for (int i = 0; i < 120000; ++i) {
        many += "C8|";
    }
    const ScoreLoadResult b = load_music(many);
    REQUIRE(b.ok);
    CHECK(b.song.measures.size() <= kMaxSongMeasures);
    std::string voices = "X:1\nT:t\nK:C\n";
    for (int i = 0; i < 200; ++i) {
        voices += "V:" + std::to_string(i) + "\nC4|]\n";
    }
    const ScoreLoadResult v = abc(voices);
    REQUIRE(v.ok);
    CHECK(v.song.parts.size() <= 64);
    CHECK(mentions(v.warnings, "voices"));
}

TEST_CASE("abc: every byte value in the music is handled") {
    for (int c = 1; c < 256; ++c) {
        if (c == '\n' || c == '\r') {
            continue;
        }
        std::string music = "CD";
        music += static_cast<char>(c);
        music += "E";
        const ScoreLoadResult r = load_music(music);
        CAPTURE(c);
        CHECK(r.ok);
    }
}

// ---- The test tunes against hand-written MusicXML of the same music

namespace {

ScoreLoadResult load_xml_data(const std::string& name) {
    const Bytes b = read_data(name);
    return load_musicxml(b.data(), b.size());
}

// Both loaders must hear the same music: pitch, place in time, bar numbers in playing order and the
// written bar, hand, the tempo and the bars.
void expect_same_music(const ScoreLoadResult& a, const ScoreLoadResult& b) {
    REQUIRE(a.ok);
    REQUIRE(b.ok);
    REQUIRE(a.song.notes.size() == b.song.notes.size());
    for (std::size_t i = 0; i < a.song.notes.size(); ++i) {
        const Note& x = a.song.notes[i];
        const Note& y = b.song.notes[i];
        CAPTURE(i);
        CHECK(x.pitch == y.pitch);
        CHECK(x.start_s == doctest::Approx(y.start_s).epsilon(1e-9));
        CHECK(x.duration_s == doctest::Approx(y.duration_s).epsilon(1e-9));
        CHECK(x.measure == y.measure);
        CHECK(x.written_measure() == y.written_measure());
        CHECK(x.hand == y.hand);
        CHECK(x.velocity == y.velocity);
    }
    REQUIRE(a.song.measures.size() == b.song.measures.size());
    for (std::size_t i = 0; i < a.song.measures.size(); ++i) {
        const Measure& x = a.song.measures[i];
        const Measure& y = b.song.measures[i];
        CAPTURE(i);
        CHECK(x.number == y.number);
        CHECK(x.written_number() == y.written_number());
        CHECK(x.start_s == doctest::Approx(y.start_s).epsilon(1e-9));
        CHECK(x.numerator == y.numerator);
        CHECK(x.denominator == y.denominator);
    }
    REQUIRE(a.song.tempo.changes.size() == b.song.tempo.changes.size());
    for (std::size_t i = 0; i < a.song.tempo.changes.size(); ++i) {
        CHECK(a.song.tempo.changes[i].us_per_quarter == b.song.tempo.changes[i].us_per_quarter);
    }
    CHECK(a.song.duration_s == doctest::Approx(b.song.duration_s).epsilon(1e-9));
    CHECK(a.stats.bars_written == b.stats.bars_written);
    CHECK(a.stats.bars_played == b.stats.bars_played);
    CHECK(a.title == b.title);
}

} // namespace

TEST_CASE("abc: the reel with repeats and endings plays like its MusicXML twin") {
    const ScoreLoadResult a = load_data("abc-reel-repeats.abc");
    const ScoreLoadResult x = load_xml_data("abc-reel-repeats.musicxml");
    expect_same_music(a, x);
    REQUIRE(a.ok);
    CHECK(a.title == "Hillside Reel");
    CHECK(a.composer == "written for etude, CC0");
    CHECK(a.stats.bars_written == 12);
    CHECK(a.stats.bars_played == 22);
    CHECK(a.song.notes.size() == 106);
    CHECK(played_bars(a.song) ==
          std::vector<int>{1, 2, 3, 4, 5, 6, 7, 1, 2, 3, 4, 5, 6, 8, 9, 10, 11, 12, 9, 10, 11, 12});
    CHECK(a.stats.repeats_followed == 2);
    CHECK(a.warnings.empty());
    for (const Note& n : a.song.notes) {
        CHECK(n.hand == Hand::Unknown); // one line: the hand split puts it whole in one hand
    }
    CHECK(a.song.tempo.changes[0].us_per_quarter == 535714); // 112 quarter notes a minute
}

TEST_CASE("abc: the D dorian tune plays like its MusicXML twin") {
    const ScoreLoadResult a = load_data("abc-dorian.abc");
    const ScoreLoadResult x = load_xml_data("abc-dorian.musicxml");
    expect_same_music(a, x);
    REQUIRE(a.ok);
    CHECK(a.song.measures.size() == 9);
    CHECK(a.song.measures[0].number == 0); // the pickup
    CHECK(a.song.measures[1].number == 1);
    CHECK(a.song.notes.size() == 29);
    // No C sharp and no F sharp in the key; the F sharp of the third bar lasts only in its bar and
    // is carried to the second F of the fourth bar after a natural one.
    std::vector<int> f_line;
    for (const Note& n : a.song.notes) {
        if (n.measure == 4) {
            f_line.push_back(n.pitch);
        }
    }
    CHECK(f_line == std::vector<int>{65, 66, 66});
    // The tied C joined: 5 eighths across the last two bars.
    bool tied = false;
    for (const Note& n : a.song.notes) {
        if (n.pitch == 72 && n.measure == 7) {
            tied = true;
            CHECK(length_of(a.song, n) == doctest::Approx(2.5));
        }
    }
    CHECK(tied);
    CHECK(a.stats.ties_merged == 1);
    CHECK(a.song.tempo.changes[0].us_per_quarter == 666667);
}

TEST_CASE("abc: the triplet tune plays like its MusicXML twin") {
    const ScoreLoadResult a = load_data("abc-triplets.abc");
    const ScoreLoadResult x = load_xml_data("abc-triplets.musicxml");
    expect_same_music(a, x);
    REQUIRE(a.ok);
    CHECK(a.song.measures.size() == 4);
    CHECK(a.song.notes.size() == 26);
    CHECK(a.song.notes[0].pitch == 60);
    // Bb in the key of F, in the third bar.
    int flats = 0;
    for (const Note& n : a.song.notes) {
        flats += n.pitch == 70 ? 1 : 0;
    }
    CHECK(flats == 2);
    CHECK(length_of(a.song, a.song.notes[0]) == doctest::Approx(1.0 / 3));
    CHECK(length_of(a.song, a.song.notes[3]) == doctest::Approx(0.75)); // F> is a dotted eighth
    CHECK(length_of(a.song, a.song.notes[4]) == doctest::Approx(0.25));
}

TEST_CASE("abc: the two-voice tune plays like its MusicXML twin, hands included") {
    const ScoreLoadResult a = load_data("abc-two-voices.abc");
    const ScoreLoadResult x = load_xml_data("abc-two-voices.musicxml");
    expect_same_music(a, x);
    REQUIRE(a.ok);
    CHECK(a.song.parts.size() == 2);
    CHECK(a.song.measures.size() == 8);
    CHECK(a.song.notes.size() == 40);
    int right = 0;
    int left = 0;
    for (const Note& n : a.song.notes) {
        (n.hand == Hand::Right ? right : left) += 1;
    }
    CHECK(right == 16);
    CHECK(left == 24);
    CHECK(a.stats.bars_written == 4);
    CHECK(a.stats.bars_played == 8);
}

TEST_CASE("abc: the collection file gives three tunes") {
    const Bytes data = read_data("abc-collection.abc");
    AbcOptions options;
    options.tune = "X=1";
    const ScoreLoadResult up = load_abc(data, options);
    REQUIRE(up.ok);
    CHECK(pitches(up.song) == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});
    CHECK(up.song.measures.size() == 2);
}
