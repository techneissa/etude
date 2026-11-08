// SPDX-License-Identifier: GPL-3.0-or-later
// harmony: symbols (split from harmony_test.cpp).
#include "etude/harmony.hpp"
#include "etude/musicxml.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

#include "harmony_test_support.hpp"

using namespace etude;
using namespace harmony_test_support;

// ---- Chord symbols the file writes
// -----------------------------------------------------------------

namespace {

ChordSymbol symbol(std::int64_t tick, int root_pc, ChordQuality q, int bass_pc, int root_line,
                   int bass_line) {
    ChordSymbol s;
    s.tick = tick;
    s.seconds = static_cast<double>(tick) / (2.0 * kQuarter);
    s.chord = Chord{true, root_pc, q, bass_pc};
    s.spelling = ChordSpelling{root_line, bass_line};
    return s;
}

} // namespace

TEST_CASE("harmony: the chord symbols of a file are the chords, and detection is skipped") {
    Piece p(2);
    p.whole(0, {C3, G3, E4}); // the notes say C ...
    p.whole(1, {C3, G3, E4});
    Song& s = p.song();
    // ... the file says Am7 and then D.
    s.chord_symbols.push_back(symbol(0, 9, ChordQuality::Minor7, 9, 3, 3));
    s.chord_symbols.push_back(symbol(4 * kQuarter, 2, ChordQuality::Major, 2, 2, 2));
    const std::vector<HarmonyStretch> stretches = analyse_harmony(s, HarmonyScope::All);
    REQUIRE(stretches.size() == 2);
    CHECK(harmony_name(stretches[0]) == "Am7");
    CHECK(harmony_name(stretches[1]) == "D");
    CHECK(stretches[0].from_file);
    CHECK(stretches[0].confidence == doctest::Approx(1.0f));
    CHECK(stretches[0].end_tick == 4 * kQuarter);
    CHECK(stretches[1].end_tick == 8 * kQuarter);
}

TEST_CASE("harmony: a file's own spelling, slash bass and 'no chord' are kept") {
    Piece p(3);
    p.whole(0, {C3, G3, E4});
    p.whole(1, {C3, G3, E4});
    p.whole(2, {C3, G3, E4});
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 10, ChordQuality::Dominant7, 4, -2, 4)); // Bb7/E
    ChordSymbol none;
    none.tick = 4 * kQuarter;
    s.chord_symbols.push_back(none);                                                   // N.C.
    s.chord_symbols.push_back(symbol(8 * kQuarter, 1, ChordQuality::Major7, 1, 7, 7)); // C#maj7
    const std::vector<HarmonyStretch> stretches = analyse_harmony(s, HarmonyScope::All);
    REQUIRE(stretches.size() == 3);
    CHECK(harmony_name(stretches[0]) == "Bb7/E");
    CHECK_FALSE(stretches[1].chord.found);
    CHECK(harmony_name(stretches[2]) == "C#maj7");
}

TEST_CASE("harmony: equal symbols one after another become one stretch") {
    Piece p(2);
    p.whole(0, {C3, G3, E4});
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 0, ChordQuality::Major, 0, 0, 0));
    s.chord_symbols.push_back(symbol(2 * kQuarter, 0, ChordQuality::Major, 0, 0, 0));
    s.chord_symbols.push_back(symbol(4 * kQuarter, 0, ChordQuality::Major, 0, 0, 0));
    CHECK(analyse_harmony(s, HarmonyScope::All).size() == 1);
}

