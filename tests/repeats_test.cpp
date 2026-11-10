// SPDX-License-Identifier: GPL-3.0-or-later
// Repeat barlines, endings (voltas), D.C., D.S., segno, coda and Fine: the planner that orders
// the bars, and what the MusicXML, MuseScore and ABC readers hand it. Scores here have one whole
// note per bar, named by its pitch (C D E F G A B), so a play order reads as a word.
#include "etude/abc.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/repeat_order.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <vector>

using namespace etude;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// The marks of written bars from a short spec: bars split by ';', signs by spaces. F forward
// repeat, B backward repeat (B3: three plays), e1,2 an ending that starts here, s an ending that
// stops here, dc D.C., ds D.S. (ds:x to the segno x), sg segno, co coda, tc To Coda, fi Fine.
std::vector<BarMarks> marks(std::string_view spec) {
    std::vector<BarMarks> bars(1);
    std::string sign;
    auto apply = [&] {
        BarMarks& m = bars.back();
        const std::string label =
            sign.find(':') == std::string::npos ? "" : sign.substr(sign.find(':') + 1);
        const std::string name = sign.substr(0, sign.find(':'));
        if (name == "F") {
            m.forward_repeat = true;
        } else if (name[0] == 'B') {
            m.backward_repeat = true;
            m.times = std::atoi(name.c_str() + 1);
        } else if (name[0] == 'e') {
            m.endings = parse_ending_numbers(name.c_str() + 1);
        } else if (name == "s") {
            m.ending_stops = true;
        } else if (name == "dc") {
            m.dacapo = true;
        } else if (name == "ds") {
            m.dalsegno = true;
            m.dalsegno_label = label;
        } else if (name == "sg") {
            m.segno = true;
            m.segno_label = label;
        } else if (name == "co") {
            m.coda = true;
            m.coda_label = label;
        } else if (name == "tc") {
            m.tocoda = true;
            m.tocoda_label = label;
        } else if (name == "fi") {
            m.fine = true;
        }
        sign.clear();
    };
    for (char c : spec) {
        if (c == ';' || c == ' ') {
            if (!sign.empty()) {
                apply();
            }
            if (c == ';') {
                bars.emplace_back();
            }
        } else {
            sign += c;
        }
    }
    if (!sign.empty()) {
        apply();
    }
    return bars;
}

std::string word(const RepeatPlan& plan) {
    std::string out;
    for (std::size_t bar : plan.order) {
        out += static_cast<char>('A' + bar);
    }
    return out;
}

std::string repeated(const std::string& text, int times) {
    std::string out;
    for (int i = 0; i < times; ++i) {
        out += text;
    }
    return out;
}

// ---- MusicXML scores: one whole note per bar

std::string played(const ScoreLoadResult& r) {
    std::string out;
    for (const Note& n : r.song.notes) {
        out += "C D EF G A B"[n.pitch - 60];
    }
    return out;
}

std::vector<int> written(const ScoreLoadResult& r) {
    std::vector<int> out;
    for (const Measure& m : r.song.measures) {
        out.push_back(m.written_number());
    }
    return out;
}

constexpr std::string_view kHead =
    "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
    "</attributes>";

std::string xml_bar(int number, char letter, std::string_view left = "",
                    std::string_view right = "", std::string_view directions = "") {
    return "<measure number=\"" + std::to_string(number) + "\">" +
           std::string(number == 1 ? kHead : "") + std::string(left) + std::string(directions) +
           "<note><pitch><step>" + letter +
           "</step><octave>4</octave></pitch><duration>4</duration></note>" + std::string(right) +
           "</measure>";
}

struct Extra {
    std::string left, right, directions;
};

std::string xml_piece(const std::vector<Extra>& extras) {
    std::string bars;
    for (std::size_t i = 0; i < extras.size(); ++i) {
        bars += xml_bar(static_cast<int>(i) + 1, static_cast<char>('A' + i), extras[i].left,
                        extras[i].right, extras[i].directions);
    }
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?><score-partwise version=\"3.1\"><part-list>"
           "<score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list>"
           "<part id=\"P1\">" +
           bars + "</part></score-partwise>";
}

ScoreLoadResult load_xml(const std::string& xml) {
    return load_musicxml(Bytes(xml.begin(), xml.end()));
}

const std::string kForward = "<barline location=\"left\"><repeat direction=\"forward\"/></barline>";
const std::string kBackward =
    "<barline location=\"right\"><repeat direction=\"backward\"/></barline>";

