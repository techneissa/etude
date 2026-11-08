// SPDX-License-Identifier: GPL-3.0-or-later
// abc: repeats (split from abc_test.cpp).
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

// ---- Bars, pickups and repeats

TEST_CASE("abc: bars and the pickup") {
    const ScoreLoadResult r = load_music("G | c4 B2 A2 | G6 z2 |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 3);
    CHECK(r.song.measures[0].number == 0); // the one-eighth first bar is a pickup
    CHECK(r.song.measures[1].number == 1);
    CHECK(r.song.measures[2].number == 2);
    CHECK(r.song.notes[0].measure == 0);
    CHECK(r.song.notes[1].measure == 1);
    CHECK(r.song.measures[1].start_tick == r.song.tempo.ticks_per_quarter / 2);
}

TEST_CASE("abc: a first bar that fills the meter is bar 1, and a last short bar is padded") {
    const ScoreLoadResult r = load_music("C8 | D4 |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 2);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.end_tick > 0);
}

TEST_CASE("abc: a last bar that completes a pickup is not padded") {
    const ScoreLoadResult r = load_music("G2 | c8 | G6 |]");
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 3);
    CHECK(r.song.measures[0].number == 0);
    // Pickup (one beat) + the last bar (three beats) make one full bar; the song ends with the
    // notes.
    const Note& last = r.song.notes.back();
    CHECK(last.end_tick == r.song.end_tick);
    CHECK(start_of(r.song, last) + length_of(r.song, last) == doctest::Approx(1.0 + 4.0 + 3.0));
}

TEST_CASE("abc: bar line forms") {
    for (const char* bar : {"|", "||", "|]", "[|", "[|]", ".|"}) {
        CAPTURE(bar);
        const ScoreLoadResult r = load_music(std::string("C8 ") + bar + " D8 |]");
        REQUIRE(r.ok);
        CHECK(r.song.measures.size() == 2);
        CHECK(r.stats.bars_written == 2);
    }
    // Bar lines with nothing between them make no bar.
    const ScoreLoadResult empty = load_music("C8 | | || D8 |]");
    REQUIRE(empty.ok);
    CHECK(empty.song.measures.size() == 2);
    // No bar line at the end of the tune and none in the middle of a long line.
    const ScoreLoadResult open = load_music("C8 D8");
    REQUIRE(open.ok);
    CHECK(open.song.measures.size() == 1);
}

TEST_CASE("abc: a repeat plays twice, with bar numbers in playing order") {
    const ScoreLoadResult r = load_music("|: C8 | D8 :| E8 |]");
    REQUIRE(r.ok);
    CHECK(r.stats.bars_written == 3);
    CHECK(r.stats.bars_played == 5);
    CHECK(r.stats.repeats_followed == 1);
    CHECK(played_bars(r.song) == std::vector<int>{1, 2, 1, 2, 3});
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 60, 62, 64});
    for (std::size_t i = 0; i < r.song.measures.size(); ++i) {
        CHECK(r.song.measures[i].number == static_cast<int>(i) + 1);
    }
    // The note's own bar number is the played one, its written one is the page's.
    CHECK(r.song.notes[2].measure == 3);
    CHECK(r.song.notes[2].written_measure() == 1);
}

TEST_CASE("abc: a repeat sign alone goes back to the start, and :|: is both") {
    const ScoreLoadResult a = load_music("C8 | D8 :| E8 |]");
    REQUIRE(a.ok);
    CHECK(played_bars(a.song) == std::vector<int>{1, 2, 1, 2, 3});
    const ScoreLoadResult b = load_music("|: C8 :|: D8 :| E8 |]");
    REQUIRE(b.ok);
    CHECK(played_bars(b.song) == std::vector<int>{1, 1, 2, 2, 3});
    const ScoreLoadResult c = load_music("|: C8 :: D8 :| E8 |]");
    REQUIRE(c.ok);
    CHECK(played_bars(c.song) == std::vector<int>{1, 1, 2, 2, 3});
}

TEST_CASE("abc: first and second endings in every written form") {
    const std::vector<int> want = {1, 2, 3, 1, 2, 4, 5};
    for (const char* music : {"|: C8 | D8 |1 E8 :|2 F8 | G8 |]",   // |1 and :|2
                              "|: C8 | D8 [1 E8 :| [2 F8 | G8 |]", // [1 and [2
                              "|: C8 | D8 |[1 E8 :|[2 F8 | G8 |]", // |[1
                              "|: C8 | D8 | [1 E8 :| [2 F8 | G8 |]",
                              "C8 | D8 |1 E8 :|2 F8 | G8 |]"}) { // no forward repeat sign
        CAPTURE(music);
        const ScoreLoadResult r = load_music(music);
        REQUIRE(r.ok);
        CHECK(played_bars(r.song) == want);
    }
}

TEST_CASE("abc: :|1 and :|[1 open a first ending like |1 does, they do not repeat the bar before") {
    const std::string quarters = "M:4/4\nL:1/4\nK:C";
    const std::vector<int> want = {1, 2, 1, 3};
    for (const char* music :
         {"|: C D E F |1 G A B c :|2 d e f g |]", "|: C D E F :|1 G A B c :|2 d e f g |]",
          "|: C D E F :|[1 G A B c :|[2 d e f g |]"}) {
        CAPTURE(music);
        const ScoreLoadResult r = load_music(music, quarters);
        REQUIRE(r.ok);
        CHECK(played_bars(r.song) == want);
    }
    // The longer form, against the |1 order: the first ending is two bars.
    const std::vector<int> reference =
        played_bars(load_music("|: CDEF | GABc |1 defg | abcd :|2 efga |]", quarters).song);
    CHECK(reference == std::vector<int>{1, 2, 3, 4, 1, 2, 5});
    const ScoreLoadResult longer =
        load_music("|: CDEF | GABc :|1 defg | abcd :|2 efga |]", quarters);
    REQUIRE(longer.ok);
    CHECK(played_bars(longer.song) == reference);
    CHECK(longer.stats.repeats_followed == 1);
    // A ":|2" after a plain repeat with no first ending still repeats.
    CHECK(played_bars(load_music("|: C4 :|2 D4 |]", quarters).song) == std::vector<int>{1, 1, 2});
    // A real repeat sign before the numbered ending stays one when it is a double sign.
    CHECK(played_bars(load_music("|: C4 :|: D4 :|1 E4 :|2 F4 |]", quarters).song) ==
          std::vector<int>{1, 1, 2, 3, 2, 4});
}

