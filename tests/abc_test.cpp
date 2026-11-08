// SPDX-License-Identifier: GPL-3.0-or-later
// ABC notation: the header fields, notes, keys, accidentals, lengths, ties, tuplets, chords, bar
// lines and repeats, voices, chord symbols as a left-hand part, files with several tunes, damaged
// input, and the test tunes in tests/data checked against hand-written MusicXML of the same music.
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

// ---- Header

TEST_CASE("abc: the title, the composer, the meter and the tempo come from the header") {
    const ScoreLoadResult r =
        abc("X:3\nT:First title\nT:Second title\nC:A. Composer\nM:3/4\nL:1/4\n"
            "Q:1/4=90\nK:C\nCDE|]\n");
    REQUIRE(r.ok);
    CHECK(r.title == "First title");
    CHECK(r.composer == "A. Composer");
    REQUIRE(r.song.time_signatures.size() == 1);
    CHECK(r.song.time_signatures[0].numerator == 3);
    CHECK(r.song.time_signatures[0].denominator == 4);
    REQUIRE(r.song.tempo.changes.size() == 1);
    CHECK(r.song.tempo.changes[0].us_per_quarter == 666667); // 90 quarter notes a minute
    CHECK(r.stats.bars_written == 1);
}

TEST_CASE("abc: meter forms: C, C|, none and sums") {
    auto sig = [](const std::string& m) {
        const ScoreLoadResult r = load_music("CDEF|", "M:" + m + "\nL:1/4\nK:C");
        REQUIRE(r.ok);
        return std::pair<int, int>{r.song.measures[0].numerator, r.song.measures[0].denominator};
    };
    CHECK(sig("C") == std::pair<int, int>{4, 4});
    CHECK(sig("C|") == std::pair<int, int>{2, 2});
    CHECK(sig("6/8") == std::pair<int, int>{6, 8});
    CHECK(sig("(2+3+2)/8") == std::pair<int, int>{7, 8});
    CHECK(sig("2+3/8") == std::pair<int, int>{5, 8});
    CHECK(sig("none") == std::pair<int, int>{4, 4});
    const ScoreLoadResult bad = load_music("CDEF|", "M:7/5\nL:1/4\nK:C");
    REQUIRE(bad.ok);
    CHECK(bad.song.measures[0].denominator == 4);
    CHECK(mentions(bad.warnings, "meter"));
}

TEST_CASE("abc: tempo forms") {
    auto us = [](const std::string& q) {
        const ScoreLoadResult r = load_music("CDEF|", "M:4/4\nL:1/4\nQ:" + q + "\nK:C");
        REQUIRE(r.ok);
        return r.song.tempo.changes[0].us_per_quarter;
    };
    CHECK(us("1/4=120") == 500000);
    CHECK(us("1/2=60") == 500000); // 60 half notes a minute is 120 quarter notes
    CHECK(us("3/8=60") == 666667); // 90 quarter notes a minute
    CHECK(us("\"Allegro\" 1/4=240") == 250000);
    CHECK(us("1/4=100 \"slow\"") == 600000);
    CHECK(us("1/4 3/8=60") == 1000000); // the first beat length counts
    CHECK(us("120") == 500000);         // a bare number counts quarter notes
    const ScoreLoadResult none = load_music("CDEF|");
    REQUIRE(none.ok);
    CHECK(none.song.tempo.changes[0].us_per_quarter == 500000); // 120 when nothing says
    const ScoreLoadResult bad = load_music("CDEF|", "M:4/4\nL:1/4\nQ:fast\nK:C");
    REQUIRE(bad.ok);
    CHECK(bad.song.tempo.changes[0].us_per_quarter == 500000);
    CHECK(mentions(bad.warnings, "tempo"));
}

