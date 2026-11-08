// SPDX-License-Identifier: GPL-3.0-or-later
// Repeat barlines, endings (voltas), D.C., D.S., segno, coda and Fine in MusicXML, compressed
// MusicXML and MuseScore files: the played order, the bar numbers of the expanded timeline, and
// what a malformed file falls back to. Every score here has one whole note per bar, named by its
// pitch (C D E F G A B), so a play order reads as a word.
#include "etude/musicxml.hpp"
#include "etude/midi_file.hpp"
#include "etude/repeat_order.hpp"
#include "etude/sections.hpp"
#include "etude/transport.hpp"
#include "zip_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

#include "musicxml_repeats_test_support.hpp"

using namespace etude;
using namespace musicxml_repeats_test_support;

namespace {

std::string backward_times(int times) {
    return "<barline location=\"right\"><repeat direction=\"backward\" times=\"" +
           std::to_string(times) + "\"/></barline>";
}

// A direction with words or a glyph only, no <sound>.
std::string sign_only(std::string_view words, std::string_view glyph = "") {
    std::string out = "<direction placement=\"above\">";
    if (!words.empty()) {
        out += "<direction-type><words>" + std::string(words) + "</words></direction-type>";
    }
    if (!glyph.empty()) {
        out += "<direction-type><" + std::string(glyph) + "/></direction-type>";
    }
    return out + "</direction>";
}

} // namespace

// ---- Repeat barlines

