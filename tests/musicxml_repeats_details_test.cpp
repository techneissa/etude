// SPDX-License-Identifier: GPL-3.0-or-later
// musicxml_repeats: details (split from musicxml_repeats_test.cpp).
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

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

} // namespace

// ---- Timeline details

TEST_CASE("repeats: the bar after a D.C. to a pickup keeps the pickup short") {
    // A one-beat pickup, then two full bars; D.C. goes back to the pickup.
    const std::string pickup =
        "<measure number=\"0\" implicit=\"yes\">" + std::string(kHead) +
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration></note>"
        "</measure>";
    const ScoreLoadResult r =
        load(score(pickup + bar(1, 'C') + bar(2, 'D', "", "", sound("dacapo=\"yes\""))));
    REQUIRE(r.ok);
    CHECK(played(r) == "GCDGCD");
    CHECK(numbers(r) == std::vector<int>{0, 1, 2, 3, 4, 5});
    CHECK(written(r) == std::vector<int>{0, 1, 2, 0, 1, 2});
    // 0.5 s pickup, then two 2 s bars, then the same again.
    CHECK(r.song.notes[3].start_s == doctest::Approx(4.5));
    CHECK(r.song.notes[4].start_s == doctest::Approx(5.0));
}

TEST_CASE("repeats: a tie into the bar after a repeat ends with its bar on the pass that jumps") {
    // |: A  B (last beat tied to the next bar) :| C (first beat tied)
    const std::string tied_start =
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration>"
        "<tie type=\"start\"/></note>";
    const std::string tied_stop =
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration>"
        "<tie type=\"stop\"/></note><note><rest/><duration>3</duration></note>";
    const std::string b_bar = "<measure number=\"2\"><note><rest/><duration>3</duration></note>" +
                              tied_start + std::string(kBackward) + "</measure>";
    const std::string c_bar = "<measure number=\"3\">" + tied_stop + "</measure>";
    const ScoreLoadResult r = load(score(bar(1, 'A', kForward) + b_bar + c_bar));
    REQUIRE(r.ok);
    // Notes: A, G (cut), A, G (full 2 beats into bar C).
    std::vector<const Note*> g;
    for (const Note& n : r.song.notes) {
        if (n.pitch == 67) {
            g.push_back(&n);
        }
    }
    REQUIRE(g.size() == 2);
    const std::int64_t beat = r.song.tempo.ticks_per_quarter;
    CHECK(g[0]->end_tick - g[0]->start_tick == beat);
    CHECK(g[1]->end_tick - g[1]->start_tick == 2 * beat);
    CHECK(r.stats.ties_not_continued == 1);
    CHECK(r.stats.ties_merged == 1);
}

TEST_CASE("repeats: a tie from the first ending into the second is struck again") {
    // |: C |1. E G~ :| 2. ~G B: the second ending's G is a fresh attack on pass two.
    const ScoreLoadResult r = load_musicxml(read_data("repeats-tie-into-ending.musicxml"));
    REQUIRE(r.ok);
    CHECK(played(r) == "CEGCGB");
    CHECK(written(r) == std::vector<int>{1, 2, 1, 3});
    const std::int64_t beat = r.song.tempo.ticks_per_quarter;
    std::vector<const Note*> g;
    for (const Note& n : r.song.notes) {
        if (n.pitch == 67) {
            g.push_back(&n);
        }
    }
    REQUIRE(g.size() == 2);
    CHECK(g[0]->end_tick - g[0]->start_tick == 2 * beat);
    CHECK(g[1]->end_tick - g[1]->start_tick == 2 * beat);
    CHECK(g[1]->start_tick == r.song.measures[3].start_tick);
    CHECK(r.stats.ties_merged == 0);
    CHECK(r.stats.unmatched_ties == 1);
    CHECK(r.stats.ties_not_continued == 1);
}

