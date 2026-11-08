// SPDX-License-Identifier: GPL-3.0-or-later
// musicxml: the first topics (split from musicxml_test.cpp).
#include "etude/musicxml.hpp"
#include "etude/library.hpp"
#include "etude/song_builder.hpp"
#include "zip_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <string>
#include <string_view>
#include <vector>

#include "musicxml_test_support.hpp"

using namespace etude;
using namespace musicxml_test_support;

namespace {

std::uint32_t us_of(double bpm) { return us_per_quarter_from_bpm(bpm); }

std::string rest(int duration) {
    return "<note><rest/><duration>" + std::to_string(duration) + "</duration></note>";
}

} // namespace

TEST_CASE("musicxml: one quarter note at the default tempo") {
    const ScoreLoadResult r =
        load(piano_score(measure(1, std::string(kFourFour) + note("C4", 1) + rest(3))));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    const Note& n = r.song.notes[0];
    CHECK(n.pitch == 60);
    CHECK(n.start_s == doctest::Approx(0.0));
    CHECK(n.duration_s == doctest::Approx(0.5)); // 120 BPM
    CHECK(n.velocity == 80);                     // mf
    CHECK(n.measure == 1);
    CHECK(n.end_tick - n.start_tick == r.song.tempo.ticks_per_quarter);
}

TEST_CASE("musicxml: pitch spelling gives the MIDI number") {
    // A0 = 21, C4 = 60, F#5 = 78, Bb3 = 58, C8 = 108.
    std::string body =
        std::string(kFourFour) + note("A0", 1) + note("C4", 1) + note("F#5", 1) + note("Bb3", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 4);
    CHECK(r.song.notes[0].pitch == 21);
    CHECK(r.song.notes[1].pitch == 60);
    CHECK(r.song.notes[2].pitch == 78);
    CHECK(r.song.notes[3].pitch == 58);
}

TEST_CASE("musicxml: divisions other than 1 scale lengths to seconds") {
    // Divisions 4: a note of duration 6 is a dotted quarter = 0.75 s at 120 BPM.
    const std::string body =
        "<attributes><divisions>4</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
        "</attributes>" +
        note("C4", 6) + note("D4", 2);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(0.75));
    CHECK(r.song.notes[1].start_s == doctest::Approx(0.75));
    CHECK(r.song.notes[1].duration_s == doctest::Approx(0.25));
}

TEST_CASE("musicxml: odd divisions values stay exact") {
    // Triplet eighths with divisions 3: each is a third of a quarter, 1/6 s.
    const std::string body = "<attributes><divisions>3</divisions></attributes>" + note("C4", 1) +
                             note("D4", 1) + note("E4", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.notes[2].start_s == doctest::Approx(2.0 / 6.0));
    CHECK(r.song.notes[2].duration_s == doctest::Approx(1.0 / 6.0));
    CHECK(r.song.notes[2].start_tick * 3 % r.song.tempo.ticks_per_quarter == 0);
}

TEST_CASE("musicxml: a chord starts its notes together and does not advance time") {
    const std::string body = std::string(kFourFour) + note("C4", 2) + note("E4", 2, "<chord/>|") +
                             note("G4", 2, "<chord/>|") + note("D4", 2);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 4);
    CHECK(r.song.notes[0].start_tick == r.song.notes[1].start_tick);
    CHECK(r.song.notes[0].start_tick == r.song.notes[2].start_tick);
    const std::int64_t half = r.song.tempo.ticks_per_quarter * 2;
    CHECK(r.song.notes[3].start_tick == half);
    CHECK(r.song.notes[3].pitch == 62);
}