TEST_CASE("repeats: a forward and a backward barline play the passage twice") {
    // |: A B :| C
    const ScoreLoadResult r = load(piece({{kForward, "", ""}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABC");
    CHECK(r.stats.bars_written == 3);
    CHECK(r.stats.bars_played == 5);
    CHECK(r.stats.repeats_followed == 1);
    CHECK(r.stats.repeats_ignored == 0);
    CHECK_FALSE(r.stats.repeats_capped);
}

TEST_CASE("repeats: the expanded timeline numbers its bars in playing order") {
    const ScoreLoadResult r = load(piece({{kForward, "", ""}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    CHECK(numbers(r) == std::vector<int>{1, 2, 3, 4, 5});
    CHECK(written(r) == std::vector<int>{1, 2, 1, 2, 3});
    // 4/4 at 120 BPM is 2 s a bar, and the second pass starts where the first ended.
    REQUIRE(r.song.notes.size() == 5);
    for (std::size_t i = 0; i < 5; ++i) {
        CHECK(r.song.notes[i].start_s == doctest::Approx(2.0 * static_cast<double>(i)));
        CHECK(r.song.notes[i].measure == static_cast<int>(i) + 1);
    }
    CHECK(r.song.notes[2].written_measure() == 1);
    CHECK(r.song.notes[3].written_measure() == 2);
    CHECK(r.song.notes[4].written_measure() == 3);
    CHECK(r.song.measures[2].start_s == doctest::Approx(4.0));
    CHECK(r.song.duration_s == doctest::Approx(10.0));
    // The bar at a tick, by played number and by written number.
    const std::int64_t third = r.song.measures[2].start_tick + 10;
    CHECK(r.song.measure_at(third) == 3);
    CHECK(r.song.written_measure_at(third) == 1);
}

TEST_CASE("repeats: a score with no repeats keeps written numbers equal to played ones") {
    const ScoreLoadResult r = load(piece({{}, {}, {}}));
    REQUIRE(r.ok);
    CHECK(numbers(r) == written(r));
    CHECK(r.song.measures[0].source_number == -1);
    CHECK(r.song.notes[0].source_measure == -1);
    CHECK(r.song.notes[1].written_measure() == r.song.notes[1].measure);
    CHECK(r.stats.bars_written == r.stats.bars_played);
}

TEST_CASE("repeats: times is the number of plays") {
    // |: A B :|x3 C
    const ScoreLoadResult r = load(piece({{kForward, "", ""}, {"", backward_times(3), ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABABC");
    CHECK(r.stats.repeats_followed == 2);
}

TEST_CASE("repeats: a backward barline with no forward goes back to the start") {
    const ScoreLoadResult r = load(piece({{}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABC");
    CHECK(r.stats.repeats_ignored == 0);
}

TEST_CASE("repeats: back-to-back repeats") {
    // |: A :| |: B :| C
    const ScoreLoadResult r =
        load(piece({{kForward, kBackward, ""}, {kForward, kBackward, ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "AABBC");
}

TEST_CASE("repeats: nested repeats") {
    // |: A |: B C :| D :| E
    const ScoreLoadResult r = load(piece(
        {{kForward, "", ""}, {kForward, "", ""}, {"", kBackward, ""}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCBCDABCBCDE");
    CHECK(r.song.measures.size() == 13);
}

TEST_CASE("repeats: a forward barline written on the right edge belongs to the next bar") {
    // A [right: forward] B [left: backward] -> the passage is bar B only.
    const std::string right_forward =
        "<barline location=\"right\"><repeat direction=\"forward\"/></barline>";
    const std::string left_backward =
        "<barline location=\"left\"><repeat direction=\"backward\"/></barline>";
    const ScoreLoadResult r =
        load(piece({{"", right_forward, ""}, {}, {left_backward, "", ""}, {}}));
    REQUIRE(r.ok);
    // The forward sign lands on B; the backward one on B as well (the left edge of C is B's end).
    CHECK(played(r) == "ABBCD");
}

// ---- Endings

TEST_CASE("repeats: first and second endings") {
    // |: A B |1. C :| 2. D  E
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {},
                    {ending_start("1"), ending_stop("1", "stop", kRepeatBack), ""},
                    {ending_start("2"), ending_stop("2", "discontinue"), ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5});
    CHECK(numbers(r) == std::vector<int>{1, 2, 3, 4, 5, 6, 7});
}

TEST_CASE("repeats: an ending that spans two bars") {
    // |: A |1. B C :| 2. D E  F
    const std::string stop_repeat = ending_stop("1", "stop", kRepeatBack);
    const ScoreLoadResult r = load(piece({{kForward, "", ""},
                                          {ending_start("1"), "", ""},
                                          {"", stop_repeat, ""},
                                          {ending_start("2"), "", ""},
                                          {"", ending_stop("2", "discontinue"), ""},
                                          {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCADEF");
}

TEST_CASE("repeats: one ending for two passes, then a third") {
    // |: A |1, 2. B :|x3  3. C  D
    const ScoreLoadResult r = load(
        piece({{kForward, "", ""},
               {ending_start("1, 2"),
                ending_stop("1, 2", "stop", "<repeat direction=\"backward\" times=\"3\"/>"), ""},
               {ending_start("3"), ending_stop("3", "discontinue"), ""},
               {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABACD");
}

TEST_CASE("repeats: three endings, each closed by its own repeat sign") {
    // |: A |1. B :| 2. C :| 3. D  E
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {ending_start("1"), ending_stop("1", "stop", kRepeatBack), ""},
                    {ending_start("2"), ending_stop("2", "stop", kRepeatBack), ""},
                    {ending_start("3"), ending_stop("3", "discontinue"), ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABACADE");
}

TEST_CASE("repeats: a volta inside a repeated passage starts again on each outer pass") {
    // |: A |: B |1. C :| 2. D  E :| F
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {kForward, "", ""},
                    {ending_start("1"), ending_stop("1", "stop", kRepeatBack), ""},
                    {ending_start("2"), ending_stop("2", "discontinue"), ""},
                    {"", kBackward, ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCBDEABCBDEF");
}

TEST_CASE("repeats: nested repeats with endings, the outer one closing after the last ending") {
    // |: A |: B 1.C :| 2.D :| E
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {kForward, "", ""},
                    {ending_start("1"), ending_stop("1", "stop", kRepeatBack), ""},
                    {ending_start("2"), ending_stop("2", "stop", kRepeatBack), ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCBDABCBDE");
    CHECK(written(r) == std::vector<int>{1, 2, 3, 2, 4, 1, 2, 3, 2, 4, 5});
    CHECK(r.stats.repeats_ignored == 0);
    CHECK(r.stats.repeats_implied == 0);
}

TEST_CASE("repeats: three levels of nested repeats") {
    // |: A |: B |: C D :| E :| F :| G
    const ScoreLoadResult r = load(piece({{kForward, "", ""},
                                          {kForward, "", ""},
                                          {kForward, "", ""},
                                          {"", kBackward, ""},
                                          {"", kBackward, ""},
                                          {"", kBackward, ""},
                                          {}}));
    REQUIRE(r.ok);
    const std::string middle = "BCDCDE";
    const std::string outer = "A" + middle + middle + "F";
    CHECK(played(r) == outer + outer + "G");
}

// ---- D.C., D.S., segno, coda, Fine

TEST_CASE("repeats: D.C. goes back to the start") {
    const ScoreLoadResult r = load(piece({{}, {}, {"", "", sound("dacapo=\"yes\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABC");
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 3});
}

TEST_CASE("repeats: D.C. al Fine plays to the Fine") {
    // A B(Fine) C D(D.C.)
    const ScoreLoadResult r =
        load(piece({{}, {"", "", sound("fine=\"yes\"")}, {}, {"", "", sound("dacapo=\"yes\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCDAB");
}

TEST_CASE("repeats: D.S. al Coda") {
    // A(segno) B(To Coda) C(D.S. al Coda) D(coda) E
    const ScoreLoadResult r = load(piece({{"", "", sound("segno=\"segno\"", "", "segno")},
                                          {"", "", sound("tocoda=\"coda\"", "To Coda")},
                                          {"", "", sound("dalsegno=\"segno\"", "D.S. al Coda")},
                                          {"", "", sound("coda=\"coda\"", "", "coda")},
                                          {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5});
    CHECK(r.stats.repeats_followed == 2);
}

TEST_CASE("repeats: D.S. al Fine") {
    // A B(segno) C(Fine) D(D.S. al Fine)
    const ScoreLoadResult r = load(piece({{},
                                          {"", "", sound("segno=\"s\"")},
                                          {"", "", sound("fine=\"yes\"")},
                                          {"", "", sound("dalsegno=\"s\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCDBC");
}

TEST_CASE("repeats: a repeat inside a D.C. is not taken on the way back") {
    // |: A B :| C(D.C.)
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""}, {"", kBackward, ""}, {"", "", sound("dacapo=\"yes\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABCABC");
}

TEST_CASE("repeats: signs written as words and glyphs without a <sound>") {
    // A(segno) B(To Coda) C(D.S. al Coda) D(coda) E
    const ScoreLoadResult r = load(piece({{"", "", sign_only("", "segno")},
                                          {"", "", sign_only("To Coda", "coda")},
                                          {"", "", sign_only("D.S. al Coda")},
                                          {"", "", sign_only("", "coda")},
                                          {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
    // D.C. al Fine, and the Fine word.
    const ScoreLoadResult dc =
        load(piece({{}, {"", "", sign_only("Fine")}, {}, {"", "", sign_only("D.C. al Fine")}}));
    REQUIRE(dc.ok);
    CHECK(played(dc) == "ABCDAB");
}

TEST_CASE("repeats: words that only look like jumps do nothing") {
    const ScoreLoadResult r = load(piece({{"", "", sign_only("dolce")},
                                          {"", "", sign_only("a fine example")},
                                          {"", "", sign_only("Dal ritmo")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABC");
}

TEST_CASE("repeats: Fine and To Coda before any jump are ignored") {
    const ScoreLoadResult r = load(piece({{},
                                          {"", "", sound("fine=\"yes\"")},
                                          {"", "", sound("tocoda=\"c\"")},
                                          {"", "", sound("coda=\"c\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCD");
}

// ---- What reads bar numbers after the expansion

TEST_CASE("repeats: sections, loops and snapping work on the played timeline") {
    // |: A B :| C: five played bars of 2 s.
    const ScoreLoadResult r = load(piece({{kForward, "", ""}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    const Song& song = r.song;

    // A cut in the second pass snaps to a played bar start and the section names use played
    // numbers, which never repeat.
    SongSections sections;
    sections.song_hash = 1;
    REQUIRE(sections.add_marker(song, 4.3).ok);
    REQUIRE(sections.sections.size() == 2);
    CHECK(sections.sections[0].first_measure == 1);
    CHECK(sections.sections[0].last_measure == 2);
    CHECK(sections.sections[1].first_measure == 3);
    CHECK(sections.sections[1].last_measure == 5);
    CHECK(sections.sections[1].name == "Bars 3-5");
    CHECK(sections.valid(song));

    // A loop over played bars 3 and 4 (the second pass of A B) is 4 s to 8 s.
    const LoopSpan loop = measures_span(song, 3, 4);
    REQUIRE(loop.ok);
    CHECK(loop.start_s == doctest::Approx(4.0));
    CHECK(loop.end_s == doctest::Approx(8.0));
    CHECK(snap_to_measure(song, 4.9, SnapMode::Nearest) == doctest::Approx(4.0));

    // The written bar of the bar the loop starts in is still reachable.
    CHECK(song.written_measure_at(song.notes[2].start_tick) == 1);
    CHECK(song.measure_at(song.notes[2].start_tick) == 3);
}

// ---- Malformed input

TEST_CASE("repeats: a forward barline that nothing closes is left out") {
    const ScoreLoadResult r = load(piece({{}, {kForward, "", ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABC");
    CHECK(r.stats.repeats_ignored == 1);
}

TEST_CASE("repeats: endings with no repeat play straight through") {
    // A 1. B  2. C
    const ScoreLoadResult r =
        load(piece({{},
                    {ending_start("1"), ending_stop("1"), ""},
                    {ending_start("2"), ending_stop("2", "discontinue"), ""}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABC");
    CHECK(r.stats.repeats_ignored == 1);
    CHECK(r.stats.repeats_implied == 0);
}

TEST_CASE("repeats: a first ending with no backward repeat goes back to the forward repeat") {
    // |: A 1.B 2.C :|  D: the repeat sign sits after the last ending.
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {ending_start("1"), ending_stop("1"), ""},
                    {ending_start("2"), ending_stop("2", "stop", kRepeatBack), ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABACD");
    CHECK(written(r) == std::vector<int>{1, 2, 1, 3, 4});
    CHECK(r.stats.repeats_implied == 1);
    CHECK(r.stats.repeats_ignored == 0);
}

TEST_CASE("repeats: a first ending with no repeat sign at all, after an open forward repeat") {
    // |: A 1.B 2.C  D
    const ScoreLoadResult r = load(piece({{kForward, "", ""},
                                          {ending_start("1"), ending_stop("1"), ""},
                                          {ending_start("2"), ending_stop("2", "discontinue"), ""},
                                          {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABACD");
    CHECK(r.stats.repeats_implied == 1);
    CHECK(r.stats.repeats_ignored == 0);
}

TEST_CASE("repeats: an ending that two bars share is implied the same way") {
    // |: A 1.B 2.C 3.D :|x3  E: the first two endings get implied repeats.
    const ScoreLoadResult r =
        load(piece({{kForward, "", ""},
                    {ending_start("1"), ending_stop("1"), ""},
                    {ending_start("2"), ending_stop("2"), ""},
                    {ending_start("3"),
                     ending_stop("3", "stop", "<repeat direction=\"backward\" times=\"3\"/>"), ""},
                    {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABACADE");
    CHECK(r.stats.repeats_implied == 2);
}

TEST_CASE("repeats: an ending stop with no start changes nothing") {
    const ScoreLoadResult r = load(piece({{}, {"", ending_stop("1"), ""}, {}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABC");
    CHECK(r.stats.repeats_ignored == 1);
}

TEST_CASE("repeats: a D.S. with no segno and a To Coda with no coda are dropped") {
    const ScoreLoadResult ds = load(piece({{}, {}, {"", "", sound("dalsegno=\"x\"")}}));
    REQUIRE(ds.ok);
    CHECK(played(ds) == "ABC");
    CHECK(ds.stats.repeats_ignored == 1);
    const ScoreLoadResult tc =
        load(piece({{}, {"", "", sound("tocoda=\"x\"")}, {"", "", sound("dacapo=\"yes\"")}}));
    REQUIRE(tc.ok);
    CHECK(played(tc) == "ABCABC");
    CHECK(tc.stats.repeats_ignored == 1);
}

TEST_CASE("repeats: jumps that point at each other end after one jump") {
    // A(segno) B(D.S.) C(D.S.) D(D.C.): only the first jump is taken.
    const ScoreLoadResult r = load(piece({{"", "", sound("segno=\"s\"")},
                                          {"", "", sound("dalsegno=\"s\"")},
                                          {"", "", sound("dalsegno=\"s\"")},
                                          {"", "", sound("dacapo=\"yes\"")}}));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABCD");
}

TEST_CASE("repeats: a huge times is clamped and a runaway expansion falls back to straight") {
    const ScoreLoadResult big =
        load(piece({{kForward, "", ""}, {"", backward_times(1000000), ""}, {}}));
    REQUIRE(big.ok);
    CHECK(big.song.notes.size() == 2 * static_cast<std::size_t>(kMaxRepeatTimes) + 1);
    CHECK_FALSE(big.stats.repeats_capped);

    // Three nested repeats of 16 plays each: 16 x 16 x 16 bars, far past 16 times the source.
    const std::string many = backward_times(16);
    const ScoreLoadResult r = load(piece({{kForward, "", ""},
                                          {kForward, "", ""},
                                          {kForward, "", ""},
                                          {},
                                          {"", many, ""},
                                          {"", many, ""},
                                          {"", many, ""}}));
    REQUIRE(r.ok);
    CHECK(r.stats.repeats_capped);
    CHECK(played(r) == "ABCDEFG");
    CHECK(r.stats.bars_played == 7);
    CHECK(r.stats.repeats_followed == 0);
}

TEST_CASE("repeats: playing the whole piece 16 times is exactly at the cap") {
    const std::string last = backward_times(kMaxRepeatTimes);
    std::string bars;
    for (int i = 1; i <= 8; ++i) {
        bars += bar(i, 'C', i == 1 ? kForward : "", i == 8 ? last : "");
    }
    const ScoreLoadResult r = load(score(bars));
    REQUIRE(r.ok);
    CHECK(r.stats.bars_played == 8 * static_cast<int>(kMaxRepeatExpansion));
    CHECK_FALSE(r.stats.repeats_capped);
}