std::string ending_start(std::string_view numbers) {
    return "<barline location=\"left\"><ending number=\"" + std::string(numbers) +
           "\" type=\"start\"/></barline>";
}

std::string ending_stop(std::string_view numbers, std::string_view type = "stop",
                        std::string_view repeat = "") {
    return "<barline location=\"right\"><ending number=\"" + std::string(numbers) + "\" type=\"" +
           std::string(type) + "\"/>" + std::string(repeat) + "</barline>";
}

// A direction: a <sound> with attributes, and/or words, and/or a glyph.
std::string direction(std::string_view sound_attributes, std::string_view words = "",
                      std::string_view glyph = "") {
    std::string out = "<direction placement=\"above\">";
    if (!words.empty()) {
        out += "<direction-type><words>" + std::string(words) + "</words></direction-type>";
    }
    if (!glyph.empty()) {
        out += "<direction-type><" + std::string(glyph) + "/></direction-type>";
    }
    if (!sound_attributes.empty()) {
        out += "<sound " + std::string(sound_attributes) + "/>";
    }
    return out + "</direction>";
}

// ---- MuseScore scores

constexpr int kPitchOf[7] = {69, 71, 60, 62, 64, 65, 67}; // A to G in the octave from middle C

std::string ms_bar(char letter, std::string_view outside = "", std::string_view inside = "",
                   bool first = false) {
    return "<Measure>" + std::string(outside) + "<voice>" +
           (first ? "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>" : "") + std::string(inside) +
           "<Chord><durationType>whole</durationType><Note><pitch>" +
           std::to_string(kPitchOf[letter - 'A']) +
           "</pitch><tpc>14</tpc></Note></Chord></voice></Measure>";
}

