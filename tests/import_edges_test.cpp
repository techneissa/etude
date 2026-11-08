// SPDX-License-Identifier: GPL-3.0-or-later
// Import edge cases: ties across staves, short real notes, 16 and more parts, bar numbers that must
// match between MIDI and MusicXML, a missing <divisions>, many tracks and the note cap. The small
// files are in tests/data (edge-*, made by make-edge-cases.py, CC0).
#include "etude/hand_split.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include "smf_builder.hpp"
#include "timing_factor.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <ctime>
#include <fstream>
#include <iterator>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

using namespace etude;
using namespace etude::test;

namespace {

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

std::string read_text(const std::string& name) {
    const Bytes b = read_data(name);
    return std::string(b.begin(), b.end());
}

MidiLoadResult load_mid(const Bytes& bytes, const MidiLoadOptions& options = {}) {
    return load_midi(bytes.data(), bytes.size(), options);
}

ScoreLoadResult load_xml(const std::string& text) {
    return load_musicxml(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

bool has_word(const std::vector<std::string>& lines, const std::string& word) {
    return std::any_of(lines.begin(), lines.end(),
                       [&](const std::string& s) { return s.find(word) != std::string::npos; });
}

// `notes` notes of 480 ticks on one channel, each on its own pitch, then `ghosts` notes of
// `ghost_ticks` ticks.
Bytes notes_and_ghosts(int notes, int ghosts, int ghost_ticks) {
    Track t;
    for (int i = 0; i < notes; ++i) {
        t.on(0, 0, 40 + i % 40).off(480, 0, 40 + i % 40);
    }
    for (int i = 0; i < ghosts; ++i) {
        t.on(0, 0, 50 + i % 40).off(static_cast<std::uint32_t>(ghost_ticks), 0, 50 + i % 40);
    }
    return smf({t.end()});
}

} // namespace

// ---- Ties

TEST_CASE("import edges: a tie from one staff into the other is one held note (MusicXML)") {
    const Bytes file = read_data("edge-tie-cross-staff.musicxml");
    const ScoreLoadResult r = load_musicxml(file);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    const Note& held = r.song.notes[0];
    CHECK(held.pitch == 60);
    CHECK(held.start_tick == 0);
    CHECK(held.end_tick == 6 * r.song.tempo.ticks_per_quarter); // a whole note and a half
    CHECK(r.song.notes[1].pitch == 62);
    CHECK(r.stats.ties_merged == 1);
    CHECK(r.stats.unmatched_ties == 0);
}

TEST_CASE("import edges: a tie from one staff into the other is one held note (MuseScore)") {
    const Bytes file = read_data("edge-tie-cross-staff.mscx");
    const ScoreLoadResult r = load_mscx(file.data(), file.size());
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].pitch == 60);
    CHECK(r.song.notes[0].end_tick == 6 * r.song.tempo.ticks_per_quarter);
    CHECK(r.stats.ties_merged == 1);
    CHECK(r.stats.unmatched_ties == 0);
}

namespace {

std::string one_part_score(const std::string& bars) {
    return "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list><score-part "
           "id=\"P1\">"
           "<part-name>Piano</part-name></score-part></part-list><part id=\"P1\">" +
           bars + "</part></score-partwise>";
}

std::string c4(int duration, const std::string& extra, int staff, int voice) {
    return "<note><pitch><step>C</step><octave>4</octave></pitch><duration>" +
           std::to_string(duration) + "</duration>" + extra + "<voice>" + std::to_string(voice) +
           "</voice><staff>" + std::to_string(staff) + "</staff></note>";
}

const std::string kStart = "<tie type=\"start\"/>";
const std::string kStop = "<tie type=\"stop\"/>";
const std::string kBoth = kStop + kStart;
const std::string kTwoStaves =
    "<attributes><divisions>1</divisions><staves>2</staves><time><beats>4</beats>"
    "<beat-type>4</beat-type></time></attributes>";

} // namespace

TEST_CASE("import edges: a tie chain can change staff at every note") {
    const std::string text =
        one_part_score("<measure number=\"1\">" + kTwoStaves + c4(4, kStart, 1, 1) + "</measure>" +
                       "<measure number=\"2\">" + c4(4, kBoth, 2, 1) + "</measure>" +
                       "<measure number=\"3\">" + c4(4, kStop, 1, 1) + "</measure>");
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].end_tick == 12 * r.song.tempo.ticks_per_quarter);
    CHECK(r.stats.ties_merged == 2);
    CHECK(r.stats.unmatched_ties == 0);
}

TEST_CASE("import edges: two voices tying the same unison across a barline stay one note") {
    const std::string back = "<backup><duration>4</duration></backup>";
    const std::string text =
        one_part_score("<measure number=\"1\">" + kTwoStaves + c4(4, kStart, 1, 1) + back +
                       c4(4, kStart, 1, 2) + "</measure><measure number=\"2\">" +
                       c4(4, kStop, 1, 1) + back + c4(4, kStop, 1, 2) + "</measure>");
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].end_tick == 8 * r.song.tempo.ticks_per_quarter);
    CHECK(r.stats.unmatched_ties == 0);
}