TEST_CASE("harmony: with symbols a hand is named from its own notes, never from the file") {
    Piece p(2, 3, 4);
    // Bar 1: the left hand plays C, E, G; the right hand only a tune. Bar 2: the tune alone.
    p.chord(0, 0.0, {C3}, 0.9, Hand::Left).chord(0, 1.0, {E3, G3}, 0.9, Hand::Left);
    p.chord(0, 2.0, {E3, G3}, 0.9, Hand::Left);
    p.note(0, 0.0, G4, 1.0).note(0, 1.0, C5, 1.0).note(0, 2.0, E5, 1.0);
    p.note(1, 0.0, G4, 3.0);
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 0, ChordQuality::Major, 0, 0, 0));
    const std::vector<HarmonyStretch> left = analyse_harmony(s, HarmonyScope::Left);
    const std::vector<HarmonyStretch> right = analyse_harmony(s, HarmonyScope::Right);
    const std::vector<HarmonyStretch> all = analyse_harmony(s, HarmonyScope::All);
    // The one symbol reaches to the end of its bar; bar 2 is a lone tune note.
    REQUIRE(all.size() == 2);
    CHECK(harmony_name(all[0]) == "C");
    CHECK(all[0].end_tick == 3 * kQuarter);
    CHECK_FALSE(all[1].chord.found);
    // The left hand plays the C chord in bar 1 itself (C, E and G, the E and G coming back).
    REQUIRE(left.size() >= 1);
    CHECK(harmony_name(left[0]) == "C");
    // The right hand plays a tune over it: the file's chord is not the hand's.
    REQUIRE(right.size() >= 1);
    CHECK(right[0].state == HarmonyState::NoChord);
    CHECK_FALSE(right[0].chord.found);
}

TEST_CASE("harmony: a lead sheet labelled in the first bars is detected after them") {
    // Twelve bars: symbols in bars 1 to 8 (a chord a bar), then a held Am in bars 9 to 12.
    Piece p(12);
    const std::array<std::array<int, 3>, 4> chords{
        {{C3, G3, E4}, {F3, C4, 69}, {G2, D3, 71}, {C3, G3, E4}}};
    const int roots[4] = {0, 5, 7, 0};
    for (int b = 0; b < 8; ++b) {
        const auto& c = chords[static_cast<std::size_t>(b) % 4];
        p.whole(b, {c[0], c[1], c[2]});
    }
    for (int b = 8; b < 12; ++b) {
        p.whole(b, {45, 48, 52}); // A2 C3 E3
    }
    Song& s = p.song();
    for (int b = 0; b < 8; ++b) {
        const int r = roots[b % 4];
        s.chord_symbols.push_back(symbol(b * 4 * kQuarter, r, ChordQuality::Major, r, 0, 0));
    }
    const std::vector<HarmonyStretch> st = analyse_harmony(s, HarmonyScope::All);
    // Bar 8's symbol (C) is not carried to the end: it ends with its bar and the notes take over.
    const HarmonyStretch* bar8 = harmony_at_tick(st, 7 * 4 * kQuarter);
    REQUIRE(bar8 != nullptr);
    CHECK(bar8->from_file);
    CHECK(bar8->end_tick == 8 * 4 * kQuarter);
    const HarmonyStretch* bar9 = harmony_at_tick(st, 8 * 4 * kQuarter);
    REQUIRE(bar9 != nullptr);
    CHECK_FALSE(bar9->from_file);
    CHECK(harmony_name(*bar9) == "Am");
    CHECK(bar9->end_tick == 12 * 4 * kQuarter);
    CHECK(st.back().end_tick == s.end_tick);
}

TEST_CASE("harmony: a symbol reaches as far as the file's own spacing, then the notes decide") {
    // Symbols in bars 1 and 4 (three bars apart): the first reaches three bars. The last, in bar 4,
    // reaches the same three bars past it.
    Piece p(8);
    for (int b = 0; b < 8; ++b) {
        p.whole(b, {C3, G3, E4});
    }
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 9, ChordQuality::Minor, 9, 3, 3));
    s.chord_symbols.push_back(symbol(3 * 4 * kQuarter, 2, ChordQuality::Major, 2, 2, 2));
    const std::vector<HarmonyStretch> st = analyse_harmony(s, HarmonyScope::All);
    REQUIRE(st.size() == 3);
    CHECK(harmony_name(st[0]) == "Am");
    CHECK(st[0].end_tick == 3 * 4 * kQuarter);
    CHECK(harmony_name(st[1]) == "D");
    CHECK(st[1].end_tick == 6 * 4 * kQuarter);
    CHECK_FALSE(st[2].from_file); // bars 7 and 8: the notes say C
    CHECK(harmony_name(st[2]) == "C");
}