ScoreLoadResult load_ms(std::string_view staff_body) {
    const std::string xml =
        "<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division>"
        "<Part><Staff id=\"1\"><StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName>"
        "<Instrument><Channel><program value=\"0\"/></Channel></Instrument></Part><Staff "
        "id=\"1\">" +
        std::string(staff_body) + "</Staff></Score></museScore>";
    return load_mscx(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
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

// ---- ABC tunes

ScoreLoadResult load_abc_music(const std::string& music,
                               const std::string& header = "M:4/4\nL:1/8\nK:C") {
    const std::string text = "X:1\nT:test\n" + header + "\n" + music + "\n";
    return load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

} // namespace

TEST_CASE("repeat order: signs give the played order") {
    struct Row {
        const char* what;
        const char* spec;
        std::string played;
        int fallbacks = 0;
    };
    const Row rows[] = {
        {"no signs", ";;;", "ABCD"},
        {"a forward and a backward repeat", ";F;B;", "ABCBCD"},
        {"times is the number of plays", "F;B3;", "ABABABC"},
        {"times one plays once", "F;B1;", "ABC"},
        {"times is clamped", "F;B100000;", repeated("AB", kMaxRepeatTimes) + "C"},
        {"a backward repeat with no forward goes to the start", ";B;", "ABABC"},
        {"a second unopened repeat goes back to the bar after the first", "B;;B;", "AABCBCD"},
        {"a forward repeat nothing closes is left out", ";F;", "ABC", 1},
        {"back-to-back repeats", "F;B;F;B;", "ABABCDCDE"},
        {"a one-bar repeat", ";F B;", "ABBC"},
        {"nested repeats", "F;F;B;B;", "ABCBCDABCBCDE"},
        {"first and second endings", "F;;e1 s B;e2 s;", "ABCABDE"},
        {"an ending of two bars", "F;e1;s B;e2;s;", "ABCADEF"},
        {"an ending closed only by its repeat or the next ending", "F;;e1 B;e2;", "ABCABDE"},
        {"three endings", "F;e1 s B;e2 s B;e3 s;", "ABACADE"},
        {"one ending for two passes, then a third", "F;e1,2 s B3;e3 s;", "ABABACD"},
        {"nothing for the other pass", "F;e1 s B;", "ABAC"},
        {"a volta in a repeated passage starts again", "F;F;e1 s B;e2 s;B;", "ABCBDEABCBDEF"},
        {"nested repeats around endings", "F;F;e1 s B;e2 s B;", "ABCBDABCBDE"},
        {"endings with no backward repeat get one implied", "F;e1 s;e2 s B;", "ABACD"},
        {"endings under an open forward repeat get one implied", "F;e1 s;e2 s;", "ABACD"},
        {"endings with no repeat anywhere play straight", ";e1 s;e2 s;", "ABCD", 1},
        {"a closing bracket never opened", ";s;", "ABC", 1},
        {"D.C. goes back to the start once", ";;dc", "ABCABC"},
        {"D.C. al Fine", ";fi;;dc", "ABCDAB"},
        {"D.S. al Coda", "sg;tc;ds;co;", "ABCABDE"},
        {"D.S. al Fine", ";sg;fi;ds", "ABCDBC"},
        {"labels pick the segno and the coda", "sg:x;sg:y;ds:y;tc:q;co:p;co:q", "ABCBCDF"},
        {"Fine and To Coda before a jump do nothing", ";fi;tc;co", "ABCD"},
        {"repeats are not taken again after a D.C.", "F;B;dc", "ABABCABC"},
        {"after a D.C. a volta plays its last ending", "F;e1 s B;e2 s;dc", "ABACDACD"},
        {"a repeat and a D.C. on one bar repeat first", "B dc;", "AAAB"},
        {"jumps are taken once", "sg;ds;ds", "ABABC"},
        {"a D.C. on the first bar", "dc;", "AAB"},
        {"a D.S. to an unknown label goes to the first segno", "sg:x;;ds:nowhere", "ABCABC"},
        {"a D.S. with no segno is dropped", ";;ds", "ABC", 1},
        {"a To Coda with no coda is dropped", ";tc;dc", "ABCABC", 1},
        {"an expansion past the cap falls back to the written order", "F;F;F;;B16;B16;B16",
         "ABCDEFG"},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        const RepeatPlan plan = plan_repeats(marks(row.spec));
        CHECK(word(plan) == row.played);
        CHECK(plan.fallbacks == row.fallbacks);
    }

    // The cap is 16 times the written bars (or max_bars): 2 bars played 16 times fit exactly.
    const auto two = marks("F;B16");
    CHECK(plan_repeats(two).order.size() == 32);
    CHECK_FALSE(plan_repeats(two, 32).capped);
    CHECK(plan_repeats(two, 31).capped);

    CHECK(parse_ending_numbers("1, 2") == 0b110);
    CHECK(parse_ending_numbers("1-3") == 0b1110);
    CHECK(parse_ending_numbers("2 \xE2\x80\x93 3") == 0b1100);
    CHECK(parse_ending_numbers("0") == 0);
    CHECK(parse_ending_numbers("64") == 0);
    CHECK(parse_ending_numbers("1-99999999999") != 0); // a huge range is clamped, not looped
}

TEST_CASE("MusicXML repeats: barlines, endings and jumps are read into the played order") {
    struct Row {
        const char* what;
        std::vector<Extra> bars;
        std::string played;
    };
    const std::string stop_repeat = ending_stop("1", "stop", "<repeat direction=\"backward\"/>");
    const std::string dc = direction("dacapo=\"yes\"");
    const Row rows[] = {
        {"a forward and a backward barline",
         {{kForward, "", ""}, {"", kBackward, ""}, {}},
         "ABABC"},
        {"first and second endings",
         {{kForward, "", ""},
          {},
          {ending_start("1"), stop_repeat, ""},
          {ending_start("2"), ending_stop("2", "discontinue"), ""},
          {}},
         "ABCABDE"},
        {"a forward barline written on the right edge belongs to the next bar",
         {{"", "<barline location=\"right\"><repeat direction=\"forward\"/></barline>", ""},
          {},
          {"<barline location=\"left\"><repeat direction=\"backward\"/></barline>", "", ""},
          {}},
         "ABBCD"},
        {"D.C. al Fine", {{}, {"", "", direction("fine=\"yes\"")}, {}, {"", "", dc}}, "ABCDAB"},
        {"D.S. al Coda from sound marks",
         {{"", "", direction("segno=\"segno\"", "", "segno")},
          {"", "", direction("tocoda=\"coda\"", "To Coda")},
          {"", "", direction("dalsegno=\"segno\"", "D.S. al Coda")},
          {"", "", direction("coda=\"coda\"", "", "coda")},
          {}},
         "ABCABDE"},
        {"D.S. al Coda from words and glyphs alone",
         {{"", "", direction("", "", "segno")},
          {"", "", direction("", "To Coda", "coda")},
          {"", "", direction("", "D.S. al Coda")},
          {"", "", direction("", "", "coda")},
          {}},
         "ABCABDE"},
        {"words that only look like jumps do nothing",
         {{"", "", direction("", "dolce")}, {"", "", direction("", "a fine example")}, {}},
         "ABC"},
        {"endings with no repeat play straight through",
         {{},
          {ending_start("1"), ending_stop("1"), ""},
          {ending_start("2"), ending_stop("2"), ""},
          {}},
         "ABCD"},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        const ScoreLoadResult r = load_xml(xml_piece(row.bars));
        REQUIRE(r.ok);
        CHECK(played(r) == row.played);
    }

    // Played bars are numbered in playing order, the written number is the page's.
    const ScoreLoadResult r = load_xml(xml_piece({{kForward, "", ""},
                                                  {},
                                                  {ending_start("1"), stop_repeat, ""},
                                                  {ending_start("2"), ending_stop("2"), ""},
                                                  {}}));
    REQUIRE(r.ok);
    CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5});
    for (std::size_t i = 0; i < r.song.measures.size(); ++i) {
        CHECK(r.song.measures[i].number == static_cast<int>(i) + 1);
    }
    CHECK(r.stats.bars_written == 5);
    CHECK(r.stats.bars_played == 7);
}

