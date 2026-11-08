// SPDX-License-Identifier: GPL-3.0-or-later
// musicxml: files (split from musicxml_test.cpp).
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

// ---- Parts and file structure

TEST_CASE("musicxml: part names, programs and two parts") {
    const std::string xml =
        "<score-partwise><work><work-title>Two Voices</work-title></work><part-list>"
        "<score-part id=\"P1\"><part-name>Flute</part-name><midi-instrument id=\"i1\">"
        "<midi-program>74</midi-program></midi-instrument></score-part>"
        "<score-part id=\"P2\"><part-name>Piano</part-name></score-part></part-list>"
        "<part id=\"P1\">" +
        measure(1, std::string(kFourFour) + note("C5", 4)) +
        "</part>"
        "<part id=\"P2\">" +
        measure(1, std::string(kFourFour) + note("C3", 4)) +
        "</part>"
        "</score-partwise>";
    const ScoreLoadResult r = load(xml);
    REQUIRE(r.ok);
    CHECK(r.title == "Two Voices");
    REQUIRE(r.song.parts.size() == 2);
    CHECK(r.song.parts[0].name == "Flute");
    CHECK(r.song.parts[0].program == 73);
    CHECK(r.song.parts[0].has_program);
    CHECK(r.song.parts[1].name == "Piano");
    CHECK(r.song.parts[0].channel != r.song.parts[1].channel);
    CHECK(r.song.parts[0].note_count == 1);
    bool program_event = false;
    for (const ChannelEvent& e : r.song.events) {
        program_event = program_event || (e.kind == ChannelEventKind::Program && e.value == 73);
    }
    CHECK(program_event);
    for (const Note& n : r.song.notes) {
        CHECK(n.channel != 9);
    }
}

TEST_CASE("musicxml: part names with line breaks are put on one line") {
    const ScoreLoadResult r =
        load(piano_score(measure(1, std::string(kFourFour) + note("C4", 4)), "Singstimme\nVoice"));
    REQUIRE(r.ok);
    CHECK(r.song.parts[0].name == "Singstimme Voice");
}

TEST_CASE("musicxml: a timewise score is converted") {
    const std::string xml =
        "<score-timewise><part-list><score-part id=\"P1\"><part-name>Piano</part-name>"
        "</score-part></part-list>"
        "<measure number=\"1\"><part id=\"P1\">" +
        std::string(kFourFour) + note("C4", 4) +
        "</part></measure><measure number=\"2\"><part id=\"P1\">" + note("D4", 4) +
        "</part></measure></score-timewise>";
    const ScoreLoadResult r = load(xml);
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[1].pitch == 62);
    CHECK(r.song.notes[1].start_s == doctest::Approx(2.0));
    CHECK(r.song.measures.size() == 2);
}

TEST_CASE("musicxml: a UTF-8 byte order mark and a doctype are fine") {
    std::string xml =
        "\xEF\xBB\xBF<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 Partwise//EN\" "
        "\"http://www.musicxml.org/dtds/partwise.dtd\">\n";
    xml += piano_score(measure(1, std::string(kFourFour) + note("C4", 4)));
    xml.erase(xml.find("<?xml", 10),
              xml.find("?>", xml.find("<?xml", 10)) + 3 - xml.find("<?xml", 10));
    const ScoreLoadResult r = load(xml);
    CHECK(r.ok);
}

// ---- Compressed files

namespace {

const char* kContainerXml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?><container><rootfiles>"
                            "<rootfile full-path=\"score/piece.xml\" "
                            "media-type=\"application/vnd.recordare.musicxml+xml\"/>"
                            "<rootfile full-path=\"other.xml\"/></rootfiles></container>";

std::string one_note_score() {
    return piano_score(measure(1, std::string(kFourFour) + note("C4", 4)));
}

} // namespace

TEST_CASE("mxl: the container names the score") {
    const Bytes zip =
        test::zip_of({test::stored_entry("mimetype", "application/vnd.recordare.musicxml"),
                      test::stored_entry("META-INF/container.xml", kContainerXml),
                      test::stored_entry("other.xml", "<nothing/>"),
                      test::stored_entry("score/piece.xml", one_note_score())});
    const ScoreLoadResult r = load_mxl(zip);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 1);
}