TEST_CASE("harmony: N.C. in the file ends the chord before it") {
    Piece p(3);
    for (int b = 0; b < 3; ++b) {
        p.whole(b, {C3, G3, E4});
    }
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 0, ChordQuality::Major, 0, 0, 0));
    ChordSymbol none;
    none.tick = 4 * kQuarter;
    s.chord_symbols.push_back(none);
    s.chord_symbols.push_back(symbol(8 * kQuarter, 0, ChordQuality::Major, 0, 0, 0));
    const std::vector<HarmonyStretch> st = analyse_harmony(s, HarmonyScope::All);
    REQUIRE(st.size() == 3);
    CHECK(st[1].state == HarmonyState::NoChord);
    CHECK(st[1].from_file);
    CHECK(st[0].end_tick == 4 * kQuarter);
}

TEST_CASE("harmony: a symbol's span leaves out the tune") {
    Piece p(1, 3, 4);
    p.chord(0, 0.0, {C2}, 0.9, Hand::Left).chord(0, 1.0, {E3, G3}, 0.9, Hand::Left);
    p.chord(0, 2.0, {E3, G3}, 0.9, Hand::Left);
    p.note(0, 0.0, G4, 1.0).note(0, 1.0, C5, 1.0).note(0, 2.0, E5, 1.0);
    Song& s = p.song();
    s.chord_symbols.push_back(symbol(0, 0, ChordQuality::Major, 0, 0, 0));
    const HarmonyStretch st = analyse_harmony(s, HarmonyScope::All).front();
    REQUIRE(st.from_file);
    CHECK(st.low_pitch == C2);
    CHECK(st.high_pitch == G3);
}

TEST_CASE("harmony: lookups find the stretch at a time or a tick") {
    Piece p(2);
    p.whole(0, {C3, G3, E4});
    p.whole(1, {F3, C4, 69});
    const std::vector<HarmonyStretch> s = analyse_harmony(p.song(), HarmonyScope::All);
    REQUIRE(s.size() == 2);
    CHECK(harmony_at_seconds(s, 0.0) == &s[0]);
    CHECK(harmony_at_seconds(s, 1.99) == &s[0]);
    CHECK(harmony_at_seconds(s, 2.0) == &s[1]);
    CHECK(harmony_at_seconds(s, 3.99) == &s[1]);
    CHECK(harmony_at_seconds(s, 4.0) == nullptr);
    CHECK(harmony_at_seconds(s, -1.0) == nullptr);
    CHECK(harmony_at_tick(s, 0) == &s[0]);
    CHECK(harmony_at_tick(s, 4 * kQuarter) == &s[1]);
    CHECK(harmony_at_tick(s, 8 * kQuarter) == nullptr);
    CHECK(harmony_at_tick({}, 0) == nullptr);
    CHECK(harmony_name(HarmonyStretch{}).empty());
}

TEST_CASE("harmony: a song with no bars or no notes has no chords") {
    Song empty;
    CHECK(analyse_harmony(empty, HarmonyScope::All).empty());
    CHECK(build_harmony(empty).all.empty());
    Piece bars(3);
    for (const HarmonyStretch& s : analyse_harmony(bars.song(), HarmonyScope::All)) {
        CHECK_FALSE(s.chord.found);
    }
}

// ---- MusicXML <harmony>
// ----------------------------------------------------------------------------

