// SPDX-License-Identifier: GPL-3.0-or-later
// MusicXML, compressed MusicXML (.mxl) and MuseScore (.mscz) scores: notes, times, bars, ties,
// concert pitch, the containers, hostile files, and one real piece in every format.
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"

#include "etude/song_builder.hpp"
#include "zip_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

using namespace etude;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes bytes_of(std::string_view text) { return Bytes(text.begin(), text.end()); }

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

// A partwise score with one piano part around `measures`.
std::string piano_score(std::string_view measures, std::string_view part_name = "Piano") {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\"><part-list>"
           "<score-part id=\"P1\"><part-name>" +
           std::string(part_name) + "</part-name></score-part></part-list><part id=\"P1\">" +
           std::string(measures) + "</part></score-partwise>";
}

ScoreLoadResult load(const std::string& xml) { return load_musicxml(bytes_of(xml)); }

// <note> for a pitch such as "C4" or "F#5" (b for flat), `duration` in divisions. `extra` is
// "before the pitch|after the duration".
std::string note(std::string_view name, int duration, std::string_view extra = "") {
    const bool accidental = name[1] == '#' || name[1] == 'b';
    std::string out = "<note>" + std::string(extra.substr(0, extra.find('|'))) + "<pitch><step>";
    out += name[0];
    out += "</step>";
    out += name[1] == '#' ? "<alter>1</alter>" : name[1] == 'b' ? "<alter>-1</alter>" : "";
    out += "<octave>";
    out += name[accidental ? 2 : 1];
    out += "</octave></pitch><duration>" + std::to_string(duration) + "</duration>";
    if (extra.find('|') != std::string_view::npos) {
        out += extra.substr(extra.find('|') + 1);
    }
    return out + "</note>";
}

std::string rest(int duration) {
    return "<note><rest/><duration>" + std::to_string(duration) + "</duration></note>";
}

constexpr std::string_view kFourFour =
    "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
    "</attributes>";

std::string measure(int number, std::string_view body) {
    return "<measure number=\"" + std::to_string(number) + "\">" + std::string(body) + "</measure>";
}

std::string tie(std::string_view kinds) {
    std::string out;
    for (std::string_view kind : {"start", "stop"}) {
        if (kinds.find(kind) != std::string_view::npos) {
            out += "<tie type=\"" + std::string(kind) + "\"/>";
        }
    }
    return out;
}

const std::string k4 = std::string(kFourFour);

} // namespace