TEST_CASE("abc: the default note length follows the meter unless L: says") {
    // Under 3/4 it is a sixteenth; from 3/4 up an eighth.
    const ScoreLoadResult two_four = load_music("CDEF|", "M:2/4\nK:C");
    REQUIRE(two_four.ok);
    CHECK(length_of(two_four.song, two_four.song.notes[0]) == doctest::Approx(0.25));
    const ScoreLoadResult six_eight = load_music("CDEF FED|", "M:6/8\nK:C");
    REQUIRE(six_eight.ok);
    CHECK(length_of(six_eight.song, six_eight.song.notes[0]) == doctest::Approx(0.5));
    const ScoreLoadResult three_four = load_music("CDE|", "M:3/4\nK:C");
    REQUIRE(three_four.ok);
    CHECK(length_of(three_four.song, three_four.song.notes[0]) == doctest::Approx(0.5));
    const ScoreLoadResult given = load_music("CDEF|", "M:2/4\nL:1/4\nK:C");
    REQUIRE(given.ok);
    CHECK(length_of(given.song, given.song.notes[0]) == doctest::Approx(1.0));
}

// ---- Pitches

TEST_CASE("abc: note letters and octaves") {
    const ScoreLoadResult r = load_music("CDEF GABc | c' d' C, D,, |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72, 84, 86, 48, 38});
}

TEST_CASE("abc: key signatures, modes and their accidentals") {
    auto scale = [](const std::string& key) {
        const ScoreLoadResult r = load_music("CDEFGABc|", "M:4/4\nL:1/8\nK:" + key);
        REQUIRE_MESSAGE(r.ok, key);
        return pitches(r.song);
    };
    CHECK(scale("C") == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});
    CHECK(scale("G") == std::vector<int>{60, 62, 64, 66, 67, 69, 71, 72});  // F#
    CHECK(scale("D") == std::vector<int>{61, 62, 64, 66, 67, 69, 71, 73});  // F# C#
    CHECK(scale("F") == std::vector<int>{60, 62, 64, 65, 67, 69, 70, 72});  // Bb
    CHECK(scale("Bb") == std::vector<int>{60, 62, 63, 65, 67, 69, 70, 72}); // Bb Eb
    CHECK(scale("Am") == scale("C"));
    CHECK(scale("A minor") == scale("C"));
    CHECK(scale("Ddor") == scale("C"));
    CHECK(scale("D Dorian") == scale("C"));
    CHECK(scale("Gdor") == scale("F"));
    CHECK(scale("Amix") == scale("D"));
    CHECK(scale("Em") == scale("G"));
    CHECK(scale("Cm") == std::vector<int>{60, 62, 63, 65, 67, 68, 70, 72}); // Bb Eb Ab
    CHECK(scale("F#m") == scale("A"));
    CHECK(scale("Ephr") == scale("C"));
    CHECK(scale("Flyd") == scale("C"));
    CHECK(scale("Blocrian") == scale("C"));
    CHECK(scale("none") == scale("C"));
    CHECK(scale("HP") == scale("D"));
    CHECK(scale("") == scale("C"));
}

TEST_CASE("abc: a lower-case tonic and the case of mode names") {
    auto scale = [](const std::string& key) {
        const ScoreLoadResult r = load_music("CDEFGABc|", "M:4/4\nL:1/8\nK:" + key);
        REQUIRE_MESSAGE(r.ok, key);
        return pitches(r.song);
    };
    CHECK(scale("gm") == scale("Gm"));
    CHECK(scale("gm") == scale("Bb"));
    CHECK(scale("d") == scale("D"));
    CHECK(scale("f#m") == scale("A"));
    CHECK(scale("bb") == scale("Bb"));
    CHECK(scale("a dor") == scale("G"));
    CHECK(scale("D MIN") == scale("Dm"));
    CHECK(scale("D Maj") == scale("D"));
    CHECK(scale("D Dor") == scale("C"));
    CHECK(scale("D MIX") == scale("G"));
    CHECK(scale("Dm") == scale("F"));
    // The words that start with a note letter are still not tonics.
    CHECK(scale("exp ^f") == scale("G"));
    CHECK(scale("bass") == scale("C"));
    CHECK(scale("C bass") == scale("C"));
    CHECK(scale("gm exp ^f") == scale("G"));
    const ScoreLoadResult lower = load_music("CDEF|", "M:4/4\nL:1/4\nK:gm");
    REQUIRE(lower.ok);
    CHECK(lower.warnings.empty());
}