namespace {

std::string harmony_xml(std::string_view root, std::string_view kind, std::string_view extra = "") {
    std::string out = "<harmony><root><root-step>";
    out += root.substr(0, 1);
    out += "</root-step>";
    if (root.size() > 1) {
        out += root[1] == '#' ? "<root-alter>1</root-alter>" : "<root-alter>-1</root-alter>";
    }
    out += "</root><kind>";
    out += kind;
    out += "</kind>";
    out += extra;
    out += "</harmony>";
    return out;
}

std::string rest_bar(int number, std::string_view body, bool repeat_forward = false,
                     bool repeat_backward = false) {
    std::string out = "<measure number=\"" + std::to_string(number) + "\">";
    if (number == 1) {
        out += "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type>"
               "</time></attributes>";
    }
    if (repeat_forward) {
        out += "<barline location=\"left\"><repeat direction=\"forward\"/></barline>";
    }
    out += body;
    out += "<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration></note>";
    if (repeat_backward) {
        out += "<barline location=\"right\"><repeat direction=\"backward\"/></barline>";
    }
    out += "</measure>";
    return out;
}

ScoreLoadResult load_xml(const std::string& measures) {
    const std::string xml =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list><score-part id=\"P1\">"
        "<part-name>Piano</part-name></score-part></part-list><part id=\"P1\">" +
        measures + "</part></score-partwise>";
    return load_musicxml(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
}

} // namespace

TEST_CASE("musicxml: harmony elements become the song's chord symbols") {
    const ScoreLoadResult r = load_xml(
        rest_bar(1,
                 harmony_xml("F#", "major-seventh",
                             "<bass><bass-step>E</bass-step><bass-alter>1</bass-alter></bass>")) +
        rest_bar(2, harmony_xml("B", "minor-seventh")) +
        rest_bar(3, harmony_xml("B-", "dominant")));
    REQUIRE(r.ok);
    const std::vector<ChordSymbol>& c = r.song.chord_symbols;
    REQUIRE(c.size() == 3);
    CHECK(c[0].tick == 0);
    CHECK(c[0].chord.found);
    CHECK(c[0].chord.root_pc == 6);
    CHECK(c[0].chord.quality == ChordQuality::Major7);
    CHECK(c[0].chord.bass_pc == 5);
    CHECK(c[0].spelling.root_line == 6);
    CHECK(c[0].spelling.bass_line == 11); // E#
    CHECK(spelled_chord_name(c[0].chord, c[0].spelling) == "F#maj7/E#");
    CHECK(c[1].tick == 4 * 480);
    CHECK(spelled_chord_name(c[1].chord, c[1].spelling) == "Bm7");
    CHECK(c[2].tick == 8 * 480);
    CHECK(spelled_chord_name(c[2].chord, c[2].spelling) == "Bb7"); // the file's own flat
    CHECK(c[1].seconds == doctest::Approx(r.song.tempo.seconds_at(4 * 480)));
}

TEST_CASE("musicxml: the kinds the app can name, and the ones it cannot") {
    struct Case {
        const char* kind;
        ChordQuality quality;
    };
    for (const Case& k :
         {Case{"major", ChordQuality::Major}, Case{"minor", ChordQuality::Minor},
          Case{"augmented", ChordQuality::Augmented}, Case{"diminished", ChordQuality::Diminished},
          Case{"dominant", ChordQuality::Dominant7},
          Case{"half-diminished", ChordQuality::HalfDiminished7},
          Case{"diminished-seventh", ChordQuality::Diminished7},
          Case{"major-sixth", ChordQuality::Major6}, Case{"minor-sixth", ChordQuality::Minor6},
          Case{"dominant-ninth", ChordQuality::Dominant9},
          Case{"dominant-13th", ChordQuality::Dominant13},
          Case{"suspended-fourth", ChordQuality::Sus4},
          Case{"suspended-second", ChordQuality::Sus2}, Case{"power", ChordQuality::Power}}) {
        const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("C", k.kind)));
        REQUIRE(r.ok);
        REQUIRE(r.song.chord_symbols.size() == 1);
        CHECK(r.song.chord_symbols[0].chord.quality == k.quality);
    }
    for (const char* kind : {"none", "other", "Neapolitan", "dominant-11th", "augmented-seventh"}) {
        const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("C", kind)));
        REQUIRE(r.ok);
        REQUIRE(r.song.chord_symbols.size() == 1);
        CHECK_FALSE(r.song.chord_symbols[0].chord.found); // a symbol, naming nothing
    }
}

TEST_CASE("musicxml: degrees give 7sus4, 7b9 and 6/9, and any other change names nothing") {
    const auto degree = [](int value, int alter, const char* type) {
        return "<degree><degree-value>" + std::to_string(value) + "</degree-value><degree-alter>" +
               std::to_string(alter) + "</degree-alter><degree-type>" + type +
               "</degree-type></degree>";
    };
    const auto quality_of = [&](const char* kind, const std::string& extra) {
        const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("C", kind, extra)));
        REQUIRE(r.ok);
        REQUIRE(r.song.chord_symbols.size() == 1);
        return r.song.chord_symbols[0].chord.found ? r.song.chord_symbols[0].chord.quality
                                                   : ChordQuality::None;
    };
    CHECK(quality_of("suspended-fourth", degree(7, 0, "add")) == ChordQuality::Dominant7Sus4);
    CHECK(quality_of("dominant", degree(9, -1, "add")) == ChordQuality::Dominant7Flat9);
    CHECK(quality_of("dominant", degree(9, -1, "alter")) == ChordQuality::Dominant7Flat9);
    CHECK(quality_of("major-sixth", degree(9, 0, "add")) == ChordQuality::Major69);
    CHECK(quality_of("major-seventh", degree(5, 1, "alter")) == ChordQuality::None);
    CHECK(quality_of("major", degree(7, 0, "subtract")) == ChordQuality::None);
    CHECK(quality_of("major", degree(9, 0, "add")) == ChordQuality::Add9);
    CHECK(quality_of("minor", degree(9, 0, "add")) == ChordQuality::None); // not named
}