TEST_CASE("mxl: without a container the first .musicxml or .xml entry is the score") {
    const Bytes zip = test::zip_of({test::stored_entry("notes.txt", "hello"),
                                    test::stored_entry("META-INF/ignored.xml", "<x/>"),
                                    test::stored_entry("Piece.MusicXML", one_note_score())});
    const ScoreLoadResult r = load_mxl(zip);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 1);
}

TEST_CASE("mxl: a container that points at a missing file falls back to the first score entry") {
    const Bytes zip = test::zip_of({test::stored_entry("META-INF/container.xml", kContainerXml),
                                    test::stored_entry("real.musicxml", one_note_score())});
    CHECK(load_mxl(zip).ok);
}

TEST_CASE("mxl: a zip without a score is not a score") {
    const Bytes zip = test::zip_of({test::stored_entry("readme.txt", "no music here")});
    const ScoreLoadResult r = load_mxl(zip);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NotScore);
}

TEST_CASE("mxl: bytes that are not a zip are rejected") {
    const Bytes b = bytes_of("PK but then nothing that makes an archive");
    const ScoreLoadResult r = load_mxl(b);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::NotZip);
    CHECK_FALSE(load_mxl(nullptr, 0).ok);
}

TEST_CASE("zip: a bomb that declares more than the cap is refused before unpacking") {
    // 300 KB of deflate that would unpack to about 77 MB, over the 64 MiB entry cap.
    const std::size_t runs = 300'000;
    const Bytes zip = test::zip_of(
        {test::zeros_entry("score.musicxml", runs, static_cast<std::uint32_t>(1 + 258 * runs))});
    const ScoreLoadResult r = load_mxl(zip);
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::TooLarge);
}

TEST_CASE("zip: entries that together declare too much are refused") {
    std::vector<test::ZipEntry> entries;
    for (int i = 0; i < 5; ++i) {
        // Each declares 60 MiB, under the entry cap; five together are over the total cap.
        entries.push_back(
            test::zeros_entry("big" + std::to_string(i) + ".bin", 1, 60u * 1024 * 1024));
    }
    entries.push_back(test::stored_entry("score.musicxml", one_note_score()));
    const ScoreLoadResult r = load_mxl(test::zip_of(entries));
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::TooLarge);
}

TEST_CASE("zip: too many entries are refused") {
    std::vector<test::ZipEntry> entries;
    for (std::size_t i = 0; i < kMaxZipEntries + 1; ++i) {
        entries.push_back(test::stored_entry("f" + std::to_string(i), "x"));
    }
    const ScoreLoadResult r = load_mxl(test::zip_of(entries));
    CHECK_FALSE(r.ok);
    CHECK(r.error == ScoreFileError::TooLarge);
}

TEST_CASE("zip: an entry that unpacks to more than its header says is refused") {
    // Header declares 100 bytes, the deflate stream makes 259: the unpacker must stop at 100.
    const Bytes zip = test::zip_of({test::zeros_entry("score.musicxml", 1, 100)});
    const ScoreLoadResult r = load_mxl(zip);
    CHECK_FALSE(r.ok);
    CHECK((r.error == ScoreFileError::Corrupt || r.error == ScoreFileError::NotZip));
}

TEST_CASE("zip: a truncated archive never crashes") {
    const Bytes zip = test::zip_of({test::stored_entry("META-INF/container.xml", kContainerXml),
                                    test::stored_entry("score/piece.xml", one_note_score())});
    for (std::size_t cut = 0; cut < zip.size(); cut += 3) {
        const Bytes part(zip.begin(), zip.begin() + static_cast<std::ptrdiff_t>(cut));
        const ScoreLoadResult r = load_mxl(part);
        CHECK_FALSE(r.ok);
    }
}

// ---- MuseScore