TEST_CASE("repeats: a tie played straight through still joins, in every pass") {
    // |: A  B~ | ~C  :| D: the tie stays inside the repeat, so both passes hold it.
    const std::string tied_start =
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration>"
        "<tie type=\"start\"/></note>";
    const std::string tied_stop =
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>4</duration>"
        "<tie type=\"stop\"/></note>";
    const std::string bars = std::string("<measure number=\"1\">") + std::string(kHead) + kForward +
                             tied_start + "</measure><measure number=\"2\">" + tied_stop +
                             kBackward + "</measure>" + bar(3, 'D');
    const ScoreLoadResult r = load(score(bars));
    REQUIRE(r.ok);
    CHECK(played(r) == "GGD");
    CHECK(r.stats.ties_merged == 2);
    CHECK(r.stats.ties_not_continued == 0);
    const std::int64_t bar_ticks = 4 * r.song.tempo.ticks_per_quarter;
    CHECK(r.song.notes[0].end_tick - r.song.notes[0].start_tick == 2 * bar_ticks);
    CHECK(r.song.notes[1].end_tick - r.song.notes[1].start_tick == 2 * bar_ticks);
}

TEST_CASE("repeats: tempo marks inside a repeated bar are set again on each pass") {
    const ScoreLoadResult r =
        load(piece({{kForward, "", sound("tempo=\"60\"")}, {"", kBackward, ""}, {}}));
    REQUIRE(r.ok);
    // One tempo change per pass over bar A, at the start of each (bar 1 and bar 3).
    REQUIRE(r.song.tempo.changes.size() == 2);
    CHECK(r.song.tempo.changes[0].tick == 0);
    CHECK(r.song.tempo.changes[1].tick == r.song.measures[2].start_tick);
}

TEST_CASE("repeats: signs on any one part expand every part") {
    // Part P1 carries the repeat; part P2 has no barlines but plays the same bars.
    auto part = [](std::string_view id, std::string_view bars) {
        return "<part id=\"" + std::string(id) + "\">" + std::string(bars) + "</part>";
    };
    std::string p2;
    for (int i = 1; i <= 3; ++i) {
        p2 += bar(i, static_cast<char>('D' + i - 1));
    }
    const std::string xml =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Right</part-name></score-part>"
        "<score-part id=\"P2\"><part-name>Left</part-name></score-part></part-list>" +
        part("P1", bar(1, 'A', kForward) + bar(2, 'B', "", kBackward) + bar(3, 'C')) +
        part("P2", p2) + "</score-partwise>";
    const ScoreLoadResult r = load(xml);
    REQUIRE(r.ok);
    std::string right;
    std::string left;
    for (const Note& n : r.song.notes) {
        (n.hand == Hand::Right ? right : left) += letter_of(n.pitch);
    }
    CHECK(right == "ABABC");
    CHECK(left == "DEDEF");
}

TEST_CASE("repeats: timewise scores expand too") {
    const std::string xml =
        "<?xml version=\"1.0\"?><score-timewise version=\"3.1\"><part-list><score-part id=\"P1\">"
        "<part-name>Piano</part-name></score-part></part-list>"
        "<measure number=\"1\"><part id=\"P1\">" +
        std::string(kHead) + kForward + whole('A') +
        "</part></measure><measure number=\"2\"><part id=\"P1\">" + whole('B') + kBackward +
        "</part></measure><measure number=\"3\"><part id=\"P1\">" + whole('C') +
        "</part></measure></score-timewise>";
    const ScoreLoadResult r = load(xml);
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABC");
}

// ---- Compressed MusicXML

TEST_CASE("repeats: a compressed .mxl expands the same way") {
    static constexpr const char* kContainer =
        "<?xml version=\"1.0\"?><container><rootfiles><rootfile full-path=\"score.xml\"/>"
        "</rootfiles></container>";
    const std::string xml = piece({{kForward, "", ""},
                                   {},
                                   {ending_start("1"), ending_stop("1", "stop", kRepeatBack), ""},
                                   {ending_start("2"), ending_stop("2", "discontinue"), ""},
                                   {}});
    const Bytes zip = test::zip_of({test::stored_entry("META-INF/container.xml", kContainer),
                                    test::stored_entry("score.xml", xml)});
    const ScoreLoadResult r = load_mxl(zip);
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
    const ScoreLoadResult sniffed = load_score(zip);
    REQUIRE(sniffed.ok);
    CHECK(played(sniffed) == "ABCABDE");
}

// ---- MuseScore