TEST_CASE("import edges: a tie stop that does not play where the start ended is a new note") {
    // The stop is on the second beat of the next bar, not its first.
    const std::string text =
        one_part_score("<measure number=\"1\">" + kTwoStaves + c4(4, kStart, 1, 1) + "</measure>" +
                       "<measure number=\"2\"><forward><duration>1</duration></forward>" +
                       c4(3, kStop, 2, 1) + "</measure>");
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 2);
    CHECK(r.stats.unmatched_ties == 1);
}

namespace {

std::string note_xml(const std::string& step_octave_duration, const std::string& extra, int staff,
                     int voice) {
    return "<note><pitch><step>C</step><octave>4</octave></pitch><duration>" +
           step_octave_duration + "</duration>" + extra + "<voice>" + std::to_string(voice) +
           "</voice><staff>" + std::to_string(staff) + "</staff></note>";
}

std::string rest_xml(int duration, int staff, int voice) {
    return "<note><rest/><duration>" + std::to_string(duration) + "</duration><voice>" +
           std::to_string(voice) + "</voice><staff>" + std::to_string(staff) + "</staff></note>";
}

// One 4/4 bar, divisions 1, two staves, `body` inside.
std::string one_bar(const std::string& body) {
    return one_part_score("<measure number=\"1\">" + kTwoStaves + body + "</measure>");
}

const std::string kBack4 = "<backup><duration>4</duration></backup>";

void check_one_held_c4(const ScoreLoadResult& r) {
    REQUIRE(r.ok);
    std::size_t c4_notes = 0;
    for (const Note& n : r.song.notes) {
        if (n.pitch == 60) {
            ++c4_notes;
            CHECK(n.start_tick == 0);
            CHECK(n.end_tick == 2 * r.song.tempo.ticks_per_quarter);
        }
    }
    CHECK(c4_notes == 1);
    CHECK(r.stats.ties_merged == 1);
    CHECK(r.stats.unmatched_ties == 0);
}

} // namespace

TEST_CASE("import edges: a tie inside one bar joins whichever staff the file lists first") {
    SUBCASE("the start is on the lower staff, listed after the stop") {
        // Staff 1: a rest, then C4 (the stop) on beat 2. Staff 2: C4 (the start) on beat 1.
        check_one_held_c4(
            load_xml(one_bar(rest_xml(1, 1, 1) + note_xml("1", kStop, 1, 1) + rest_xml(2, 1, 1) +
                             kBack4 + note_xml("1", kStart, 2, 1) + rest_xml(3, 2, 1))));
    }
    SUBCASE("the start is on the upper staff, listed first") {
        check_one_held_c4(
            load_xml(one_bar(note_xml("1", kStart, 1, 1) + rest_xml(3, 1, 1) + kBack4 +
                             rest_xml(1, 2, 1) + note_xml("1", kStop, 2, 1) + rest_xml(2, 2, 1))));
    }
}

TEST_CASE("import edges: a tie inside one bar joins whichever voice the file lists first") {
    SUBCASE("voice 1 holds the stop, voice 2 the start") {
        check_one_held_c4(
            load_xml(one_bar(rest_xml(1, 1, 1) + note_xml("1", kStop, 1, 1) + rest_xml(2, 1, 1) +
                             kBack4 + note_xml("1", kStart, 1, 2) + rest_xml(3, 1, 2))));
    }
    SUBCASE("voice 1 holds the start, voice 2 the stop") {
        check_one_held_c4(
            load_xml(one_bar(note_xml("1", kStart, 1, 1) + rest_xml(3, 1, 1) + kBack4 +
                             rest_xml(1, 1, 2) + note_xml("1", kStop, 1, 2) + rest_xml(2, 1, 2))));
    }
}

TEST_CASE("import edges: two notes that touch with no tie stay two notes") {
    // The C4 on staff 2 at beat 2 has a stop but nothing starts a tie before it.
    const ScoreLoadResult r =
        load_xml(one_bar(note_xml("1", "", 1, 1) + rest_xml(3, 1, 1) + kBack4 + rest_xml(1, 2, 1) +
                         note_xml("1", kStop, 2, 1) + rest_xml(2, 2, 1)));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 2);
    CHECK(r.stats.ties_merged == 0);
    CHECK(r.stats.unmatched_ties == 1);
}