TEST_CASE("abc: endings with several numbers and several bars") {
    // [1,2 is played on both passes of a three-time repeat, [3 only on the last.
    const ScoreLoadResult r = load_music("|: C8 |1,2 D8 :|3 E8 |]");
    REQUIRE(r.ok);
    CHECK(played_bars(r.song) == std::vector<int>{1, 2, 1, 2, 1, 3});
    // An ending of two bars.
    const ScoreLoadResult two = load_music("|: C8 |1 D8 | E8 :|2 F8 | G8 |]");
    REQUIRE(two.ok);
    CHECK(played_bars(two.song) == std::vector<int>{1, 2, 3, 1, 4, 5});
    const ScoreLoadResult range = load_music("|: C8 |1-2 D8 :|3 E8 |]");
    REQUIRE(range.ok);
    CHECK(range.song.measures.size() >= 4);
}

TEST_CASE("abc: a repeat sign with nothing to repeat is ignored, never a hang") {
    const ScoreLoadResult r = load_music("C8 | :| D8 |: E8 |]");
    REQUIRE(r.ok);
    CHECK(r.song.measures.size() >= 3);
    const ScoreLoadResult stray = load_music("|: |: |: C8 :| :| :| D8 |]");
    REQUIRE(stray.ok);
}

TEST_CASE("abc: a tie into the second ending is struck again on the pass that reaches it") {
    const ScoreLoadResult r = load_music("|: C8 | D4 E4- |1 E4 F4 :|2 E4 G4 |]");
    REQUIRE(r.ok);
    // The tied E joins the E of the first ending, which plays right after it. The second ending
    // does not follow the tied bar on the second pass, so its E is a note of its own.
    std::vector<double> e_lengths;
    for (const Note& n : r.song.notes) {
        if (n.pitch == 64) {
            e_lengths.push_back(length_of(r.song, n));
        }
    }
    REQUIRE(e_lengths.size() == 3);
    CHECK(e_lengths[0] == doctest::Approx(4.0)); // pass one: the tied E and the first ending's E
    CHECK(e_lengths[1] == doctest::Approx(2.0)); // pass two: the tied E ...
    CHECK(e_lengths[2] == doctest::Approx(2.0)); // ... and the second ending's E, struck again
    CHECK(r.stats.ties_not_continued == 1);
}

TEST_CASE("abc: D.C., D.S., segno, coda and fine decorations") {
    const ScoreLoadResult dc = load_music("C8 | D8 | E8 !D.C.! | F8 |]");
    REQUIRE(dc.ok);
    CHECK(played_bars(dc.song) == std::vector<int>{1, 2, 3, 1, 2, 3, 4});
    const ScoreLoadResult fine = load_music("C8 | D8 !fine! | E8 !D.C.! |]");
    REQUIRE(fine.ok);
    CHECK(played_bars(fine.song) == std::vector<int>{1, 2, 3, 1, 2});
    const ScoreLoadResult ds = load_music("C8 | !segno! D8 | E8 !D.S.! | F8 |]");
    REQUIRE(ds.ok);
    CHECK(played_bars(ds.song) == std::vector<int>{1, 2, 3, 2, 3, 4});
}

TEST_CASE("abc: an expansion that passes the cap plays straight through") {
    std::string music = "|: C8 :|";
    // 100 nested-looking repeat signs: more than the planner allows must not blow up.
    for (int i = 0; i < 40; ++i) {
        music += ": D8 :|";
    }
    const ScoreLoadResult r = load_music(music);
    REQUIRE(r.ok);
    CHECK(r.song.measures.size() <= 100000);
}

// ---- Inline fields and mid-tune changes

TEST_CASE("abc: inline fields change the key, meter, length and tempo from there") {
    const ScoreLoadResult r =
        load_music("C2 D2 [K:G] F2 G2 | [M:3/4] [L:1/4] [Q:1/4=60] F G A | c3 |]");
    REQUIRE(r.ok);
    // F is natural before [K:G] only; there is none before. After it F is sharp.
    CHECK(pitches(r.song) == std::vector<int>{60, 62, 66, 67, 66, 67, 69, 72});
    REQUIRE(r.song.time_signatures.size() == 2);
    CHECK(r.song.time_signatures[1].numerator == 3);
    CHECK(r.song.time_signatures[1].tick == r.song.measures[1].start_tick);
    REQUIRE(r.song.tempo.changes.size() == 2);
    CHECK(r.song.tempo.changes[1].us_per_quarter == 1000000);
    // After [L:1/4] a note is a quarter.
    CHECK(length_of(r.song, r.song.notes[4]) == doctest::Approx(1.0));
}

TEST_CASE("abc: a field line in the body changes the key too") {
    const ScoreLoadResult r = load_music("C8 |\nK:D\nF8 |]");
    REQUIRE(r.ok);
    CHECK(pitches(r.song) == std::vector<int>{60, 66});
}