namespace {

// A MuseScore 3/4 style score: one part with `staff_count` staves, the Staff bodies in `staves`
// (each the Measure elements of one staff).
std::string mscx_score(const std::vector<std::string>& staves, std::string_view version = "3.02",
                       std::string_view part_name = "Piano") {
    std::string out = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<museScore version=\"";
    out += version;
    out += "\"><programVersion>3.6.2</programVersion><Score><Division>480</Division>"
           "<metaTag name=\"workTitle\">A Title</metaTag><Part>";
    for (std::size_t i = 0; i < staves.size(); ++i) {
        out += "<Staff id=\"" + std::to_string(i + 1) +
               "\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>";
    }
    out += "<trackName>";
    out += part_name;
    out += "</trackName><Instrument id=\"piano\"><longName>Piano</longName>"
           "<Channel><program value=\"0\"/></Channel></Instrument></Part>";
    for (std::size_t i = 0; i < staves.size(); ++i) {
        out += "<Staff id=\"" + std::to_string(i + 1) + "\">" + staves[i] + "</Staff>";
    }
    out += "</Score></museScore>";
    return out;
}

std::string chord(std::string_view duration_type, int pitch, std::string_view extra_note = "",
                  std::string_view extra_chord = "") {
    return "<Chord>" + std::string(extra_chord) + "<durationType>" + std::string(duration_type) +
           "</durationType><Note>" + std::string(extra_note) + "<pitch>" + std::to_string(pitch) +
           "</pitch><tpc>14</tpc></Note></Chord>";
}

std::string mscx_rest(std::string_view duration_type) {
    return "<Rest><durationType>" + std::string(duration_type) + "</durationType></Rest>";
}

std::string ms_measure(std::string_view voice_body, std::string_view attributes = "") {
    return "<Measure" + std::string(attributes) + "><voice>" + std::string(voice_body) +
           "</voice></Measure>";
}

constexpr std::string_view kMsFourFour = "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>";

ScoreLoadResult load_ms(const std::string& xml) {
    const Bytes b = bytes_of(xml);
    return load_mscx(b.data(), b.size());
}

} // namespace

TEST_CASE("names: MuseScore's movement title and composer meta tags") {
    std::string xml = mscx_score({ms_measure(std::string(kMsFourFour) + chord("whole", 60))});
    const std::string with_tags =
        "<metaTag name=\"workTitle\"></metaTag><metaTag name=\"movementTitle\">Allegro</metaTag>"
        "<metaTag name=\"composer\">Muzio Clementi</metaTag>";
    const std::size_t at = xml.find("<metaTag name=\"workTitle\">A Title</metaTag>");
    REQUIRE(at != std::string::npos);
    xml.replace(at, std::string_view("<metaTag name=\"workTitle\">A Title</metaTag>").size(),
                with_tags);
    const ScoreLoadResult r = load_ms(xml);
    REQUIRE(r.ok);
    CHECK(r.title == "Allegro");
    CHECK(r.composer == "Muzio Clementi");
}

TEST_CASE("mscx: notes, lengths, dots and the tempo") {
    const std::string body =
        ms_measure(std::string(kMsFourFour) +
                   "<Tempo><tempo>1</tempo><followText>1</followText><text>q=60</text></Tempo>" +
                   chord("quarter", 60) + chord("eighth", 62, "", "<dots>1</dots>") +
                   chord("16th", 64) + mscx_rest("half"));
    const ScoreLoadResult r = load_ms(mscx_score({body}));
    REQUIRE(r.ok);
    CHECK(r.title == "A Title");
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.tempo.changes[0].us_per_quarter == 1'000'000); // 1 quarter note a second
    CHECK(r.song.notes[0].pitch == 60);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(1.0));
    CHECK(r.song.notes[1].start_s == doctest::Approx(1.0));
    CHECK(r.song.notes[1].duration_s == doctest::Approx(0.75)); // dotted eighth
    CHECK(r.song.notes[2].start_s == doctest::Approx(1.75));
    CHECK(r.song.notes[2].duration_s == doctest::Approx(0.25));
    CHECK(r.song.notes[0].velocity == 80);
    REQUIRE(r.song.time_signatures.size() == 1);
    CHECK(r.song.time_signatures[0].numerator == 4);
}