TEST_CASE("import edges: a MuseScore tie inside one bar joins whichever staff is listed first") {
    const std::string head =
        "<?xml version=\"1.0\"?><museScore version=\"3.02\"><Score><Division>480</Division><Part>"
        "<Staff id=\"1\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<Staff id=\"2\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>"
        "<trackName>Piano</trackName><Instrument id=\"piano\"><longName>Piano</longName>"
        "<Channel><program value=\"0\"/></Channel></Instrument></Part>";
    const std::string sig = "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";
    auto chord = [](const char* tie) {
        return std::string("<Chord><durationType>quarter</durationType><Note>") + tie +
               "<pitch>60</pitch><tpc>14</tpc></Note></Chord>";
    };
    auto rest = [](const char* type) {
        return std::string("<Rest><durationType>") + type + "</durationType></Rest>";
    };
    auto staff = [&](int id, const std::string& voice) {
        return "<Staff id=\"" + std::to_string(id) + "\"><Measure><voice>" + sig + voice +
               "</voice></Measure></Staff>";
    };
    auto check = [](const std::string& xml) {
        const Bytes b(xml.begin(), xml.end());
        check_one_held_c4(load_mscx(b.data(), b.size()));
    };
    SUBCASE("the start is on the lower staff, listed after the stop") {
        check(head + staff(1, rest("quarter") + chord("<tieBack/>") + rest("half")) +
              staff(2, chord("<tieFor/>") + rest("quarter") + rest("half")) +
              "</Score></museScore>");
    }
    SUBCASE("the start is on the upper staff, listed first") {
        check(head + staff(1, chord("<tieFor/>") + rest("quarter") + rest("half")) +
              staff(2, rest("quarter") + chord("<tieBack/>") + rest("half")) +
              "</Score></museScore>");
    }
}

TEST_CASE("import edges: a tie never joins a note of another part") {
    const std::string attrs =
        "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type>"
        "</time></attributes>";
    const std::string c4 = "<pitch><step>C</step><octave>4</octave></pitch>";
    const std::string text =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list>"
        "<score-part id=\"P1\"><part-name>One</part-name></score-part>"
        "<score-part id=\"P2\"><part-name>Two</part-name></score-part></part-list>"
        "<part id=\"P1\"><measure number=\"1\">" +
        attrs + "<note>" + c4 +
        "<duration>4</duration><tie type=\"start\"/></note></measure></part>"
        "<part id=\"P2\"><measure number=\"1\">" +
        attrs + "<note><rest/><duration>4</duration></note></measure><measure number=\"2\"><note>" +
        c4 + "<duration>4</duration><tie type=\"stop\"/></note></measure></part></score-partwise>";
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 2);
    CHECK(r.stats.ties_merged == 0);
    CHECK(r.stats.unmatched_ties == 1);
}

// ---- Short notes in a MIDI file

TEST_CASE("import edges: 32nd notes at 300 BPM are real notes and load") {
    const MidiLoadResult r = load_mid(read_data("edge-fast-32nds.mid"));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 8);
    CHECK(r.stats.ghost_notes_dropped == 0);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(0.025));
}

TEST_CASE("import edges: a note shorter than 30 ms and a 128th note is still a ghost") {
    // 480 ticks per quarter at 120 BPM: a 128th note is 15 ticks. 14 ticks is 14.6 ms.
    const MidiLoadResult r = load_mid(notes_and_ghosts(40, 1, 14));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 40);
    CHECK(r.stats.ghost_notes_dropped == 1);
    CHECK_FALSE(r.stats.ghost_filter_skipped);
}

TEST_CASE("import edges: a note of 30 ms or more that is not shorter than a 128th note stays") {
    // 16 ticks is 16.7 ms: under 30 ms, but longer than the 15.6 ms of a 128th note.
    const MidiLoadResult r = load_mid(notes_and_ghosts(40, 1, 16));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 41);
    CHECK(r.stats.ghost_notes_dropped == 0);
}

TEST_CASE("import edges: a note exactly a 128th note long is not a ghost") {
    // 15 ticks at 480 per quarter and 120 BPM: 15.6 ms, under 30 ms, but not shorter than a 128th.
    const MidiLoadResult r = load_mid(notes_and_ghosts(40, 1, 15));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 41);
    CHECK(r.stats.ghost_notes_dropped == 0);
}

TEST_CASE("import edges: just over a tenth ghosts keeps them all") {
    // 19 notes, 2 of them ghosts: 2 of 19 is over a tenth.
    const MidiLoadResult r = load_mid(notes_and_ghosts(17, 2, 10));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 19);
    CHECK(r.stats.ghost_filter_skipped);
}

TEST_CASE("import edges: a filter that would remove over a tenth of the notes keeps them all") {
    // 25 notes, 5 of them ghosts: a fifth.
    const MidiLoadResult r = load_mid(notes_and_ghosts(20, 5, 10));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 25);
    CHECK(r.stats.ghost_notes_dropped == 0);
    CHECK(r.stats.ghost_filter_skipped);
}

TEST_CASE("import edges: exactly a tenth ghosts are still dropped") {
    const MidiLoadResult r = load_mid(notes_and_ghosts(18, 2, 10));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 18);
    CHECK(r.stats.ghost_notes_dropped == 2);
}