TEST_CASE("musicxml: backup puts a second voice over the first") {
    const std::string body = std::string(kFourFour) + note("E5", 2, "|<voice>1</voice>") +
                             note("D5", 2, "|<voice>1</voice>") +
                             "<backup><duration>4</duration></backup>" +
                             note("C4", 4, "|<voice>2</voice>");
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 3);
    // Sorted by start tick, then pitch: C4 and E5 at 0, D5 at beat 3.
    CHECK(r.song.notes[0].pitch == 60);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(2.0));
    CHECK(r.song.notes[1].pitch == 76);
    CHECK(r.song.notes[1].start_tick == 0);
    CHECK(r.song.notes[2].pitch == 74);
    CHECK(r.song.notes[2].start_s == doctest::Approx(1.0));
}

TEST_CASE("musicxml: forward skips time without a rest") {
    const std::string body = std::string(kFourFour) + note("C4", 1) +
                             "<forward><duration>2</duration></forward>" + note("D4", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[1].start_s == doctest::Approx(1.5));
}

TEST_CASE("musicxml: a score without notes says so") {
    const ScoreLoadResult r = load(piano_score(measure(1, std::string(kFourFour) + rest(4))));
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NoNotes);
    CHECK_FALSE(r.message.empty());
}

TEST_CASE("musicxml: text that is not XML is rejected") {
    const Bytes b = bytes_of("this is not a score");
    const ScoreLoadResult r = load_musicxml(b);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NotXml);
    const ScoreLoadResult e = load_musicxml(nullptr, 0);
    CHECK_FALSE(e.ok);
}

TEST_CASE("musicxml: XML of another kind is not a score") {
    const ScoreLoadResult r = load("<?xml version=\"1.0\"?><svg><rect/></svg>");
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NotScore);
}

// ---- Bars, tempo and time signatures

TEST_CASE("musicxml: bars are numbered from 1 and carry their notes") {
    std::string bars;
    for (int m = 1; m <= 3; ++m) {
        bars += measure(m, (m == 1 ? std::string(kFourFour) : std::string()) + note("C4", 4));
    }
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 3);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.measures[2].number == 3);
    CHECK(r.song.measures[1].start_tick == r.song.tempo.ticks_per_quarter * 4);
    CHECK(r.song.measures[1].start_s == doctest::Approx(2.0));
    CHECK(r.song.notes[2].measure == 3);
    REQUIRE(r.song.time_signatures.size() == 1);
    CHECK(r.song.time_signatures[0].numerator == 4);
    CHECK(r.song.time_signatures[0].denominator == 4);
}

TEST_CASE("musicxml: an implicit first bar is a pickup numbered 0") {
    const std::string bars = "<measure number=\"0\" implicit=\"yes\">" + std::string(kFourFour) +
                             note("G4", 1) + "</measure>" + measure(1, note("C5", 4)) +
                             measure(2, note("C5", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() >= 3);
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.measures[1].number == 1);
    CHECK(r.song.measures[1].start_tick == r.song.tempo.ticks_per_quarter); // pickup is 1 beat
    CHECK(r.song.notes[0].measure == 0);
    CHECK(r.song.notes[1].measure == 1);
    CHECK(r.song.notes[1].start_s == doctest::Approx(0.5));
}