namespace {

std::string ms_chord(char letter) {
    int pitch = 60;
    switch (letter) {
    case 'D':
        pitch = 62;
        break;
    case 'E':
        pitch = 64;
        break;
    case 'F':
        pitch = 65;
        break;
    case 'G':
        pitch = 67;
        break;
    case 'A':
        pitch = 69;
        break;
    case 'B':
        pitch = 71;
        break;
    default:
        break;
    }
    return "<Chord><durationType>whole</durationType><Note><pitch>" + std::to_string(pitch) +
           "</pitch><tpc>14</tpc></Note></Chord>";
}

// A bar of one whole note. `outside` goes before <voice> (startRepeat, endRepeat), `inside` at
// the start of the voice (spanners, markers, jumps).
std::string ms_bar(char letter, std::string_view outside = "", std::string_view inside = "",
                   bool first = false) {
    return "<Measure>" + std::string(outside) + "<voice>" +
           (first ? "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>" : "") + std::string(inside) +
           ms_chord(letter) + "</voice></Measure>";
}

std::string ms_score(std::string_view staff_body) {
    return "<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division>"
           "<Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano"
           "</trackName><Instrument><Channel><program value=\"0\"/></Channel></Instrument></Part>"
           "<Staff id=\"1\">" +
           std::string(staff_body) + "</Staff></Score></museScore>";
}

ScoreLoadResult load_ms(const std::string& xml) {
    const Bytes b = bytes_of(xml);
    return load_mscx(b.data(), b.size());
}

std::string volta(std::string_view endings, int bars = 1) {
    return "<Spanner type=\"Volta\"><Volta><endings>" + std::string(endings) +
           "</endings></Volta><next><location><measures>" + std::to_string(bars) +
           "</measures></location></next></Spanner>";
}

std::string marker(std::string_view label) {
    return "<Marker><label>" + std::string(label) + "</label></Marker>";
}

std::string jump(std::string_view jump_to, std::string_view play_until,
                 std::string_view continue_at = "") {
    return "<Jump><jumpTo>" + std::string(jump_to) + "</jumpTo><playUntil>" +
           std::string(play_until) + "</playUntil><continueAt>" + std::string(continue_at) +
           "</continueAt></Jump>";
}

} // namespace

TEST_CASE("mscx repeats: start and end repeat measures") {
    // |: A B :| C
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) +
                         ms_bar('B', "<endRepeat>2</endRepeat>") + ms_bar('C')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABC");
    CHECK(written(r) == std::vector<int>{1, 2, 1, 2, 3});
    CHECK(r.stats.repeats_followed == 1);
}

TEST_CASE("mscx repeats: the end repeat count is the number of plays") {
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) +
                         ms_bar('B', "<endRepeat>3</endRepeat>") + ms_bar('C')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABABC");
}

TEST_CASE("mscx repeats: first and second endings") {
    // |: A B |1. C :| 2. D  E
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B') +
                         ms_bar('C', "<endRepeat>2</endRepeat>", volta("1")) +
                         ms_bar('D', "", volta("2")) + ms_bar('E')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5});
}

TEST_CASE("mscx repeats: one ending for two passes, and a third") {
    // |: A |1, 2. B :|x3  3. C
    const ScoreLoadResult r = load_ms(ms_score(
        ms_bar('A', "<startRepeat/>", "", true) +
        ms_bar('B', "<endRepeat>3</endRepeat>", volta("1, 2")) + ms_bar('C', "", volta("3"))));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABAC");
}

TEST_CASE("mscx repeats: a volta over two bars") {
    // |: A |1. B C :| 2. D E  F
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "", volta("1", 2)) +
                         ms_bar('C', "<endRepeat>2</endRepeat>") + ms_bar('D', "", volta("2", 2)) +
                         ms_bar('E') + ms_bar('F')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCADEF");
}

TEST_CASE("mscx repeats: D.S. al Coda from a jump and its markers") {
    // A(segno) B(To Coda) C(D.S. al Coda) D(coda) E
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "", marker("segno"), true) + ms_bar('B', "", marker("coda")) +
                         ms_bar('C', "", jump("segno", "coda", "codab")) +
                         ms_bar('D', "", marker("codab")) + ms_bar('E')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCABDE");
}

TEST_CASE("mscx repeats: D.C. al Fine from a jump and a Fine marker") {
    // A B(Fine) C(D.C. al Fine)
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "", "", true) + ms_bar('B', "", marker("fine")) +
                         ms_bar('C', "", jump("start", "fine"))));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCAB");
}

TEST_CASE("mscx repeats: malformed signs fall back to straight through") {
    // A volta with no repeat anywhere, and a jump to a missing segno.
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "", "", true) + ms_bar('B', "", volta("1")) +
                         ms_bar('C', "", volta("2")) + ms_bar('D', "", jump("segno", "end"))));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCD");
    CHECK(r.stats.repeats_ignored == 2);
}