TEST_CASE("import edges: drum hits are never ghost-filtered") {
    Track t;
    for (int i = 0; i < 30; ++i) {
        t.on(0, 0, 60 + i % 12).off(480, 0, 60 + i % 12);
    }
    t.on(0, 9, 42).off(5, 9, 42);
    const MidiLoadResult r = load_mid(smf({t.end()}));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 31);
}

TEST_CASE("import edges: a MIDI file with no notes fails to load with a message") {
    SUBCASE("tempo and signature only") {
        const MidiLoadResult r = load_mid(smf({Track().bpm(0, 90).time_sig(0, 3, 4).end()}));
        CHECK_FALSE(r.ok);
        CHECK(r.error != MidiFileError::None);
        CHECK(r.message.find("no notes") != std::string::npos);
    }
    SUBCASE("every note has zero length") {
        const MidiLoadResult r = load_mid(smf({Track().on(0, 0, 60).off(0, 0, 60).end()}));
        CHECK_FALSE(r.ok);
        CHECK(r.message.find("no notes") != std::string::npos);
    }
    SUBCASE("a song of ghost notes only keeps them rather than loading empty") {
        const MidiLoadResult r = load_mid(smf({Track().on(0, 0, 60).off(5, 0, 60).end()}));
        REQUIRE(r.ok);
        CHECK(r.song.notes.size() == 1);
    }
}

// ---- 16 and more parts

TEST_CASE("import edges: sixteen parts all keep their notes") {
    const ScoreLoadResult r = load_musicxml(read_data("edge-sixteen-parts.musicxml"));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 16);
    REQUIRE(r.song.parts.size() == 16);
    for (const Part& p : r.song.parts) {
        CHECK(p.note_count == 1);
    }
    CHECK(r.stats.duplicates_merged == 0);
}

TEST_CASE("import edges: a part's notes still merge when they repeat on the same pitch") {
    // Two voices of one staff writing the same C4 on beat 1 are one note, as in a MIDI file.
    std::string text = read_text("edge-sixteen-parts.musicxml");
    const std::size_t note_at = text.find("<note>");
    const std::size_t note_end = text.find("</note>", note_at) + 7;
    const std::string one = text.substr(note_at, note_end - note_at);
    text.insert(note_end, "<backup><duration>4</duration></backup>" + one);
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 16);
    CHECK(r.stats.duplicates_merged == 1);
}

// ---- Bar numbers

TEST_CASE("import edges: a full 3/4 bar before 4/4 is bar 1 in a MIDI file") {
    const MidiLoadResult r = load_mid(read_data("edge-3-4-then-4-4.mid"));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() == 3);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.measures[1].number == 2);
    CHECK(r.song.measures[2].number == 3);
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.notes[0].measure == 1);
    CHECK(r.song.notes[1].measure == 2);
    CHECK(r.song.notes[2].measure == 3);
}

TEST_CASE(
    "import edges: a first bar a signature change cuts short is a pickup, whatever the meter") {
    const MidiLoadResult r = load_mid(read_data("edge-sig-midbar.mid"));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() >= 2);
    // The first bar, cut at beat 3 by a 3/4 signature, is the pickup; the 3/4 bar is bar 1.
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.measures[0].start_tick == 0);
    CHECK(r.song.measures[1].number == 1);
    CHECK(r.song.measures[1].start_tick == 960);
    CHECK(r.song.measures[1].numerator == 3);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].measure == 0);
    CHECK(r.song.notes[1].measure == 1);
}

TEST_CASE("import edges: a signature change in the middle of a later bar starts a new bar there") {
    // The cut comes in bar 2: the bars before it keep their numbers.
    const MidiLoadResult r =
        load_mid(smf({Track().time_sig(0, 4, 4).time_sig(2880, 3, 4).end(),
                      Track().on(0, 0, 60).off(480, 0, 60).on(2400, 0, 62).off(480, 0, 62).end()}));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() >= 3);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.measures[1].number == 2);
    CHECK(r.song.measures[1].start_tick == 1920);
    CHECK(r.song.measures[2].number == 3);
    CHECK(r.song.measures[2].start_tick == 2880);
}

TEST_CASE("import edges: a bar before a mid-bar change keeps its number when the music goes on") {
    // Full 4/4 bar, then a 4/4 bar cut at beat 3 by 3/4: bars 1, 2, then 3 from the cut.
    const MidiLoadResult r =
        load_mid(smf({Track().time_sig(0, 4, 4).time_sig(2880, 3, 4).end(),
                      Track().on(0, 0, 60).off(480, 0, 60).on(2400, 0, 62).off(480, 0, 62).end()}));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() >= 3);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.measures[1].number == 2);
    CHECK(r.song.measures[2].number == 3);
    CHECK(r.song.measures[2].start_tick == 2880);
}