TEST_CASE("musicxml: a symbol in the middle of a bar, and a numeral, a missing root") {
    const std::string body =
        "<note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration></note>" +
        harmony_xml("G", "dominant") +
        "<note><pitch><step>D</step><octave>4</octave></pitch><duration>2</duration></note>";
    std::string bar =
        "<measure number=\"1\"><attributes><divisions>1</divisions><time><beats>4</beats>"
        "<beat-type>4</beat-type></time></attributes>" +
        harmony_xml("C", "major") + body +
        "<harmony><function>V</function><kind>dominant</kind></harmony>"
        "</measure>";
    const ScoreLoadResult r = load_xml(bar);
    REQUIRE(r.ok);
    REQUIRE(r.song.chord_symbols.size() == 3); // the numeral is a symbol that names no chord
    CHECK(r.song.chord_symbols[0].tick == 0);
    CHECK(r.song.chord_symbols[1].tick == 2 * 480);
    CHECK_FALSE(r.song.chord_symbols[2].chord.found);
    CHECK(spelled_chord_name(r.song.chord_symbols[1].chord, r.song.chord_symbols[1].spelling) ==
          "G7");
}

TEST_CASE("musicxml: N.C. and a numeral are symbols that end the chord before them") {
    const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("C", "major")) +
                                       rest_bar(2, "<harmony><kind>none</kind></harmony>") +
                                       rest_bar(3, harmony_xml("F", "major")));
    REQUIRE(r.ok);
    REQUIRE(r.song.chord_symbols.size() == 3);
    CHECK(r.song.chord_symbols[0].chord.found);
    CHECK_FALSE(r.song.chord_symbols[1].chord.found);
    CHECK(r.song.chord_symbols[1].tick == 4 * 480);
    const std::vector<HarmonyStretch> st = analyse_harmony(r.song, HarmonyScope::All);
    REQUIRE(st.size() >= 3);
    CHECK(st[0].state == HarmonyState::Chord);
    CHECK(st[1].state == HarmonyState::NoChord);
    CHECK(st[1].start_tick == 4 * 480);
}