TEST_CASE("mscx repeats: a first ending with no end repeat goes back to the start repeat") {
    // |: A 1.B 2.C :|  D
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "", volta("1")) +
                         ms_bar('C', "<endRepeat>2</endRepeat>", volta("2")) + ms_bar('D')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABACD");
    CHECK(r.stats.repeats_implied == 1);
    CHECK(r.stats.repeats_ignored == 0);
}

TEST_CASE("mscx repeats: nested repeats around endings") {
    // |: A |: B 1.C :| 2.D :| E
    const ScoreLoadResult r =
        load_ms(ms_score(ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "<startRepeat/>") +
                         ms_bar('C', "<endRepeat>2</endRepeat>", volta("1")) +
                         ms_bar('D', "<endRepeat>2</endRepeat>", volta("2")) + ms_bar('E')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABCBDABCBDE");
}

TEST_CASE("mscx repeats: a tie from the first ending into the second is struck again") {
    // |: A  1.B~ :| 2.~B C, with the tie across the two endings.
    const std::string start = "<tieFor/>";
    const std::string stop = "<tieBack/>";
    const ScoreLoadResult r = load_ms(ms_score(
        ms_bar('A', "<startRepeat/>", "", true) + "<Measure><endRepeat>2</endRepeat><voice>" +
        volta("1") + "<Chord><durationType>whole</durationType><Note>" + start +
        "<pitch>71</pitch><tpc>14</tpc></Note></Chord></voice></Measure>" + "<Measure><voice>" +
        volta("2") + "<Chord><durationType>whole</durationType><Note>" + stop +
        "<pitch>71</pitch><tpc>14</tpc></Note></Chord></voice></Measure>" + ms_bar('C')));
    REQUIRE(r.ok);
    CHECK(played(r) == "ABABC");
    CHECK(r.stats.ties_merged == 0);
    CHECK(r.stats.ties_not_continued == 1);
}

// ---- A real file with voltas: MuseScore's own export is the reference

namespace {

struct Hit {
    std::int64_t start;
    int pitch;
    bool operator==(const Hit& o) const { return start == o.start && pitch == o.pitch; }
};

std::vector<Hit> hits_of(const Song& song) {
    std::vector<Hit> out;
    for (const Note& n : song.notes) {
        out.push_back({n.start_tick, n.pitch});
    }
    std::sort(out.begin(), out.end(), [](const Hit& a, const Hit& b) {
        return std::tie(a.start, a.pitch) < std::tie(b.start, b.pitch);
    });
    return out;
}

void check_voltas_file(const ScoreLoadResult& r, const std::vector<Hit>& reference) {
    REQUIRE(r.ok);
    // 7 written bars, played 1 2 3 | 1 2 4 | 5 6 | 5 6 | 5 7.
    CHECK(r.stats.bars_written == 7);
    CHECK(r.stats.bars_played == 12);
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5, 6, 5, 6, 5, 7});
    CHECK(numbers(r) == std::vector<int>{1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12});
    CHECK(r.stats.repeats_ignored == 0);
    CHECK_FALSE(r.stats.repeats_capped);
    CHECK(hits_of(r.song) == reference);
}

} // namespace

TEST_CASE("repeats: the voltas piece plays like MuseScore's own export, in every format") {
    const Bytes midi_bytes = read_data("repeats-voltas-musescore-export.mid");
    const MidiLoadResult midi = load_midi(midi_bytes);
    REQUIRE(midi.ok);
    const std::vector<Hit> reference = hits_of(midi.song);
    REQUIRE(reference.size() == 44);
    CHECK(midi.song.tempo.ticks_per_quarter == 480);

    const Bytes xml = read_data("repeats-voltas.musicxml");
    const std::string text(xml.begin(), xml.end());
    CHECK(text.find("<rights>CC0 1.0") != std::string::npos);
    check_voltas_file(load_musicxml(xml), reference);
    check_voltas_file(load_mxl(read_data("repeats-voltas.mxl")), reference);
    check_voltas_file(load_mscz(read_data("repeats-voltas-musescore4.mscz")), reference);
    check_voltas_file(load_score(read_data("repeats-voltas.mxl")), reference);
}