TEST_CASE("musicxml: pitch spelling, divisions, chords, backup and forward place the notes") {
    SUBCASE("A0, C4, F#5 and Bb3 are 21, 60, 78 and 58") {
        const ScoreLoadResult r = load(piano_score(
            measure(1, k4 + note("A0", 1) + note("C4", 1) + note("F#5", 1) + note("Bb3", 1))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 4);
        CHECK(r.song.notes[0].pitch == 21);
        CHECK(r.song.notes[1].pitch == 60);
        CHECK(r.song.notes[2].pitch == 78);
        CHECK(r.song.notes[3].pitch == 58);
    }
    SUBCASE("divisions scale lengths: with 4, a duration of 6 is a dotted quarter, 0.75 s") {
        const ScoreLoadResult r = load(piano_score(measure(
            1, "<attributes><divisions>4</divisions><time><beats>4</beats><beat-type>4</beat-type>"
               "</time></attributes>" +
                   note("C4", 6) + note("D4", 2))));
        REQUIRE(r.ok);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(0.75));
        CHECK(r.song.notes[1].start_s == doctest::Approx(0.75));
        CHECK(r.song.notes[1].duration_s == doctest::Approx(0.25));
    }
    SUBCASE("a chord starts together and takes no time; forward skips without a rest") {
        const ScoreLoadResult r = load(piano_score(
            measure(1, k4 + note("C4", 1) + note("E4", 1, "<chord/>|") + note("D4", 1) +
                           "<forward><duration>1</duration></forward>" + note("F4", 1))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 4);
        CHECK(r.song.notes[0].start_tick == r.song.notes[1].start_tick);
        CHECK(r.song.notes[2].start_s == doctest::Approx(0.5));
        CHECK(r.song.notes[3].start_s == doctest::Approx(1.5));
    }
    SUBCASE("backup puts a second voice over the first") {
        const ScoreLoadResult r = load(piano_score(measure(
            1, k4 + note("E5", 2, "|<voice>1</voice>") + note("D5", 2, "|<voice>1</voice>") +
                   "<backup><duration>4</duration></backup>" +
                   note("C4", 4, "|<voice>2</voice>"))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 3);
        CHECK(r.song.notes[0].pitch == 60);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(2.0));
        CHECK(r.song.notes[2].pitch == 74);
        CHECK(r.song.notes[2].start_s == doctest::Approx(1.0));
    }
    SUBCASE("the same pitch overlapping in two voices is cut, not stacked") {
        const ScoreLoadResult r = load(piano_score(measure(
            1, k4 + note("C4", 4, "|<voice>1</voice>") + "<backup><duration>4</duration></backup>" +
                   note("C4", 2, "|<voice>2</voice>"))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 1);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(2.0));
    }
    SUBCASE(
        "grace notes take no time; cue notes and percussion take their time and sound nothing") {
        const std::string grace =
            "<note><grace slash=\"yes\"/><pitch><step>D</step><octave>5</octave></pitch></note>";
        const std::string cue = "<note><cue/><pitch><step>D</step><octave>5</octave></pitch>"
                                "<duration>2</duration></note>";
        const std::string perc = "<note><unpitched><display-step>E</display-step><display-octave>4"
                                 "</display-octave></unpitched><duration>1</duration></note>";
        const ScoreLoadResult r =
            load(piano_score(measure(1, k4 + grace + note("C5", 1) + cue + perc + note("E5", 1))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 2);
        CHECK(r.song.notes[0].start_tick == 0);
        CHECK(r.song.notes[1].start_s == doctest::Approx(2.0));
    }
}

TEST_CASE("musicxml: a transposing instrument sounds at concert pitch") {
    // Clarinet in B-flat: written C5 sounds Bb4 (chromatic -2). Alto sax: chromatic 3 with an
    // octave down, so C5 sounds Eb4.
    const ScoreLoadResult clarinet = load(piano_score(
        measure(1, "<attributes><divisions>1</divisions><transpose><diatonic>-1</diatonic>"
                   "<chromatic>-2</chromatic></transpose></attributes>" +
                       note("C5", 1)),
        "Clarinet"));
    REQUIRE(clarinet.ok);
    CHECK(clarinet.song.notes[0].pitch == 70);
    const ScoreLoadResult sax = load(piano_score(
        measure(1, "<attributes><divisions>1</divisions><transpose><chromatic>3</chromatic>"
                   "<octave-change>-1</octave-change></transpose></attributes>" +
                       note("C5", 1))));
    REQUIRE(sax.ok);
    CHECK(sax.song.notes[0].pitch == 63);
}

TEST_CASE("musicxml: tempo marks move the note times") {
    const std::string slow = "<direction><sound tempo=\"60\"/></direction>";
    const std::string fast = "<direction><sound tempo=\"120\"/></direction>";
    const ScoreLoadResult r =
        load(piano_score(measure(1, k4 + slow + note("C4", 4)) + measure(2, fast + note("D4", 4))));
    REQUIRE(r.ok);
    REQUIRE(r.song.tempo.changes.size() == 2);
    CHECK(r.song.tempo.changes[1].tick == 4 * r.song.tempo.ticks_per_quarter);
    // Bar 1 is four beats at 60 BPM = 4 s, then bar 2 at 120 BPM.
    CHECK(r.song.notes[1].start_s == doctest::Approx(4.0));
    CHECK(r.song.notes[1].duration_s == doctest::Approx(2.0));

    // A metronome mark counts in quarter notes: dotted quarter = 60 is 90.
    const std::string dotted =
        "<direction><direction-type><metronome><beat-unit>quarter</beat-unit><beat-unit-dot/>"
        "<per-minute>60</per-minute></metronome></direction-type></direction>";
    const ScoreLoadResult m = load(piano_score(measure(1, k4 + dotted + note("C4", 4))));
    REQUIRE(m.ok);
    CHECK(m.song.tempo.changes[0].us_per_quarter == us_per_quarter_from_bpm(90.0));
}

TEST_CASE("musicxml: bars are numbered from 1, a short first bar is the pickup, 0") {
    SUBCASE("bars and a time signature change place the bar starts") {
        const ScoreLoadResult r = load(piano_score(
            measure(1, k4 + note("C4", 4)) +
            measure(2, "<attributes><time><beats>3</beats><beat-type>4</beat-type></time>"
                       "</attributes>" +
                           note("C4", 3)) +
            measure(3, note("C4", 3)) + measure(4, note("D4", 1)) + measure(5, note("E4", 3))));
        REQUIRE(r.ok);
        REQUIRE(r.song.time_signatures.size() == 2);
        CHECK(r.song.time_signatures[1].tick == 4 * r.song.tempo.ticks_per_quarter);
        REQUIRE(r.song.measures.size() == 5);
        CHECK(r.song.measures[0].number == 1);
        CHECK(r.song.measures[2].start_tick == 7 * r.song.tempo.ticks_per_quarter);
        // Bar 4 holds one quarter note and no rest: bar 5 still starts a whole bar later.
        CHECK(r.song.measures[4].start_tick == 13 * r.song.tempo.ticks_per_quarter);
        CHECK(r.song.notes[4].measure == 5);
    }
    SUBCASE("a first bar marked implicit, numbered 0, or just short") {
        const std::string implicit = "<measure number=\"0\" implicit=\"yes\">" + k4 +
                                     note("G4", 1) + "</measure>" + measure(1, note("C5", 4));
        const std::string numbered_zero = "<measure number=\"0\">" + k4 + note("G4", 4) +
                                          "</measure>" + measure(1, note("C5", 4));
        const std::string short_bar =
            measure(1, k4 + note("G4", 1)) + measure(2, note("C5", 4)) + measure(3, note("C5", 4));
        for (const std::string& bars : {implicit, numbered_zero, short_bar}) {
            const ScoreLoadResult r = load(piano_score(bars));
            REQUIRE(r.ok);
            CHECK(r.song.measures[0].number == 0);
            CHECK(r.song.measures[1].number == 1);
            CHECK(r.song.notes[0].measure == 0);
            CHECK(r.song.notes[1].measure == 1);
        }
        const ScoreLoadResult r = load(piano_score(implicit));
        CHECK(r.song.measures[1].start_tick == r.song.tempo.ticks_per_quarter); // a one-beat pickup
        CHECK(r.song.notes[1].start_s == doctest::Approx(0.5));
    }
}

TEST_CASE("musicxml: ties make one note, and only where the notes meet") {
    const std::string start = "|" + tie("start");
    const std::string stop = "|" + tie("stop");
    const std::string both = "|" + tie("startstop");
    SUBCASE("across a barline") {
        const ScoreLoadResult r = load(piano_score(measure(1, k4 + rest(3) + note("C4", 1, start)) +
                                                   measure(2, note("C4", 2, stop) + rest(2))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 1);
        CHECK(r.song.notes[0].start_s == doctest::Approx(1.5));
        CHECK(r.song.notes[0].duration_s == doctest::Approx(1.5));
    }
    SUBCASE("through a middle note") {
        const ScoreLoadResult r =
            load(piano_score(measure(1, k4 + note("D4", 4, start)) +
                             measure(2, note("D4", 4, both)) + measure(3, note("D4", 4, stop))));
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == 1);
        CHECK(r.song.notes[0].duration_s == doctest::Approx(6.0));
    }
    SUBCASE("a stop with no start, or a stop that comes later than the start ends, stays a note") {
        const ScoreLoadResult alone = load(piano_score(measure(1, k4 + note("D4", 4, stop))));
        REQUIRE(alone.ok);
        CHECK(alone.song.notes.size() == 1);
        const ScoreLoadResult later = load(piano_score(
            measure(1, k4 + note("C4", 1, start) + rest(1) + note("C4", 1, stop) + note("E4", 1))));
        REQUIRE(later.ok);
        CHECK(later.song.notes.size() == 3);
    }
    SUBCASE("within one bar, from one staff to the other, whichever is listed first") {
        // C4 on staff 2 on beat 1 starts a tie that C4 on staff 1 on beat 2 stops.
        const std::string staves = "<attributes><divisions>1</divisions><staves>2</staves><time>"
                                   "<beats>4</beats><beat-type>4</beat-type></time></attributes>";
        const std::string back = "<backup><duration>4</duration></backup>";
        const std::string upper =
            rest(1) + note("C4", 1, "|<staff>1</staff>" + tie("stop")) + rest(2);
        const std::string lower = note("C4", 1, "|<staff>2</staff>" + tie("start")) + rest(3);
        for (const std::string& body :
             {staves + upper + back + lower, staves + lower + back + upper}) {
            const ScoreLoadResult r = load(piano_score(measure(1, body)));
            REQUIRE(r.ok);
            REQUIRE(r.song.notes.size() == 1);
            CHECK(r.song.notes[0].end_tick == 2 * r.song.tempo.ticks_per_quarter);
        }
    }
    SUBCASE("from one staff into the other, in MusicXML and in MuseScore") {
        const Bytes xml = read_data("edge-tie-cross-staff.musicxml");
        const Bytes ms = read_data("edge-tie-cross-staff.mscx");
        for (const ScoreLoadResult& r : {load_musicxml(xml), load_mscx(ms.data(), ms.size())}) {
            REQUIRE(r.ok);
            REQUIRE(r.song.notes.size() == 2);
            CHECK(r.song.notes[0].pitch == 60);
            CHECK(r.song.notes[0].end_tick == 6 * r.song.tempo.ticks_per_quarter);
        }
    }
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
    CHECK(r.song.notes[0].hand == Hand::Left);
    CHECK(r.song.notes[1].hand == Hand::Right);
}

TEST_CASE("mscx: a tuplet shares its time out evenly and the next note starts on time") {
    // A tuplet of `actual` notes in the time of `normal` fills one quarter note here; the quarter
    // rest after it must start exactly 0.5 s in, and each note takes an equal share.
    struct Row {
        const char* base; // the note type
        int normal, actual;
    };
    for (Row row : {Row{"eighth", 2, 3}, Row{"16th", 4, 5}}) {
        std::string body = "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Tuplet><normalNotes>" +
                           std::to_string(row.normal) + "</normalNotes><actualNotes>" +
                           std::to_string(row.actual) + "</actualNotes><baseNote>" + row.base +
                           "</baseNote></Tuplet>";
        for (int i = 0; i < row.actual; ++i) {
            body += std::string("<Chord><durationType>") + row.base +
                    "</durationType><Note><pitch>" + std::to_string(60 + i) +
                    "</pitch></Note></Chord>";
        }
        body += "<endTuplet/><Chord><durationType>quarter</durationType><Note><pitch>72</pitch>"
                "</Note></Chord>";
        const std::string xml =
            "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\">"
            "<StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName></Part>"
            "<Staff id=\"1\"><Measure><voice>" +
            body + "</voice></Measure></Staff></Score></museScore>";
        const ScoreLoadResult r =
            load_mscx(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
        CAPTURE(row.base);
        REQUIRE(r.ok);
        REQUIRE(r.song.notes.size() == static_cast<std::size_t>(row.actual) + 1);
        const double share = 0.5 / row.actual;
        for (int i = 0; i < row.actual; ++i) {
            CHECK(r.song.notes[static_cast<std::size_t>(i)].start_s ==
                  doctest::Approx(share * i).epsilon(0.001));
            CHECK(r.song.notes[static_cast<std::size_t>(i)].duration_s ==
                  doctest::Approx(share).epsilon(0.001));
        }
        CHECK(r.song.notes.back().start_s == doctest::Approx(0.5).epsilon(0.001));
    }
}

TEST_CASE("musicxml: a missing <divisions> is read from the first full bar, with a warning") {
    // Eight units fill a 4/4 bar, so a unit is an eighth note: 0.25 s at 120 BPM.
    const ScoreLoadResult r = load_musicxml(read_data("edge-no-divisions.musicxml"));
    REQUIRE(r.ok);
    REQUIRE(r.song.notes.size() == 9);
    CHECK(r.song.notes[0].duration_s == doctest::Approx(0.25));
    CHECK(r.song.duration_s == doctest::Approx(4.0));
    CHECK(std::any_of(r.warnings.begin(), r.warnings.end(), [](const std::string& s) {
        return s.find("divisions") != std::string::npos;
    }));
}

TEST_CASE("musicxml: MIDI and MusicXML number the same music alike") {
    // The same two pieces as a MIDI file and as MusicXML: a full 3/4 bar then 4/4 (bars 1, 2, 3),
    // and a one-beat pickup then 4/4 (bars 0, 1, 2).
    struct Piece {
        const char* name;
        std::vector<int> bars;
    };
    for (const Piece& piece :
         {Piece{"edge-3-4-then-4-4", {1, 2, 3}}, Piece{"edge-pickup", {0, 1, 2}}}) {
        const char* name = piece.name;
        CAPTURE(name);
        const MidiLoadResult mid = load_midi(read_data(std::string(name) + ".mid"));
        const ScoreLoadResult xml = load_musicxml(read_data(std::string(name) + ".musicxml"));
        REQUIRE(mid.ok);
        REQUIRE(xml.ok);
        auto numbering = [](const Song& song) {
            std::vector<std::tuple<std::int64_t, int, int>> out;
            for (const Measure& m : song.measures) {
                out.emplace_back(m.start_tick, m.number, -1);
            }
            for (const Note& n : song.notes) {
                out.emplace_back(n.start_tick, n.pitch, n.measure);
            }
            return out;
        };
        CHECK(numbering(mid.song) == numbering(xml.song));
        for (const Song* song : {&mid.song, &xml.song}) {
            std::vector<int> bars;
            for (const Measure& m : song->measures) {
                bars.push_back(m.number);
            }
            CHECK(bars == piece.bars);
        }
    }
}

TEST_CASE("musicxml: a score that cannot be read says why") {
    const Bytes xml_no_notes = bytes_of(piano_score(measure(1, k4 + rest(4))));
    const Bytes svg = bytes_of("<?xml version=\"1.0\"?><svg><rect/></svg>");
    const Bytes text = bytes_of("this is not a score");
    const Bytes zip_no_score = test::zip_of({test::stored_entry("readme.txt", "no music here")});
    const Bytes not_a_zip = bytes_of("PK but then nothing that makes an archive");
    const Bytes ms2 = bytes_of("<museScore version=\"2.06\"><Score/></museScore>");
    struct Row {
        const char* what;
        ScoreLoadResult result;
        ScoreFileError error;
    };
    const Row rows[] = {
        {"no notes", load_musicxml(xml_no_notes), ScoreFileError::NoNotes},
        {"XML of another kind", load_musicxml(svg), ScoreFileError::NotScore},
        {"text", load_musicxml(text), ScoreFileError::NotXml},
        {"a zip without a score", load_mxl(zip_no_score), ScoreFileError::NotScore},
        {"bytes that are not a zip", load_mxl(not_a_zip), ScoreFileError::NotZip},
        {"MuseScore 2", load_mscx(ms2.data(), ms2.size()), ScoreFileError::Unsupported},
    };
    for (const Row& row : rows) {
        CAPTURE(row.what);
        CHECK_FALSE(row.result.ok);
        CHECK(row.result.error == row.error);
        CHECK_FALSE(row.result.message.empty());
    }
}

TEST_CASE(
    "mxl: the container names the score, else the first score entry; a BOM and doctype are fine") {
    const std::string score = piano_score(measure(1, k4 + note("C4", 4)));
    const char* container = "<container><rootfiles><rootfile full-path=\"score/piece.xml\"/>"
                            "<rootfile full-path=\"other.xml\"/></rootfiles></container>";
    const Bytes named = test::zip_of({test::stored_entry("META-INF/container.xml", container),
                                      test::stored_entry("other.xml", "<nothing/>"),
                                      test::stored_entry("score/piece.xml", score)});
    const Bytes no_container = test::zip_of(
        {test::stored_entry("notes.txt", "hello"), test::stored_entry("Piece.MusicXML", score)});
    const Bytes missing = test::zip_of({test::stored_entry("META-INF/container.xml", container),
                                        test::stored_entry("real.musicxml", score)});
    for (const Bytes& zip : {named, no_container, missing}) {
        const ScoreLoadResult r = load_mxl(zip);
        REQUIRE(r.ok);
        CHECK(r.song.notes.size() == 1);
    }

    std::string with_doctype = score;
    with_doctype.insert(with_doctype.find("?>") + 3,
                        "<!DOCTYPE score-partwise PUBLIC \"-//Recordare//DTD MusicXML 3.1 "
                        "Partwise//EN\" \"http://www.musicxml.org/dtds/partwise.dtd\">\n");
    CHECK(load("\xEF\xBB\xBF" + with_doctype).ok);
}

TEST_CASE(
    "a real piece reads the same from MusicXML, MuseScore 3, MuseScore 4 and MuseScore's MIDI") {
    // "Die Ehre Gottes aus der Natur" (Beethoven, voice and piano, 716 notes). MuseScore's own MIDI
    // export of the .mxl is the reference for pitch, start and end of every note, within its habit
    // of ending a note early (about 5%, half for staccato) and its ornament in bar 6.
    using Key = std::tuple<std::int64_t, int, std::int64_t>; // start, pitch, end
    auto keys_of = [](const Song& song) {
        std::vector<Key> out;
        for (const Note& n : song.notes) {
            if (n.start_tick < 10080 || n.start_tick > 10200) {
                out.emplace_back(n.start_tick, n.pitch, n.end_tick);
            }
        }
        std::sort(out.begin(), out.end());
        return out;
    };
    // Notes that differ in start or pitch, or end earlier by more than `slack` of their length.
    auto mismatches = [](const std::vector<Key>& a, const std::vector<Key>& b, double slack) {
        REQUIRE(a.size() == b.size());
        int bad = 0;
        for (std::size_t i = 0; i < a.size(); ++i) {
            const auto [start, pitch, end] = a[i];
            const auto early = static_cast<double>(end - std::get<2>(b[i]));
            const bool end_ok = early >= 0.0 && early <= slack * static_cast<double>(end - start);
            bad += (start != std::get<0>(b[i]) || pitch != std::get<1>(b[i]) ||
                    (end != std::get<2>(b[i]) && !end_ok))
                       ? 1
                       : 0;
        }
        return bad;
    };
    const ScoreLoadResult mxl = load_mxl(read_data("beethoven-op48-4-ehre-gottes.mxl"));
    const MidiLoadResult reference =
        load_midi(read_data("beethoven-op48-4-ehre-gottes-musescore-export.mid"));
    REQUIRE(mxl.ok);
    REQUIRE(reference.ok);
    CHECK(mxl.song.notes.size() == 716);
    CHECK(mismatches(keys_of(mxl.song), keys_of(reference.song), 0.52) == 0);
    REQUIRE(mxl.song.measures.size() == reference.song.measures.size());
    for (std::size_t i = 0; i < mxl.song.measures.size(); ++i) {
        CHECK(mxl.song.measures[i].start_tick == reference.song.measures[i].start_tick);
    }
    for (const char* name : {"beethoven-op48-4-ehre-gottes-musescore3.mscz",
                             "beethoven-op48-4-ehre-gottes-musescore4.mscz"}) {
        CAPTURE(name);
        const Bytes zip = read_data(name);
        for (const ScoreLoadResult& r : {load_mscz(zip), load_score(zip)}) {
            REQUIRE(r.ok);
            CHECK(r.song.measures.size() == mxl.song.measures.size());
            CHECK(mismatches(keys_of(r.song), keys_of(mxl.song), 0.0) == 0);
        }
    }
}

// --- Damaged and hostile files ---------------------------------------------------------------

TEST_CASE(
    "zip bombs are refused: a declared size over the cap, too many in total, too many entries") {
    std::vector<test::ZipEntry> total;
    for (int i = 0; i < 5; ++i) { // each 60 MiB, under the entry cap; together over the total cap
        total.push_back(test::zeros_entry("big" + std::to_string(i), 1, 60u * 1024 * 1024));
    }
    total.push_back(test::stored_entry("score.musicxml", "<x/>"));
    std::vector<test::ZipEntry> many;
    for (std::size_t i = 0; i < kMaxZipEntries + 1; ++i) {
        many.push_back(test::stored_entry("f" + std::to_string(i), "x"));
    }
    // 300 KB of deflate that would unpack to about 77 MB, over the 64 MiB entry cap.
    const std::size_t runs = 300'000;
    const Bytes bombs[] = {
        test::zip_of({test::zeros_entry("score.musicxml", runs,
                                        static_cast<std::uint32_t>(1 + 258 * runs))}),
        test::zip_of(total),
        test::zip_of(many),
    };
    for (const Bytes& zip : bombs) {
        const ScoreLoadResult r = load_mxl(zip);
        CHECK_FALSE(r.ok);
        CHECK(r.error == ScoreFileError::TooLarge);
    }
    // An entry that unpacks to more than its header says: the unpacker stops at 100 bytes.
    const ScoreLoadResult lying =
        load_mxl(test::zip_of({test::zeros_entry("score.musicxml", 1, 100)}));
    CHECK_FALSE(lying.ok);
}

TEST_CASE("hostile XML is refused without blowing up: entity bomb, huge file, nesting, bar counts, "
          "numbers") {
    const std::string entity_bomb =
        "<?xml version=\"1.0\"?><!DOCTYPE lolz [<!ENTITY a \"aaaaaaaaaa\">"
        "<!ENTITY b \"&a;&a;&a;&a;&a;&a;&a;&a;&a;&a;\"><!ENTITY c "
        "\"&b;&b;&b;&b;&b;&b;&b;&b;&b;&b;\">"
        "<!ENTITY d \"&c;&c;&c;&c;&c;&c;&c;&c;&c;&c;\">]><score-partwise><work><work-title>&d;"
        "</work-title></work></score-partwise>";
    const ScoreLoadResult bomb = load(entity_bomb);
    CHECK_FALSE(bomb.ok);
    CHECK(bomb.message.size() < 1000);

    Bytes big(kMaxScoreXmlBytes + 1, ' '); // only the size matters
    big[0] = '<';
    CHECK(load_musicxml(big).error == ScoreFileError::TooLarge);

    std::string many_bars =
        "<score-partwise><part-list><score-part id=\"P1\"><part-name>x</part-name>"
        "</score-part></part-list><part id=\"P1\">";
    for (std::size_t i = 0; i < kMaxZipEntries * 120; ++i) {
        many_bars += "<measure/>";
    }
    CHECK(load(many_bars + "</part></score-partwise>").error == ScoreFileError::TooLarge);

    CHECK_FALSE(load("<score-partwise>" +
                     [] {
                         std::string deep;
                         for (int i = 0; i < 200000; ++i) {
                             deep += "<a>";
                         }
                         return deep;
                     }())
                    .ok);

    // Numbers no score has: they must not overflow or loop.
    const std::string huge =
        "<score-partwise><part-list><score-part id=\"P1\"><part-name>x</part-name></score-part>"
        "</part-list><part id=\"P1\"><measure number=\"1\"><attributes>"
        "<divisions>999999999999</divisions></attributes>"
        "<note><pitch><step>C</step><octave>4</octave></pitch><duration>99999999999999999999"
        "</duration></note><backup><duration>-5</duration></backup>"
        "<note><pitch><step>E</step><alter>1e999</alter><octave>99999999999</octave></pitch>"
        "<duration>4</duration></note></measure><measure><attributes><divisions>1</divisions>"
        "<time><beats>999999</beats><beat-type>0</beat-type></time></attributes>"
        "<note><pitch><step>F</step><octave>4</octave></pitch><duration>4</duration></note>"
        "</measure></part></score-partwise>";
    load(huge); // returns, ok or not

    // Repeats that would expand past the note cap play straight through: 2000 bars of 33 notes
    // inside one repeat played 16 times would be over a million notes.
    std::string repeats =
        "<score-partwise><part-list><score-part id=\"P1\"><part-name>x</part-name>"
        "</score-part></part-list><part id=\"P1\">";
    for (int bar = 1; bar <= 2000; ++bar) {
        repeats += "<measure number=\"" + std::to_string(bar) + "\">";
        if (bar == 1) {
            repeats += "<attributes><divisions>16</divisions></attributes>"
                       "<barline location=\"left\"><repeat direction=\"forward\"/></barline>";
        }
        for (int k = 0; k < 33; ++k) {
            repeats += "<note><pitch><step>C</step><octave>4</octave></pitch><duration>2</duration>"
                       "</note>";
        }
        if (bar == 2000) {
            repeats += "<barline location=\"right\"><repeat direction=\"backward\" times=\"16\"/>"
                       "</barline>";
        }
        repeats += "</measure>";
    }
    const ScoreLoadResult r = load(repeats + "</part></score-partwise>");
    REQUIRE(r.ok);
    CHECK(r.stats.repeats_capped);
    CHECK(r.song.notes.size() == 66000);
}

TEST_CASE("damaged scores do not crash: a cut XML, a cut or garbled zip, noise") {
    const Bytes xml = bytes_of(piano_score(measure(1, note("C4", 4) + note("E4", 4))));
    REQUIRE(load_musicxml(xml).ok);
    const Bytes mxl = read_data("beethoven-op48-4-ehre-gottes.mxl");
    Bytes garbled = mxl;
    for (std::size_t i = 40; i < garbled.size(); i += 97) {
        garbled[i] = static_cast<std::uint8_t>(~garbled[i]);
    }
    Bytes noise(200);
    for (std::size_t i = 0; i < noise.size(); ++i) {
        noise[i] = static_cast<std::uint8_t>(i * 131 + 7);
    }
    const Bytes inputs[] = {Bytes(xml.begin(), xml.begin() + xml.size() / 2),
                            Bytes(mxl.begin(), mxl.begin() + mxl.size() / 2), garbled, noise};
    for (const Bytes& bad : inputs) {
        for (const ScoreLoadResult& r : {load_musicxml(bad), load_mxl(bad), load_score(bad)}) {
            if (!r.ok) {
                CHECK(r.error != ScoreFileError::None);
                CHECK(r.song.notes.empty());
            }
            for (const Note& n : r.song.notes) {
                CHECK(n.end_tick > n.start_tick);
            }
        }
    }
}