TEST_CASE("musicxml: a first bar the file numbers 0 is the pickup even when it is full length") {
    const std::string bars = "<measure number=\"0\">" + std::string(kFourFour) + note("G4", 4) +
                             "</measure>" + measure(1, note("C5", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.notes[1].measure == 1);
}

TEST_CASE("musicxml: a short first bar not marked implicit is still a pickup") {
    const std::string bars = measure(1, std::string(kFourFour) + note("G4", 1)) +
                             measure(2, note("C5", 4)) + measure(3, note("C5", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.measures[1].number == 1);
}

TEST_CASE("musicxml: a sound tempo sets the tempo map, a later one changes it") {
    const std::string bars =
        measure(1, std::string(kFourFour) + "<direction><sound tempo=\"60\"/></direction>" +
                       note("C4", 4)) +
        measure(2, "<direction><sound tempo=\"120\"/></direction>" + note("D4", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.tempo.changes.size() == 2);
    CHECK(r.song.tempo.changes[0].us_per_quarter == 1'000'000);
    CHECK(r.song.tempo.changes[1].us_per_quarter == 500'000);
    CHECK(r.song.tempo.changes[1].tick == 4 * r.song.tempo.ticks_per_quarter);
    // Bar 1 is four beats at 60 BPM = 4 s, then bar 2 at 120 BPM.
    CHECK(r.song.notes[1].start_s == doctest::Approx(4.0));
    CHECK(r.song.notes[1].duration_s == doctest::Approx(2.0));
    CHECK(r.song.duration_s == doctest::Approx(6.0));
}

TEST_CASE("musicxml: a metronome mark gives the tempo in quarter notes") {
    // Dotted quarter = 60 is 90 quarter notes a minute; half = 50 is 100.
    const std::string dotted =
        "<direction><direction-type><metronome><beat-unit>quarter</beat-unit><beat-unit-dot/>"
        "<per-minute>60</per-minute></metronome></direction-type></direction>";
    const std::string half = "<direction><direction-type><metronome><beat-unit>half</beat-unit>"
                             "<per-minute>50</per-minute></metronome></direction-type></direction>";
    const ScoreLoadResult a =
        load(piano_score(measure(1, std::string(kFourFour) + dotted + note("C4", 4))));
    REQUIRE(a.ok);
    CHECK(a.song.tempo.changes[0].us_per_quarter == us_of(90.0));
    const ScoreLoadResult b =
        load(piano_score(measure(1, std::string(kFourFour) + half + note("C4", 4))));
    REQUIRE(b.ok);
    CHECK(b.song.tempo.changes[0].us_per_quarter == us_of(100.0));
}

TEST_CASE("musicxml: a sound tempo beats the metronome mark in the same direction") {
    const std::string dir = "<direction><direction-type><metronome><beat-unit>quarter</beat-unit>"
                            "<per-minute>100</per-minute></metronome></direction-type><sound "
                            "tempo=\"80\"/></direction>";
    const ScoreLoadResult r =
        load(piano_score(measure(1, std::string(kFourFour) + dir + note("C4", 4))));
    REQUIRE(r.ok);
    REQUIRE(r.song.tempo.changes.size() == 1);
    CHECK(r.song.tempo.changes[0].us_per_quarter == us_of(80.0));
}

TEST_CASE("musicxml: a time signature change starts a new signature entry") {
    const std::string bars =
        measure(1, std::string(kFourFour) + note("C4", 4)) +
        measure(2,
                "<attributes><time><beats>3</beats><beat-type>4</beat-type></time></attributes>" +
                    note("C4", 3)) +
        measure(3, note("C4", 3));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.time_signatures.size() == 2);
    CHECK(r.song.time_signatures[1].numerator == 3);
    CHECK(r.song.time_signatures[1].tick == 4 * r.song.tempo.ticks_per_quarter);
    REQUIRE(r.song.measures.size() == 3);
    CHECK(r.song.measures[2].start_tick == 7 * r.song.tempo.ticks_per_quarter);
}

TEST_CASE("musicxml: a bar without rests is as long as its signature") {
    // The second bar holds one quarter note and no rest; the next bar still starts on beat 5.
    const std::string bars = measure(1, std::string(kFourFour) + note("C4", 4)) +
                             measure(2, note("D4", 1)) + measure(3, note("E4", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    CHECK(r.song.measures[2].start_tick == 8 * r.song.tempo.ticks_per_quarter);
}

// ---- Ties, hands, fingering

TEST_CASE("musicxml: a tie across a barline makes one note") {
    const std::string bars =
        measure(1,
                std::string(kFourFour) + rest(3) +
                    note("C4", 1,
                         "|<tie type=\"start\"/><notations><tied type=\"start\"/></notations>")) +
        measure(2,
                note("C4", 2, "|<tie type=\"stop\"/><notations><tied type=\"stop\"/></notations>") +
                    rest(2));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].start_s == doctest::Approx(1.5));
    CHECK(r.song.notes[0].duration_s == doctest::Approx(1.5)); // three beats at 120 BPM
    CHECK(r.song.notes[0].measure == 1);
    CHECK(r.stats.ties_merged == 1);
}

TEST_CASE("musicxml: a tie through a middle note joins all three") {
    const std::string start = "|<tie type=\"start\"/>";
    const std::string both = "|<tie type=\"stop\"/><tie type=\"start\"/>";
    const std::string stop = "|<tie type=\"stop\"/>";
    const std::string bars = measure(1, std::string(kFourFour) + note("D4", 4, start)) +
                             measure(2, note("D4", 4, both)) + measure(3, note("D4", 4, stop));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(6.0));
    CHECK(r.stats.ties_merged == 2);
}

TEST_CASE("musicxml: a tie stop with no start stays a note") {
    const ScoreLoadResult r = load(
        piano_score(measure(1, std::string(kFourFour) + note("D4", 4, "|<tie type=\"stop\"/>"))));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.stats.unmatched_ties == 1);
}

TEST_CASE("musicxml: a tie does not join a different pitch or a note that starts later") {
    const std::string bars =
        measure(1, std::string(kFourFour) + note("C4", 1, "|<tie type=\"start\"/>") + rest(1) +
                       note("C4", 1, "|<tie type=\"stop\"/>") + note("E4", 1));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 3); // the second C does not touch the first
}