TEST_CASE("abc: a lone M is major with a warning, a lone m is minor") {
    auto key = [](const std::string& text) {
        const ScoreLoadResult r = load_music("CDEFGABc|", "M:4/4\nL:1/8\nK:" + text);
        REQUIRE_MESSAGE(r.ok, text);
        if (!r.warnings.empty()) {
            CHECK(r.warnings[0].find("M read as major") != std::string::npos);
            CHECK(r.warnings[0].find("not understood") == std::string::npos);
        }
        return std::pair{pitches(r.song), r.warnings.size()};
    };
    const auto upper = key("D M");
    CHECK(upper.first == key("D").first); // major
    CHECK(upper.second == 1);
    const auto attached = key("DM");
    CHECK(attached.first == key("D").first);
    CHECK(attached.second == 1);
    const auto lower = key("D m");
    CHECK(lower.first == key("F").first); // minor: one flat
    CHECK(lower.second == 0);
    CHECK(key("Dm").second == 0);
}

TEST_CASE("abc: explicit accidentals on the key line") {
    auto scale = [](const std::string& key) {
        const ScoreLoadResult r = load_music("CDEFGABc|", "M:4/4\nL:1/8\nK:" + key);
        REQUIRE_MESSAGE(r.ok, key);
        return pitches(r.song);
    };
    CHECK(scale("D exp _b") == std::vector<int>{60, 62, 64, 65, 67, 69, 70, 72}); // only Bb
    CHECK(scale("C ^f") == std::vector<int>{60, 62, 64, 66, 67, 69, 71, 72});     // F# on top
    CHECK(scale("G =f") == std::vector<int>{60, 62, 64, 65, 67, 69, 71, 72});     // F natural
    CHECK(scale("C exp ^f ^c") == std::vector<int>{61, 62, 64, 66, 67, 69, 71, 73});
    CHECK(scale("G ^^g") == std::vector<int>{60, 62, 64, 66, 69, 69, 71, 72}); // G double sharp
}