TEST_CASE(
    "import edges: a first bar exactly its signature long is bar 1, one tick short is a pickup") {
    SUBCASE("a full 4/4 bar, the signature restated at its end") {
        const MidiLoadResult r = load_mid(
            smf({Track().time_sig(0, 4, 4).time_sig(1920, 4, 4).end(),
                 Track().on(0, 0, 60).off(480, 0, 60).on(1440, 0, 62).off(480, 0, 62).end()}));
        REQUIRE(r.ok);
        CHECK(r.song.measures[0].number == 1);
        CHECK(r.song.notes[1].measure == 2);
    }
    SUBCASE("one tick short") {
        const MidiLoadResult r = load_mid(
            smf({Track().time_sig(0, 4, 4).time_sig(1919, 4, 4).end(),
                 Track().on(0, 0, 60).off(480, 0, 60).on(1440, 0, 62).off(480, 0, 62).end()}));
        REQUIRE(r.ok);
        CHECK(r.song.measures[0].number == 0);
        CHECK(r.song.measures[1].number == 1);
    }
}

TEST_CASE("import edges: a one-beat pickup in a MIDI file is bar 0") {
    const MidiLoadResult r = load_mid(read_data("edge-pickup.mid"));
    REQUIRE(r.ok);
    REQUIRE(r.song.measures.size() >= 2);
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.measures[1].number == 1);
}

namespace {

// The bars and the (start tick, pitch, bar) of every note, for comparing two loads of one tune.
struct Numbering {
    std::vector<std::pair<std::int64_t, int>> bars; // start tick, number
    std::vector<std::tuple<std::int64_t, int, int>> notes;
};

Numbering numbering_of(const Song& song) {
    Numbering n;
    for (const Measure& m : song.measures) {
        n.bars.emplace_back(m.start_tick, m.number);
    }
    for (const Note& note : song.notes) {
        n.notes.emplace_back(note.start_tick, note.pitch, note.measure);
    }
    return n;
}

} // namespace

TEST_CASE("import edges: MIDI and MusicXML number the same music alike") {
    SUBCASE("a full 3/4 bar, then 4/4") {
        const MidiLoadResult mid = load_mid(read_data("edge-3-4-then-4-4.mid"));
        const ScoreLoadResult xml = load_musicxml(read_data("edge-3-4-then-4-4.musicxml"));
        REQUIRE(mid.ok);
        REQUIRE(xml.ok);
        const Numbering a = numbering_of(mid.song);
        const Numbering b = numbering_of(xml.song);
        CHECK(a.bars == b.bars);
        CHECK(a.notes == b.notes);
    }
    SUBCASE("a one-beat pickup, then 4/4") {
        const MidiLoadResult mid = load_mid(read_data("edge-pickup.mid"));
        const ScoreLoadResult xml = load_musicxml(read_data("edge-pickup.musicxml"));
        REQUIRE(mid.ok);
        REQUIRE(xml.ok);
        const Numbering a = numbering_of(mid.song);
        const Numbering b = numbering_of(xml.song);
        CHECK(a.bars == b.bars);
        CHECK(a.notes == b.notes);
        CHECK(a.bars.front().second == 0);
    }
}

namespace {

// The same tune as MusicXML (divisions 1, so a unit is a quarter note at 480 ticks): `bars` are
// the <measure> elements.
std::string tune_xml(const std::string& bars) { return one_part_score(bars); }

std::string xml_note(const char* step, int octave, int units) {
    return std::string("<note><pitch><step>") + step + "</step><octave>" + std::to_string(octave) +
           "</octave></pitch><duration>" + std::to_string(units) + "</duration></note>";
}

const std::string kTime44 =
    "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
    "</attributes>";

void check_same_numbering(const MidiLoadResult& mid, const ScoreLoadResult& xml) {
    REQUIRE(mid.ok);
    REQUIRE(xml.ok);
    const Numbering a = numbering_of(mid.song);
    const Numbering b = numbering_of(xml.song);
    CHECK(a.bars == b.bars);
    CHECK(a.notes == b.notes);
}

} // namespace