TEST_CASE("musicxml: two staves are the right and left hand") {
    const std::string body = "<attributes><divisions>1</divisions><staves>2</staves>"
                             "<time><beats>4</beats><beat-type>4</beat-type></time></attributes>" +
                             note("E5", 4, "|<voice>1</voice><staff>1</staff>") +
                             "<backup><duration>4</duration></backup>" +
                             note("C3", 4, "|<voice>5</voice><staff>2</staff>");
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].pitch == 48);
    CHECK(r.song.notes[0].hand == Hand::Left);
    CHECK(r.song.notes[1].pitch == 76);
    CHECK(r.song.notes[1].hand == Hand::Right);
    REQUIRE(r.song.parts.size() == 2);
    CHECK(r.song.parts[0].hand == Hand::Right);
    CHECK(r.song.parts[1].hand == Hand::Left);
}

TEST_CASE("musicxml: a single staff takes its hand from the part name") {
    const std::string bars = measure(1, std::string(kFourFour) + note("C4", 4));
    CHECK(load(piano_score(bars, "Piano Right Hand")).song.notes[0].hand == Hand::Right);
    CHECK(load(piano_score(bars, "Piano (LH)")).song.notes[0].hand == Hand::Left);
    CHECK(load(piano_score(bars, "Piano")).song.notes[0].hand == Hand::Unknown);
}

TEST_CASE("musicxml: fingering reaches Note::finger") {
    const std::string tech =
        "|<notations><technical><fingering>3</fingering></technical></notations>";
    const std::string bad =
        "|<notations><technical><fingering>9</fingering></technical></notations>";
    const std::string body =
        std::string(kFourFour) + note("C4", 1, tech) + note("D4", 1, bad) + note("E4", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.notes[0].finger == 3);
    CHECK(r.song.notes[1].finger == 0);
    CHECK(r.song.notes[2].finger == 0);
}

// ---- Dynamics, transposition, ornaments

TEST_CASE("musicxml: dynamics marks set the velocity until the next mark") {
    auto mark = [](const char* m) {
        return std::string("<direction><direction-type><dynamics><") + m +
               "/></dynamics></direction-type></direction>";
    };
    const std::string body = std::string(kFourFour) + note("C4", 1) + mark("pp") + note("D4", 1) +
                             mark("ff") + note("E4", 1) + mark("mp") + note("F4", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 4);
    CHECK(r.song.notes[0].velocity == 80); // default mf
    CHECK(r.song.notes[1].velocity == 33);
    CHECK(r.song.notes[2].velocity == 112);
    CHECK(r.song.notes[3].velocity == 64);
}