TEST_CASE("abc: an accidental lasts to the end of the bar, for that pitch in that octave") {
    // ^F makes the second F sharp too; the F an octave up and the F in the next bar are not.
    const ScoreLoadResult r = load_music("^F F f F | F =F F ^^F _B B |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{66, 66, 77, 66, 65, 65, 65, 67, 70, 70});
}

TEST_CASE("abc: an accidental belongs to the note a tie leads to in the next bar") {
    const ScoreLoadResult r = load_music("A3 ^F5- | F3 F5 |]");
    REQUIRE(r.ok);
    // The tied F stays F sharp across the barline and joins; the next F in that bar is plain again.
    CHECK(pitches(r.song) == std::vector<int>{69, 66, 65});
    expect_lengths(r.song, {1.5, 4.0, 2.5});
}

// ---- Lengths and rests

TEST_CASE("abc: note lengths") {
    const ScoreLoadResult r = load_music("C2 D/2 E3/2 F/ G// A3 B/4 c|", "M:4/4\nL:1/4\nK:C");
    REQUIRE(r.ok);
    expect_lengths(r.song, {2.0, 0.5, 1.5, 0.5, 0.25, 3.0, 0.25, 1.0});
}

TEST_CASE("abc: rests move time and make no note") {
    const ScoreLoadResult r = load_music("C z D2 z2 E x F/ z/ G |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 5);
    // The unit is an eighth: C, a rest, D2, a rest of two, E, x, F/, z/, G.
    const std::vector<double> want_start = {0.0, 1.0, 3.0, 4.0, 4.5};
    for (std::size_t i = 0; i < want_start.size(); ++i) {
        CAPTURE(i);
        CHECK(start_of(r.song, r.song.notes[i]) == doctest::Approx(want_start[i]));
    }
}

TEST_CASE("abc: Z is a whole bar of rest, Z3 is three") {
    const ScoreLoadResult r = load_music("C4 z4 | Z | Z2 | G8 |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.measures.size() == 5); // bar 1, bar 2, bars 3 and 4, bar 5
    CHECK(start_of(r.song, r.song.notes[1]) == doctest::Approx(16.0));
}

TEST_CASE("abc: broken rhythm shifts length from one note to the next") {
    const ScoreLoadResult r = load_music("A>B c<d e>>f g<<a |]");
    REQUIRE(r.ok);
    // Eighth notes: 3:1, 1:3, 7:1 and 1:7 of two eighths.
    expect_lengths(r.song, {0.75, 0.25, 0.25, 0.75, 0.875, 0.125, 0.125, 0.875});
    CHECK(start_of(r.song, r.song.notes[1]) == doctest::Approx(0.75));
    CHECK(start_of(r.song, r.song.notes[3]) == doctest::Approx(1.25));
}

TEST_CASE("abc: broken rhythm works on chords and rests and with a unit change") {
    const ScoreLoadResult r = load_music("[CE]>G z<A |]", "M:4/4\nL:1/4\nK:C");
    REQUIRE(r.ok);
    expect_lengths(r.song, {1.5, 1.5, 0.5, 1.5});
}

TEST_CASE("abc: triplets and general tuplets") {
    const ScoreLoadResult r = load_music("(3CDE F2 (3:2:3 GAB c2 |]");
    REQUIRE(r.ok);
    // A triplet puts three eighths in the time of two.
    expect_lengths(r.song, {1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0, 1.0 / 3, 1.0 / 3, 1.0 / 3, 1.0});
    CHECK(start_of(r.song, r.song.notes[3]) == doctest::Approx(1.0));
    CHECK(start_of(r.song, r.song.notes[4]) == doctest::Approx(2.0));

    const ScoreLoadResult quint = load_music("(5CDEFG A2 |]");
    REQUIRE(quint.ok);
    // (5 is five in the time of two, in simple time.
    CHECK(length_of(quint.song, quint.song.notes[0]) == doctest::Approx(0.5 * 2.0 / 5.0));
    CHECK(start_of(quint.song, quint.song.notes[5]) == doctest::Approx(1.0));

    const ScoreLoadResult compound = load_music("(5CDEFG A2 |]", "M:6/8\nL:1/8\nK:C");
    REQUIRE(compound.ok);
    // In compound time (6/8) it is five in the time of three.
    CHECK(length_of(compound.song, compound.song.notes[0]) == doctest::Approx(0.5 * 3.0 / 5.0));

    const ScoreLoadResult duplet = load_music("(2CD E2 |]");
    REQUIRE(duplet.ok);
    CHECK(length_of(duplet.song, duplet.song.notes[0]) ==
          doctest::Approx(0.75)); // two in the time of three

    // (3:2:2 puts three eighths in the time of two over the next two notes only.
    const ScoreLoadResult partial = load_music("(3:2:2CD EF |]");
    REQUIRE(partial.ok);
    expect_lengths(partial.song, {1.0 / 3, 1.0 / 3, 0.5, 0.5});

    // A tuplet of chords and rests counts them as notes.
    const ScoreLoadResult mixed = load_music("(3[CE] z G c2 |]");
    REQUIRE(mixed.ok);
    CHECK(start_of(mixed.song, mixed.song.notes.back()) == doctest::Approx(1.0));
}

TEST_CASE("abc: a tuplet inside a bar keeps the bar full") {
    // Four bars of 4/4 written with triplets: every bar still lasts four beats.
    const ScoreLoadResult r = load_music("(3CDE (3FGA (3Bcd (3efg | C8 |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 2);
    CHECK(r.song.measures[1].start_tick == 4 * r.song.tempo.ticks_per_quarter);
    CHECK(r.song.notes.back().start_tick == r.song.measures[1].start_tick);
}

// ---- Chords, ties, and what is dropped

TEST_CASE("abc: chords play together, the first note's length times the outer one") {
    const ScoreLoadResult r = load_music("[CEG]2 [C2E2G2]3/2 [C-E-G-]2 [CEG] |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 9);
    CHECK(pitches(r.song) == std::vector<int>{60, 64, 67, 60, 64, 67, 60, 64, 67});
    // [CEG]2 lasts a quarter; [C2E2G2]3/2 is 1.5 times a quarter; the tie joins into the last
    // chord.
    CHECK(start_of(r.song, r.song.notes[3]) == doctest::Approx(1.0));
    CHECK(length_of(r.song, r.song.notes[3]) == doctest::Approx(1.5));
    CHECK(start_of(r.song, r.song.notes[6]) == doctest::Approx(2.5));
}

TEST_CASE("abc: chord notes with their own lengths end apart, the next note follows the first") {
    const ScoreLoadResult r = load_music("[C2E]G|]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 64, 67});
    CHECK(length_of(r.song, r.song.notes[0]) == doctest::Approx(1.0));
    CHECK(length_of(r.song, r.song.notes[1]) == doctest::Approx(0.5));
    CHECK(start_of(r.song, r.song.notes[2]) == doctest::Approx(1.0));
}

TEST_CASE("abc: ties join notes of the same pitch, across bars and in chords") {
    const ScoreLoadResult r = load_music("C2-C2 D2-D2 | E8- | E8 | [CG]8- | [CG]8 |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 64, 60, 67});
    expect_lengths(r.song, {2.0, 2.0, 8.0, 8.0, 8.0});
    CHECK(r.stats.ties_merged == 5);
    CHECK(r.stats.unmatched_ties == 0);
}

TEST_CASE("abc: a tie written after a space or before a bar line still ties the note before it") {
    const ScoreLoadResult r = load_music("C4 -C4 | D8 -| D8 | E4 - z4 | F8 |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 64, 65});
    expect_lengths(r.song, {4.0, 8.0, 2.0, 4.0});
}

TEST_CASE("abc: a tie to a different pitch joins nothing") {
    const ScoreLoadResult r = load_music("C2-D2 E-z F2 |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 64, 65});
    expect_lengths(r.song, {1.0, 1.0, 0.5, 1.0});
    CHECK(r.stats.ties_merged == 0);
}

TEST_CASE("abc: grace notes are dropped and counted, slurs and decorations are ignored") {
    const ScoreLoadResult r =
        load_music("{g}A {gab}B {/c}C (D E) .F ~G !trill!A +fermata+B H c T d u e |]");
    REQUIRE(r.ok);
    CHECK(r.stats.grace_notes_dropped == 5);
    CHECK(pitches(r.song) == std::vector<int>{69, 71, 60, 62, 64, 65, 67, 69, 71, 72, 74, 76});
    CHECK(r.warnings.empty());
    // A grace note takes no time.
    CHECK(start_of(r.song, r.song.notes[1]) == doctest::Approx(0.5));
}

TEST_CASE("abc: annotations, chord symbols, lyrics and other fields do not play") {
    const ScoreLoadResult r =
        load_music("\"Am\"A \"^up here\"B \"_below\"c y z\nw: la la la\nW: more words\n%% a "
                   "directive\n% a comment\n"
                   "A:somebody\nD:disc\nN:a "
                   "note\nR:reel\nS:somewhere\nZ:transcriber\nH:history\nB:book\nF:file\n"
                   "G:group\nO:origin\nP:A\nr:remark\nm:macro\nU:T=!trill!\nI:linebreak\ns:ccc\n+: "
                   "more\nc d |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{69, 71, 72, 72, 74});
    CHECK(r.warnings.empty());
}

TEST_CASE("abc: dynamics set the velocity from there on") {
    const ScoreLoadResult r = load_music("C !f!D E !pp!F !fff!G !p!A |]");
    REQUIRE(r.ok);
    std::vector<int> v;
    for (const Note& n : r.song.notes) {
        v.push_back(n.velocity);
    }
    CHECK(v == std::vector<int>{80, 96, 96, 33, 126, 49});
}