TEST_CASE("import edges: MIDI and MusicXML number every pickup form the same") {
    SUBCASE("a two-beat pickup with the signature restated where it ends") {
        const MidiLoadResult mid =
            load_mid(smf({Track().time_sig(0, 4, 4).time_sig(960, 4, 4).end(), Track()
                                                                                   .on(0, 0, 67)
                                                                                   .off(960, 0, 67)
                                                                                   .on(0, 0, 60)
                                                                                   .off(1920, 0, 60)
                                                                                   .on(0, 0, 62)
                                                                                   .off(480, 0, 62)
                                                                                   .end()}));
        const ScoreLoadResult xml = load_xml(
            tune_xml("<measure number=\"0\" implicit=\"yes\">" + kTime44 + xml_note("G", 4, 2) +
                     "</measure>" + "<measure number=\"1\">" + xml_note("C", 4, 4) + "</measure>" +
                     "<measure number=\"2\">" + xml_note("D", 4, 1) + "</measure>"));
        check_same_numbering(mid, xml);
        REQUIRE(mid.ok);
        CHECK(mid.song.measures[0].number == 0);
    }
    SUBCASE("a two-beat pickup, then 3/4") {
        const MidiLoadResult mid =
            load_mid(smf({Track().time_sig(0, 4, 4).time_sig(960, 3, 4).end(), Track()
                                                                                   .on(0, 0, 67)
                                                                                   .off(960, 0, 67)
                                                                                   .on(0, 0, 60)
                                                                                   .off(1440, 0, 60)
                                                                                   .on(0, 0, 62)
                                                                                   .off(480, 0, 62)
                                                                                   .end()}));
        const ScoreLoadResult xml = load_xml(tune_xml(
            "<measure number=\"0\" implicit=\"yes\">" + kTime44 + xml_note("G", 4, 2) +
            "</measure>" +
            "<measure "
            "number=\"1\"><attributes><time><beats>3</beats><beat-type>4</beat-type></time>"
            "</attributes>" +
            xml_note("C", 4, 3) + "</measure>" + "<measure number=\"2\">" + xml_note("D", 4, 1) +
            "</measure>"));
        check_same_numbering(mid, xml);
        REQUIRE(mid.ok);
        CHECK(mid.song.measures[0].number == 0);
        CHECK(mid.song.measures[1].number == 1);
    }
    SUBCASE("a three-beat first bar in 3/4, then 4/4, is a whole bar in both") {
        const MidiLoadResult mid = load_mid(read_data("edge-3-4-then-4-4.mid"));
        const ScoreLoadResult xml = load_musicxml(read_data("edge-3-4-then-4-4.musicxml"));
        check_same_numbering(mid, xml);
        REQUIRE(mid.ok);
        CHECK(mid.song.measures[0].number == 1);
    }
    SUBCASE("a one-beat 1/4 pickup, then 4/4") {
        check_same_numbering(load_mid(read_data("edge-pickup.mid")),
                             load_musicxml(read_data("edge-pickup.musicxml")));
    }
    SUBCASE("a one-beat 1/4 first bar that the MusicXML file writes as a full bar") {
        const MidiLoadResult mid = load_mid(read_data("edge-pickup.mid"));
        const ScoreLoadResult xml = load_xml(tune_xml(
            "<measure number=\"1\"><attributes><divisions>1</divisions><time><beats>1</beats>"
            "<beat-type>4</beat-type></time></attributes>" +
            xml_note("G", 4, 1) +
            "</measure>"
            "<measure number=\"2\"><attributes><time><beats>4</beats><beat-type>4</beat-type>"
            "</time></attributes>" +
            xml_note("C", 4, 4) + "</measure><measure number=\"3\">" + xml_note("D", 4, 1) +
            "</measure>"));
        check_same_numbering(mid, xml);
    }
}

TEST_CASE("import edges: a MIDI pickup with no signature event at its end is bar 1") {
    // The file has one 4/4 signature and two beats of pickup: nothing in it says where the first
    // full bar starts, so the first bar is bar 1. A restated signature at beat 3 says it.
    const MidiLoadResult r =
        load_mid(smf({Track().time_sig(0, 4, 4).end(),
                      Track().on(0, 0, 67).off(960, 0, 67).on(0, 0, 60).off(1920, 0, 60).end()}));
    REQUIRE(r.ok);
    CHECK(r.song.measures[0].number == 1);
    CHECK(r.song.notes[1].measure == 1);
}

// ---- A missing <divisions>

TEST_CASE("import edges: a missing <divisions> is read from the first full bar, with a warning") {
    const ScoreLoadResult r = load_musicxml(read_data("edge-no-divisions.musicxml"));
    REQUIRE(r.ok);
    // Eight units fill a 4/4 bar, so a unit is an eighth note: 0.25 s at 120 BPM.
    REQUIRE(r.song.notes.size() == 9);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(0.25));
    CHECK(r.song.duration_s == doctest::Approx(4.0));
    CHECK(has_word(r.warnings, "divisions"));
}

TEST_CASE("import edges: the inferred divisions give the notes a file with divisions would") {
    std::string text = read_text("edge-no-divisions.musicxml");
    const ScoreLoadResult inferred = load_xml(text);
    text.replace(text.find("<attributes>"), 12, "<attributes><divisions>2</divisions>");
    const ScoreLoadResult explicit_divisions = load_xml(text);
    REQUIRE(inferred.ok);
    REQUIRE(explicit_divisions.ok);
    CHECK(explicit_divisions.warnings.empty());
    REQUIRE(inferred.song.notes.size() == explicit_divisions.song.notes.size());
    for (std::size_t i = 0; i < inferred.song.notes.size(); ++i) {
        CHECK(inferred.song.notes[i].start_s ==
              doctest::Approx(explicit_divisions.song.notes[i].start_s));
        CHECK(inferred.song.notes[i].duration_s ==
              doctest::Approx(explicit_divisions.song.notes[i].duration_s));
    }
}