TEST_CASE("musicxml: a note's own dynamics attribute wins") {
    const std::string n = "<note dynamics=\"50\"><pitch><step>C</step><octave>4</octave></pitch>"
                          "<duration>1</duration></note>";
    const ScoreLoadResult r = load(piano_score(measure(1, std::string(kFourFour) + n)));
    REQUIRE(r.ok);
    CHECK(r.song.notes[0].velocity == 45);
}

TEST_CASE("musicxml: a transposing instrument sounds at concert pitch") {
    // Clarinet in B-flat: written C5 sounds Bb4 (chromatic -2).
    const std::string body =
        "<attributes><divisions>1</divisions><transpose><diatonic>-1</diatonic>"
        "<chromatic>-2</chromatic></transpose></attributes>" +
        note("C5", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body), "Clarinet"));
    REQUIRE(r.ok);
    CHECK(r.song.notes[0].pitch == 70);
    // Alto sax (chromatic 3, octave-change -1): written C5 sounds Eb4.
    const std::string sax =
        "<attributes><divisions>1</divisions><transpose><chromatic>3</chromatic>"
        "<octave-change>-1</octave-change></transpose></attributes>" +
        note("C5", 1);
    const ScoreLoadResult s = load(piano_score(measure(1, sax)));
    REQUIRE(s.ok);
    CHECK(s.song.notes[0].pitch == 63);
}

TEST_CASE("musicxml: grace notes are dropped and take no time") {
    const std::string grace =
        "<note><grace slash=\"yes\"/><pitch><step>D</step><octave>5</octave></pitch></note>";
    const std::string body = std::string(kFourFour) + grace + note("C5", 2) + note("E5", 2);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].start_tick == 0);
    CHECK(r.song.notes[1].start_s == doctest::Approx(1.0));
    CHECK(r.stats.grace_notes_dropped == 1);
}

TEST_CASE("musicxml: cue notes and percussion take their time but give no note") {
    const std::string cue =
        "<note><cue/><pitch><step>D</step><octave>5</octave></pitch><duration>2</duration></note>";
    const std::string perc = "<note><unpitched><display-step>E</display-step><display-octave>4</"
                             "display-octave></unpitched><duration>1</duration></note>";
    const std::string body = std::string(kFourFour) + cue + perc + note("C5", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].start_s == doctest::Approx(1.5));
    CHECK(r.stats.cue_notes_dropped == 1);
    CHECK(r.stats.unpitched_dropped == 1);
}

TEST_CASE("musicxml: a repeated bar plays twice (details in musicxml_repeats_test.cpp)") {
    const std::string bars =
        measure(1, std::string(kFourFour) +
                       "<barline location=\"left\"><repeat direction=\"forward\"/></barline>" +
                       note("C4", 4) +
                       "<barline location=\"right\"><repeat direction=\"backward\"/></barline>") +
        measure(2, note("D4", 4));
    const ScoreLoadResult r = load(piano_score(bars));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 3);
    CHECK(r.stats.repeats_followed == 1);
    CHECK(r.stats.repeats_ignored == 0);
}

TEST_CASE("musicxml: pitches outside MIDI range are skipped and counted") {
    const std::string body = std::string(kFourFour) + note("A9", 1) + note("G9", 1);
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].pitch == 127);
    CHECK(r.stats.unreadable_notes == 1);
}

TEST_CASE("musicxml: the same pitch overlapping in two voices is cut, not stacked") {
    const std::string body = std::string(kFourFour) + note("C4", 4, "|<voice>1</voice>") +
                             "<backup><duration>4</duration></backup>" +
                             note("C4", 2, "|<voice>2</voice>");
    const ScoreLoadResult r = load(piano_score(measure(1, body)));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(2.0));
    CHECK(r.stats.duplicates_merged == 1);
}