TEST_CASE("MusicXML repeats: a D.C. back to a pickup keeps the pickup short") {
    const std::string pickup =
        "<measure number=\"0\" implicit=\"yes\">" + std::string(kHead) +
        "<note><pitch><step>G</step><octave>4</octave></pitch><duration>1</duration></note>"
        "</measure>";
    std::string xml = xml_piece({});
    xml.insert(xml.find("</part>"),
               pickup + xml_bar(1, 'C') + xml_bar(2, 'D', "", "", direction("dacapo=\"yes\"")));
    const ScoreLoadResult r = load_xml(xml);
    REQUIRE(r.ok);
    CHECK(played(r) == "GCDGCD");
    CHECK(written(r) == std::vector<int>{0, 1, 2, 0, 1, 2});
    // 0.5 s pickup, then two 2 s bars, then the same again.
    CHECK(r.song.notes[3].start_s == doctest::Approx(4.5));
    CHECK(r.song.notes[4].start_s == doctest::Approx(5.0));
}

TEST_CASE("MuseScore repeats: repeat measures, voltas, jumps and markers") {
    struct Row {
        const char* what;
        std::string staff;
        std::string played;
    };
    const Row rows[] = {
        {"a repeat played three times",
         ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "<endRepeat>3</endRepeat>") +
             ms_bar('C'),
         "ABABABC"},
        {"first and second endings, the first over two bars",
         ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "", volta("1", 2)) +
             ms_bar('C', "<endRepeat>2</endRepeat>") + ms_bar('D', "", volta("2", 2)) +
             ms_bar('E') + ms_bar('F'),
         "ABCADEF"},
        {"a first ending with no end repeat goes back to the start repeat",
         ms_bar('A', "<startRepeat/>", "", true) + ms_bar('B', "", volta("1")) +
             ms_bar('C', "<endRepeat>2</endRepeat>", volta("2")) + ms_bar('D'),
         "ABACD"},
        {"D.S. al Coda from a jump and its markers",
         ms_bar('A', "", marker("segno"), true) + ms_bar('B', "", marker("coda")) +
             ms_bar('C', "", jump("segno", "coda", "codab")) + ms_bar('D', "", marker("codab")) +
             ms_bar('E'),
         "ABCABDE"},
        {"D.C. al Fine from a jump and a Fine marker",
         ms_bar('A', "", "", true) + ms_bar('B', "", marker("fine")) +
             ms_bar('C', "", jump("start", "fine")),
         "ABCAB"},
        {"a volta with no repeat and a jump to a missing segno play straight through",
         ms_bar('A', "", "", true) + ms_bar('B', "", volta("1")) + ms_bar('C', "", volta("2")) +
             ms_bar('D', "", jump("segno", "end")),
         "ABCD"},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        const ScoreLoadResult r = load_ms(row.staff);
        REQUIRE(r.ok);
        CHECK(played(r) == row.played);
    }
}