TEST_CASE("import edges: a missing <divisions> that cannot be inferred falls back to 1") {
    // Three units in a 4/4 bar: no whole number of units per quarter note fits.
    const std::string text =
        R"(<?xml version="1.0"?><score-partwise version="3.1"><part-list><score-part id="P1"><part-name>Piano</part-name></score-part></part-list><part id="P1"><measure number="1"><attributes><time><beats>4</beats><beat-type>4</beat-type></time></attributes><note><pitch><step>C</step><octave>4</octave></pitch><duration>3</duration></note></measure></part></score-partwise>)";
    const ScoreLoadResult r = load_xml(text);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(1.5)); // three quarter notes
    CHECK(has_word(r.warnings, "divisions"));
}

namespace {

// A part with no <divisions>: each string is one bar, `time` the signature at its start (empty to
// keep the last), `notes` the durations of C4 notes in units.
struct NoDivBar {
    const char* time;
    std::vector<int> notes;
    bool implicit = false;
};

std::string no_divisions_score(const std::vector<NoDivBar>& bars) {
    std::string body;
    int number = 0;
    for (const NoDivBar& b : bars) {
        body += "<measure number=\"" + std::to_string(number++) + "\"" +
                (b.implicit ? " implicit=\"yes\"" : "") + ">";
        if (b.time[0] != '\0') {
            body += std::string("<attributes><time>") + b.time + "</time></attributes>";
        }
        for (int n : b.notes) {
            body += xml_note("C", 4, n);
        }
        body += "</measure>";
    }
    return one_part_score(body);
}

constexpr const char* k34 = "<beats>3</beats><beat-type>4</beat-type>";
constexpr const char* k44 = "<beats>4</beats><beat-type>4</beat-type>";

} // namespace

TEST_CASE("import edges: a missing <divisions> is read against each bar's own signature") {
    SUBCASE("3/4 then 4/4 at three units to the quarter note") {
        const ScoreLoadResult r = load_xml(no_divisions_score({{k34, {9}}, {k44, {12}}}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 2);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(1.5)); // three quarter notes
        CHECK(r.song.notes[1].duration_s == doctest::Approx(2.0)); // four
        CHECK(has_word(r.warnings, "3 to the quarter note"));
    }
    SUBCASE("triplet eighths: twelve units fill a 4/4 bar") {
        const ScoreLoadResult r =
            load_xml(no_divisions_score({{k44, {1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1}}, {"", {12}}}));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 13);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(1.0 / 6.0));
        CHECK(r.song.notes.back().duration_s == doctest::Approx(2.0));
    }
    SUBCASE("a short first bar that is not marked is a pickup, not the measure") {
        // One quarter of four units to the quarter, then a whole bar of sixteen.
        const ScoreLoadResult r = load_xml(no_divisions_score({{k44, {4}}, {"", {16}}}));
        REQUIRE(r.ok);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(0.5));
        CHECK(r.song.notes[1].duration_s == doctest::Approx(2.0));
    }
    SUBCASE("a pickup marked implicit does not count") {
        const ScoreLoadResult r =
            load_xml(no_divisions_score({{k44, {4}, true}, {"", {16}}, {"", {16}}}));
        REQUIRE(r.ok);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(0.5));
        CHECK(r.song.notes[1].duration_s == doctest::Approx(2.0));
    }
    SUBCASE("most whole bars wins") {
        // Three bars of eight units (two to the quarter) and one of twelve.
        const ScoreLoadResult r =
            load_xml(no_divisions_score({{k44, {8}}, {"", {12}}, {"", {8}}, {"", {8}}}));
        REQUIRE(r.ok);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(2.0));
        CHECK(has_word(r.warnings, "2 to the quarter note"));
    }
}

TEST_CASE("import edges: a file that sets <divisions> carries no warning") {
    const ScoreLoadResult r = load_musicxml(read_data("edge-tie-cross-staff.musicxml"));
    REQUIRE(r.ok);
    CHECK(r.warnings.empty());
}

// ---- Many tracks and the note cap

namespace {

Bytes many_one_note_tracks(int tracks) {
    Bytes out = header(1, tracks, 480);
    for (int i = 0; i < tracks; ++i) {
        put_chunk(out, "MTrk", Track().on(0, 0, 40 + i % 40).off(96, 0, 40 + i % 40).end().data());
    }
    return out;
}

} // namespace

TEST_CASE("import edges: a MIDI file with many tracks loads in time that scales with its size") {
    const int tracks = 65'535;
    const Bytes file = many_one_note_tracks(tracks);
    const std::clock_t start = std::clock();
    const MidiLoadResult r = load_mid(file);
    const double seconds = static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == static_cast<std::size_t>(tracks));
    CHECK(r.song.parts.size() == static_cast<std::size_t>(tracks));
    CHECK_TIMING(seconds < 8.0);
}