TEST_CASE("mscx: chords and a tuplet") {
    const std::string tuplet = "<Tuplet><normalNotes>2</normalNotes><actualNotes>3</actualNotes>"
                               "<baseNote>eighth</baseNote></Tuplet>" +
                               chord("eighth", 60) + chord("eighth", 62) + chord("eighth", 64) +
                               "<endTuplet/>";
    const std::string two_note_chord =
        "<Chord><durationType>half</durationType><Note><pitch>48</pitch></Note>"
        "<Note><pitch>55</pitch></Note></Chord>";
    const ScoreLoadResult r = load_ms(mscx_score({ms_measure(
        std::string(kMsFourFour) + tuplet + mscx_rest("eighth") + mscx_rest("quarter"))}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 3);
    CHECK(r.song.notes[1].start_s == doctest::Approx(1.0 / 6.0).epsilon(0.001));
    CHECK(r.song.notes[2].start_s == doctest::Approx(1.0 / 3.0).epsilon(0.001));
    CHECK(r.song.notes[2].duration_s == doctest::Approx(1.0 / 6.0).epsilon(0.001));

    const ScoreLoadResult c =
        load_ms(mscx_score({ms_measure(std::string(kMsFourFour) + two_note_chord)}));
    REQUIRE(c.ok);
    REQUIRE(c.song.notes.size() == 2);
    CHECK(c.song.notes[0].start_tick == c.song.notes[1].start_tick);
    CHECK(c.song.notes[0].duration_s == doctest::Approx(1.0));
}

TEST_CASE("mscx: a tie across a barline makes one note") {
    const std::string tie_start =
        "<Spanner "
        "type=\"Tie\"><Tie/><next><location><fractions>1/4</fractions></location></next></Spanner>";
    const std::string tie_stop =
        "<Spanner "
        "type=\"Tie\"><prev><location><fractions>-1/4</fractions></location></prev></Spanner>";
    const ScoreLoadResult r = load_ms(mscx_score(
        {ms_measure(std::string(kMsFourFour) + mscx_rest("half") + mscx_rest("quarter") +
                    chord("quarter", 67, tie_start)) +
         ms_measure(chord("quarter", 67, tie_stop) + mscx_rest("half") + mscx_rest("quarter"))}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 1);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(1.0)); // two quarters at 120 BPM
    CHECK(r.stats.ties_merged == 1);
}

TEST_CASE("mscx: two staves in a part are the right and left hand; fingering and dynamics") {
    const std::string right =
        ms_measure(std::string(kMsFourFour) + "<Dynamic><subtype>pp</subtype></Dynamic>" +
                   chord("whole", 72, "<Fingering><text>2</text></Fingering>"));
    const std::string left = ms_measure(std::string(kMsFourFour) + chord("whole", 48));
    const ScoreLoadResult r = load_ms(mscx_score({right, left}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.song.notes[0].pitch == 48);
    CHECK(r.song.notes[0].hand == Hand::Left);
    CHECK(r.song.notes[1].hand == Hand::Right);
    CHECK(r.song.notes[1].finger == 2);
    CHECK(r.song.notes[1].velocity == 33);
    CHECK(r.song.notes[0].velocity == 33); // a dynamic holds for the whole part, both staves
}

TEST_CASE("mscx: grace notes are dropped, a short bar with len is a pickup") {
    const std::string pickup =
        ms_measure(std::string(kMsFourFour) + chord("quarter", 67), " len=\"1/4\"");
    const std::string grace = chord("eighth", 72, "", "<acciaccatura/>");
    const std::string full = ms_measure(grace + chord("whole", 72));
    const ScoreLoadResult r = load_ms(mscx_score({pickup + full}));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 2);
    CHECK(r.stats.grace_notes_dropped == 1);
    CHECK(r.song.measures[0].number == 0);
    CHECK(r.song.notes[1].start_s == doctest::Approx(0.5));
    CHECK(r.song.notes[1].measure == 1);
}

TEST_CASE("mscz: a zip with an embedded .mscx loads") {
    const std::string xml = mscx_score({ms_measure(std::string(kMsFourFour) + chord("whole", 60))});
    const Bytes zip = test::zip_of(
        {test::stored_entry(
             "META-INF/container.xml",
             "<container><rootfiles><rootfile full-path=\"song.mscx\"/></rootfiles></container>"),
         test::stored_entry("Thumbnails/thumbnail.png", "not a png"),
         test::stored_entry("song.mscx", xml)});
    const ScoreLoadResult r = load_mscz(zip);
    REQUIRE(r.ok);
    CHECK(r.song.notes.size() == 1);
    const ScoreLoadResult any = load_score(zip);
    REQUIRE(any.ok);
    CHECK(any.song.notes.size() == 1);
    // The MusicXML loader does not take a MuseScore zip.
    CHECK_FALSE(load_mxl(zip).ok);
}

TEST_CASE("mscz: MuseScore 4 lists its style file before the score in container.xml") {
    const std::string xml =
        mscx_score({ms_measure(std::string(kMsFourFour) + chord("whole", 60))}, "4.70");
    const Bytes zip =
        test::zip_of({test::stored_entry("score_style.mss", "<museScore/>"),
                      test::stored_entry("piece.mscx", xml),
                      test::stored_entry(
                          "META-INF/container.xml",
                          "<container><rootfiles><rootfile full-path=\"score_style.mss\"/>"
                          "<rootfile full-path=\"piece.mscx\"/>"
                          "<rootfile full-path=\"audiosettings.json\"/></rootfiles></container>")});
    CHECK(load_mscz(zip).ok);
    const ScoreLoadResult any = load_score(zip);
    CHECK(any.ok);
    CHECK_FALSE(load_mxl(zip).ok);
}

TEST_CASE("mscz: what cannot be read says to export MusicXML") {
    // A MuseScore 2 file, a score without staves, and an archive without a score.
    const ScoreLoadResult old = load_ms(mscx_score({ms_measure(chord("whole", 60))}, "2.06"));
    CHECK_FALSE(old.ok);
    CHECK(old.error == ScoreFileError::Unsupported);
    CHECK(old.message.find("MusicXML") != std::string::npos);
    const ScoreLoadResult empty =
        load_ms("<museScore version=\"4.20\"><Score><Division>480</Division></Score></museScore>");
    CHECK_FALSE(empty.ok);
    CHECK(empty.error == ScoreFileError::Unsupported);
    CHECK(empty.message.find("MusicXML") != std::string::npos);
    const Bytes no_score = test::zip_of({test::stored_entry("readme.txt", "hi")});
    const ScoreLoadResult z = load_mscz(no_score);
    CHECK_FALSE(z.ok);
    CHECK(z.message.find("MusicXML") != std::string::npos);
    const ScoreLoadResult wrong = load_ms("<svg/>");
    CHECK_FALSE(wrong.ok);
    CHECK(wrong.error == ScoreFileError::NotScore);
}

TEST_CASE("score: load_score picks the reader from the bytes") {
    const Bytes xml = bytes_of(one_note_score());
    CHECK(sniff_song_format(xml) == SongFileFormat::Xml);
    CHECK(load_score(xml).ok);
    const Bytes ms =
        bytes_of(mscx_score({ms_measure(std::string(kMsFourFour) + chord("whole", 60))}));
    CHECK(load_score(ms).ok); // a bare .mscx
    const Bytes midi = bytes_of(std::string("MThd\0\0\0\6", 8));
    CHECK(sniff_song_format(midi) == SongFileFormat::Midi);
    CHECK_FALSE(load_score(midi).ok);
    CHECK(sniff_song_format(bytes_of("  \n <a/>")) == SongFileFormat::Xml);
    CHECK(sniff_song_format(bytes_of("hello world")) == SongFileFormat::Unknown);
    CHECK_FALSE(load_score(bytes_of("hello world")).ok);
    CHECK_FALSE(load_score(nullptr, 0).ok);
}

// ---- Library

namespace {

SongEntry library_entry(const std::string& text, std::string_view file_name, bool* ok = nullptr) {
    const Bytes b = bytes_of(text);
    const LibraryLoad r = load_library_entry(b, file_name);
    if (ok != nullptr) {
        *ok = r.ok;
    }
    return r.entry;
}

} // namespace

TEST_CASE("library: a MusicXML file becomes an entry with hands known") {
    const std::string body = "<attributes><divisions>1</divisions><staves>2</staves>"
                             "<time><beats>4</beats><beat-type>4</beat-type></time></attributes>" +
                             note("E5", 4, "|<staff>1</staff>") +
                             "<backup><duration>4</duration></backup>" +
                             note("C3", 4, "|<staff>2</staff>");
    bool ok = false;
    const SongEntry e = library_entry(piano_score(measure(1, body)), "fur-elise.musicxml", &ok);
    REQUIRE(ok);
    CHECK(e.title == "Fur Elise");
    CHECK(e.note_count == 2);
    CHECK(e.left_notes == 1);
    CHECK(e.right_notes == 1);
    CHECK(e.hands_known());
    CHECK(e.lowest_pitch == 48);
    CHECK(e.highest_pitch == 76);
}

TEST_CASE("library: the score's title wins over the file name") {
    const std::string xml =
        "<score-partwise><work><work-title>Sonatina in C</work-title></work><part-list>"
        "<score-part id=\"P1\"><part-name>Piano</part-name></score-part></part-list>"
        "<part id=\"P1\">" +
        measure(1, std::string(kFourFour) + note("C4", 4)) + "</part></score-partwise>";
    CHECK(library_entry(xml, "untitled.xml").title == "Sonatina in C");
    CHECK(library_entry(xml, "My Song.xml").title == "Sonatina in C");
}

TEST_CASE("library: the loader goes by content, not by extension") {
    // MusicXML named .mid, a zip named .xml, and a MIDI file named .musicxml all load.
    bool ok = false;
    library_entry(one_note_score(), "wrong-name.mid", &ok);
    CHECK(ok);
    const Bytes zip = test::zip_of({test::stored_entry("score.musicxml", one_note_score())});
    CHECK(load_library_entry(zip, "wrong-name.xml").ok);
    const Bytes midi = {'M', 'T', 'h', 'd',  0,   0,   0, 6,    0,    0,  0, 1,
                        0,   96,  'M', 'T',  'r', 'k', 0, 0,    0,    12, 0, 0x90,
                        60,  64,  96,  0x80, 60,  0,   0, 0xFF, 0x2F, 0};
    const LibraryLoad m = load_library_entry(midi, "x.musicxml");
    CHECK(m.ok);
    CHECK(m.entry.note_count == 1);
}

TEST_CASE("library: a broken score reports why") {
    const LibraryLoad r = load_library_entry(bytes_of("<svg/>"), "a.xml");
    CHECK_FALSE(r.ok);
    CHECK_FALSE(r.message.empty());
    CHECK(r.score_error == ScoreFileError::NotScore);
    const LibraryLoad j = load_library_entry(bytes_of("junk"), "a.mid");
    CHECK_FALSE(j.ok);
    CHECK(j.error == MidiFileError::NotMidi);
}

TEST_CASE("names: MusicXML's credits stand in for a missing title, the composer is read") {
    const std::string part = "<part-list><score-part id=\"P1\"><part-name>Piano</part-name>"
                             "</score-part></part-list><part id=\"P1\">" +
                             measure(1, std::string(kFourFour) + note("C4", 4)) + "</part>";
    // The creator of type composer, and the title credit for a score with no work title.
    const std::string credited =
        "<score-partwise><identification><creator type=\"lyricist\">Someone</creator>"
        "<creator type=\"composer\">Erik  Satie</creator></identification>"
        "<credit page=\"1\"><credit-type>composer</credit-type><credit-words>E. S.</credit-words>"
        "</credit><credit page=\"1\"><credit-type>title</credit-type>"
        "<credit-words>Gnossienne</credit-words><credit-words>No. 1</credit-words></credit>" +
        part + "</score-partwise>";
    const ScoreLoadResult r = load_score(bytes_of(credited));
    REQUIRE(r.ok);
    CHECK(r.title == "Gnossienne No. 1");
    CHECK(r.composer == "Erik Satie");
    CHECK(library_entry(credited, "x.musicxml").composer == "Erik Satie");
    CHECK(library_entry(credited, "x.musicxml").title == "Gnossienne No. 1");
    // No creator: the composer credit; no typed title credit: the first credit with no type.
    const std::string untyped =
        "<score-partwise><credit page=\"1\"><credit-words>Arabesque</credit-words></credit>"
        "<credit page=\"1\"><credit-type>composer</credit-type>"
        "<credit-words>Claude Debussy</credit-words></credit>" +
        part + "</score-partwise>";
    const ScoreLoadResult u = load_score(bytes_of(untyped));
    REQUIRE(u.ok);
    CHECK(u.title == "Arabesque");
    CHECK(u.composer == "Claude Debussy");
    // A work title wins over the credits.
    const std::string worked = "<score-partwise><work><work-title>Sonatina</work-title></work>"
                               "<credit page=\"1\"><credit-words>Other</credit-words></credit>" +
                               part + "</score-partwise>";
    CHECK(load_score(bytes_of(worked)).title == "Sonatina");
    CHECK(load_score(bytes_of(worked)).composer.empty());
}