TEST_CASE("ABC repeats: signs and decorations give the played bars") {
    struct Row {
        const char* music;
        std::vector<int> bars; // the written number of each played bar
        const char* header = "M:4/4\nL:1/8\nK:C";
    };
    const char* quarters = "M:4/4\nL:1/4\nK:C";
    const Row rows[] = {
        {"|: C8 | D8 :| E8 |]", {1, 2, 1, 2, 3}},
        {"C8 | D8 :| E8 |]", {1, 2, 1, 2, 3}},
        {"|: C8 :|: D8 :| E8 |]", {1, 1, 2, 2, 3}},
        {"|: C8 | D8 |1 E8 :|2 F8 | G8 |]", {1, 2, 3, 1, 2, 4, 5}},
        {"|: C8 | D8 [1 E8 :| [2 F8 | G8 |]", {1, 2, 3, 1, 2, 4, 5}},
        {"C8 | D8 |1 E8 :|2 F8 | G8 |]", {1, 2, 3, 1, 2, 4, 5}},
        {"|: C D E F :|1 G A B c :|2 d e f g |]", {1, 2, 1, 3}, quarters},
        {"|: CDEF | GABc :|1 defg | abcd :|2 efga |]", {1, 2, 3, 4, 1, 2, 5}, quarters},
        {"|: C8 |1,2 D8 :|3 E8 |]", {1, 2, 1, 2, 1, 3}},
        {"|: C8 |1 D8 | E8 :|2 F8 | G8 |]", {1, 2, 3, 1, 4, 5}},
        {"C8 | D8 !D.C.! | E8 |]", {1, 2, 1, 2, 3}},
        {"C8 | D8 !fine! | E8 !D.C.! |]", {1, 2, 3, 1, 2}},
        {"C8 | !segno! D8 | E8 !D.S.! | F8 |]", {1, 2, 3, 2, 3, 4}},
    };
    for (const Row& row : rows) {
        CAPTURE(row.music);
        const ScoreLoadResult r = load_abc_music(row.music, row.header);
        REQUIRE(r.ok);
        std::vector<int> bars;
        for (const Measure& m : r.song.measures) {
            bars.push_back(m.written_number());
        }
        CHECK(bars == row.bars);
    }
    // A sign with nothing to repeat never hangs.
    for (const char* music : {"C8 | :| D8 |: E8 |]", "|: |: |: C8 :| :| :| D8 |]"}) {
        CHECK(load_abc_music(music).ok);
    }
}

TEST_CASE("repeats: a tie into the second ending is struck again on the pass that reaches it") {
    // ABC: |: C | D E- |1 E F :|2 E G |]
    const ScoreLoadResult abc = load_abc_music("|: C8 | D4 E4- |1 E4 F4 :|2 E4 G4 |]");
    REQUIRE(abc.ok);
    std::vector<double> e_lengths;
    for (const Note& n : abc.song.notes) {
        if (n.pitch == 64) {
            e_lengths.push_back(static_cast<double>(n.end_tick - n.start_tick) /
                                abc.song.tempo.ticks_per_quarter);
        }
    }
    // Pass one: the tied E and the first ending's E. Pass two: the tied E, then a fresh E.
    REQUIRE(e_lengths.size() == 3);
    CHECK(e_lengths[0] == doctest::Approx(4.0));
    CHECK(e_lengths[1] == doctest::Approx(2.0));
    CHECK(e_lengths[2] == doctest::Approx(2.0));

    // MusicXML, from a file: |: C |1. E G~ :| 2. ~G B.
    const ScoreLoadResult xml = load_musicxml(read_data("repeats-tie-into-ending.musicxml"));
    REQUIRE(xml.ok);
    CHECK(played(xml) == "CEGCGB");
    CHECK(xml.stats.ties_merged == 0);
    CHECK(xml.stats.ties_not_continued == 1);
}

TEST_CASE("repeats: the voltas piece plays like MuseScore's own export, in every format") {
    // 7 written bars, played 1 2 3 | 1 2 4 | 5 6 | 5 6 | 5 7; MuseScore's MIDI export is the
    // reference for the notes.
    const MidiLoadResult midi = load_midi(read_data("repeats-voltas-musescore-export.mid"));
    REQUIRE(midi.ok);
    using Hit = std::tuple<std::int64_t, int>;
    auto hits_of = [](const Song& song) {
        std::vector<Hit> out;
        for (const Note& n : song.notes) {
            out.emplace_back(n.start_tick, n.pitch);
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    const std::vector<Hit> reference = hits_of(midi.song);
    REQUIRE(reference.size() == 44);
    const ScoreLoadResult results[] = {load_musicxml(read_data("repeats-voltas.musicxml")),
                                       load_mxl(read_data("repeats-voltas.mxl")),
                                       load_mscz(read_data("repeats-voltas-musescore4.mscz")),
                                       load_score(read_data("repeats-voltas.mxl"))};
    for (const ScoreLoadResult& r : results) {
        REQUIRE(r.ok);
        CHECK(written(r) == std::vector<int>{1, 2, 3, 1, 2, 4, 5, 6, 5, 6, 5, 7});
        CHECK(r.stats.repeats_ignored == 0);
        CHECK(hits_of(r.song) == reference);
    }
}