TEST_CASE("import edges: splitting hands over many parts takes time that scales with its size") {
    // 65,535 parts of one note each, built directly: a search of the part list for every note is
    // billions of steps.
    Song song;
    const int parts = 65'535;
    for (int i = 0; i < parts; ++i) {
        Note n;
        n.start_tick = std::int64_t{i} * 480;
        n.end_tick = n.start_tick + 240;
        n.pitch = static_cast<std::uint8_t>(40 + i % 40);
        n.track = static_cast<std::uint16_t>(i);
        n.channel = 0;
        song.notes.push_back(n);
        Part p;
        p.track = n.track;
        p.channel = 0;
        p.note_count = 1;
        song.parts.push_back(p);
    }
    const std::clock_t start = std::clock();
    const Song split = with_split_hands(song, {});
    const double seconds = static_cast<double>(std::clock() - start) / CLOCKS_PER_SEC;
    CHECK(split.notes.size() == song.notes.size());
    CHECK_TIMING(seconds < 8.0);
}

TEST_CASE("import edges: a MIDI song over the note cap fails with a message") {
    Track t;
    for (std::size_t i = 0; i < kMaxSongNotes + 1; ++i) {
        t.on(0, 0, 40 + static_cast<int>(i % 40)).off(96, 0, 40 + static_cast<int>(i % 40));
    }
    const MidiLoadResult r = load_mid(smf({t.end()}));
    CHECK_FALSE(r.ok);
    CHECK(r.error == MidiFileError::TooLarge);
    CHECK(r.message.find("notes") != std::string::npos);
    CHECK(r.song.notes.empty());
}

TEST_CASE("import edges: a MIDI song at the note cap loads") {
    Track t;
    for (std::size_t i = 0; i < kMaxSongNotes; ++i) {
        t.on(0, 0, 40 + static_cast<int>(i % 40)).off(96, 0, 40 + static_cast<int>(i % 40));
    }
    const MidiLoadResult r = load_mid(smf({t.end()}));
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == kMaxSongNotes);
}

TEST_CASE(
    "import edges: a MIDI song over the control-change and pitch-bend cap fails with a message") {
    // Half control changes, half pitch bends, one more than the cap.
    Track t;
    t.on(0, 0, 60).off(96, 0, 60);
    for (std::size_t i = 0; i < kMaxSongEvents + 1; ++i) {
        if (i % 2 == 0) {
            t.cc(0, 0, 1, static_cast<int>(i % 128));
        } else {
            t.raw(0, {0xE0, static_cast<std::uint8_t>(i % 128), 0x40});
        }
    }
    const MidiLoadResult r = load_mid(smf({t.end()}));
    CHECK_FALSE(r.ok);
    CHECK(r.error == MidiFileError::TooLarge);
    CHECK(r.message.find("control changes and pitch bends") != std::string::npos);
    CHECK(r.song.events.empty());
}

TEST_CASE("import edges: a MIDI song at the control-change and pitch-bend cap loads") {
    Track t;
    t.on(0, 0, 60).off(96, 0, 60);
    for (std::size_t i = 0; i < kMaxSongEvents; ++i) {
        if (i % 2 == 0) {
            t.cc(0, 0, 1, static_cast<int>(i % 128));
        } else {
            t.raw(0, {0xE0, static_cast<std::uint8_t>(i % 128), 0x40});
        }
    }
    const MidiLoadResult r = load_mid(smf({t.end()}));
    REQUIRE(r.ok);
    CHECK(r.song.events.size() == kMaxSongEvents);
}

TEST_CASE("import edges: a score over the note cap fails with a message") {
    std::string measure = "<measure number=\"1\"><attributes><divisions>1</divisions></attributes>";
    for (std::size_t i = 0; i < kMaxSongNotes + 1; ++i) {
        measure +=
            "<note><pitch><step>C</step><octave>4</octave></pitch><duration>1</duration></note>";
    }
    measure += "</measure>";
    const std::string text =
        "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list><score-part id=\"P1\">"
        "<part-name>Piano</part-name></score-part></part-list><part id=\"P1\">" +
        measure + "</part></score-partwise>";
    const ScoreLoadResult r = load_xml(text);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::TooLarge);
    CHECK(r.message.find("notes") != std::string::npos);
}

// ---- Messages

TEST_CASE("import edges: the SMPTE error names the problem once") {
    Bytes file = header(0, 1, 0xE728);
    put_chunk(file, "MTrk", Track().on(0, 0, 60).off(40, 0, 60).end().data());
    const MidiLoadResult r = load_mid(file);
    CHECK_FALSE(r.ok);
    CHECK(r.error == MidiFileError::SmpteDivision);
    const std::size_t first = r.message.find("SMPTE");
    REQUIRE(first != std::string::npos);
    CHECK(r.message.find("SMPTE", first + 1) == std::string::npos);
}