TEST_CASE("musicxml: a keyboard part's symbols win over a vocal part's") {
    const auto bar = [](int number, const std::string& harmony) {
        return "<measure number=\"" + std::to_string(number) + "\">" +
               (number == 1
                    ? std::string("<attributes><divisions>1</divisions><time><beats>4</beats>"
                                  "<beat-type>4</beat-type></time></attributes>")
                    : std::string()) +
               harmony +
               "<note><pitch><step>C</step><octave>4</octave></pitch><duration>4</duration>"
               "</note></measure>";
    };
    // The vocal part is listed first and writes Am in bars 1, 2 and 3; the piano writes Am7 in bar
    // 1 and bar 2 and nothing in bar 3.
    const std::string xml =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>Voice</part-name></score-part>"
        "<score-part id=\"P2\"><part-name>Piano</part-name></score-part></part-list>"
        "<part id=\"P1\">" +
        bar(1, harmony_xml("A", "minor")) + bar(2, harmony_xml("A", "minor")) +
        bar(3, harmony_xml("A", "minor")) + "</part><part id=\"P2\">" +
        bar(1, harmony_xml("A", "minor-seventh")) + bar(2, harmony_xml("A", "minor-seventh")) +
        bar(3, "") + "</part></score-partwise>";
    const ScoreLoadResult r =
        load_musicxml(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
    REQUIRE(r.ok);
    const std::vector<ChordSymbol>& c = r.song.chord_symbols;
    REQUIRE(c.size() == 3);
    CHECK(c[0].chord.quality == ChordQuality::Minor7); // the piano's, not the vocal's Am
    CHECK(c[1].chord.quality == ChordQuality::Minor7);
    CHECK(c[2].chord.quality == ChordQuality::Minor); // the piano writes none in bar 3: the vocal's
}

TEST_CASE("musicxml: a part is a keyboard by its name, its sound or its General MIDI program") {
    const auto winner = [](const std::string& keys_part_info) {
        // Two parts write the same bar: a choir (program 53) first, then the part under test.
        const std::string bar =
            "<measure number=\"1\"><attributes><divisions>1</divisions><time><beats>4</beats>"
            "<beat-type>4</beat-type></time></attributes>";
        const std::string note = "<note><pitch><step>C</step><octave>4</octave></"
                                 "pitch><duration>4</duration></note></measure>";
        const std::string xml =
            "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>"
            "<score-part id=\"P1\"><part-name>Choir</part-name><midi-instrument id=\"i1\">"
            "<midi-program>53</midi-program></midi-instrument></score-part>"
            "<score-part id=\"P2\">" +
            keys_part_info + "</score-part></part-list><part id=\"P1\">" + bar +
            harmony_xml("C", "major") + note + "</part><part id=\"P2\">" + bar +
            harmony_xml("C", "minor") + note + "</part></score-partwise>";
        const ScoreLoadResult r =
            load_musicxml(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
        REQUIRE(r.ok);
        REQUIRE(r.song.chord_symbols.size() == 1);
        return r.song.chord_symbols[0].chord.quality;
    };
    CHECK(winner("<part-name>Klavier</part-name>") == ChordQuality::Minor);
    CHECK(
        winner(
            "<part-name>Part 2</part-name><midi-instrument id=\"i2\"><midi-program>5</midi-program>"
            "</midi-instrument>") == ChordQuality::Minor);
    CHECK(winner("<part-name>Part 2</part-name><score-instrument id=\"i2\"><instrument-sound>"
                 "keyboard.piano</instrument-sound></score-instrument>") == ChordQuality::Minor);
    CHECK(winner("<part-name>Strings</part-name>") ==
          ChordQuality::Major); // neither: the first part wins
}

TEST_CASE("musicxml: the symbols follow the repeats along the order the bars play") {
    const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("C", "major"), true) +
                                       rest_bar(2, harmony_xml("G", "major"), false, true) +
                                       rest_bar(3, harmony_xml("D", "minor")));
    REQUIRE(r.ok);
    const std::vector<ChordSymbol>& c = r.song.chord_symbols;
    REQUIRE(c.size() == 5); // C G C G Dm
    const int roots[] = {0, 7, 0, 7, 2};
    for (std::size_t i = 0; i < c.size(); ++i) {
        CHECK(c[i].chord.root_pc == roots[i]);
        CHECK(c[i].tick == static_cast<std::int64_t>(i) * 4 * 480);
    }
}

TEST_CASE("musicxml: the first symbol at a place wins and a file without symbols has none") {
    const std::string two = harmony_xml("C", "major") + harmony_xml("D", "minor");
    const ScoreLoadResult r = load_xml(rest_bar(1, two));
    REQUIRE(r.ok);
    REQUIRE(r.song.chord_symbols.size() == 1);
    CHECK(r.song.chord_symbols[0].chord.root_pc == 0);
    const ScoreLoadResult plain = load_xml(rest_bar(1, ""));
    REQUIRE(plain.ok);
    CHECK(plain.song.chord_symbols.empty());
}

TEST_CASE("harmony: a loaded score with symbols names its chords from them") {
    const ScoreLoadResult r = load_xml(rest_bar(1, harmony_xml("A", "minor")) +
                                       rest_bar(2, harmony_xml("E", "dominant")));
    REQUIRE(r.ok);
    const std::vector<HarmonyStretch> s = analyse_harmony(r.song, HarmonyScope::All);
    REQUIRE(s.size() == 2);
    CHECK(harmony_name(s[0]) == "Am");
    CHECK(harmony_name(s[1]) == "E7");
    CHECK(s[0].from_file);
}
