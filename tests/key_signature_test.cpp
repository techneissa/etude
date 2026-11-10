// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/key_signature.hpp"
#include "etude/scales.hpp"

#include "smf_builder.hpp"
#include "etude/abc.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/song.hpp"

#include <doctest/doctest.h>

#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;
using etude::test::Bytes;
using etude::test::Track;

namespace {

Bytes read_data(const std::string& name) {
    std::ifstream in(std::string(ETUDE_TEST_DATA_DIR) + "/" + name, std::ios::binary);
    REQUIRE_MESSAGE(in.good(), "missing test data " << name);
    return Bytes(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

// Eighth notes (240 ticks at 480 per quarter) one after another from tick 0.
void melody(Track& t, std::initializer_list<int> pitches, int length = 240) {
    for (int p : pitches) {
        t.on(0, 0, p).off(static_cast<std::uint32_t>(length), 0, p);
    }
}

Song midi_song(const Track& track) {
    const MidiLoadResult r = load_midi(test::smf({track}));
    REQUIRE(r.ok);
    return r.song;
}

// The label text of the key a song shows at the start.
std::string label_of(const Song& song, bool solfege = false, int transpose = 0) {
    REQUIRE_FALSE(song.keys.empty());
    return key_label_text(key_label_parts(song.keys.front(), solfege, transpose));
}

const std::initializer_list<int> kCMajorScale = {60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60};
// A minor with the raised seventh at the cadence.
const std::initializer_list<int> kAMinorMelody = {69, 71, 72, 74, 76, 74, 72, 71, 69, 68, 69, 64, 69, 72, 71, 68, 69};
// Ode to Joy, the first phrase, in G major.
const std::initializer_list<int> kOdeInG = {71, 71, 72, 74, 74, 72, 71, 69, 67, 67, 69, 71, 71, 69, 69};

} // namespace

TEST_CASE("key: the tonic is a pitch class and follows the signature's spelling") {
    CHECK(key_from_signature(0, 1, KeyMode::Major).tonic == 7);   // G
    CHECK(key_from_signature(0, -3, KeyMode::Minor).tonic == 0);  // C minor
    CHECK(key_from_signature(0, 3, KeyMode::Minor).tonic == 6);   // F# minor
    CHECK(key_from_signature(0, 9, KeyMode::Major).fifths == 7);  // clamped
    CHECK(key_from_tonic_line(0, 2, KeyMode::Dorian).fifths == 0); // D dorian
    CHECK(key_from_tonic_line(0, 6, KeyMode::Minor).fifths == 3);  // F# minor
    CHECK(key_from_pitch_class(0, 9, KeyMode::Minor).fifths == 0); // A minor
    CHECK(key_from_pitch_class(0, 3, KeyMode::Major).fifths == -3); // Eb major, not D#
    CHECK(key_from_pitch_class(0, 6, KeyMode::Major).fifths == 6);
    CHECK(key_from_pitch_class(0, 6, KeyMode::Major, true).fifths == -6);
}

TEST_CASE("key: the transposed key is the one that is played") {
    const KeyChange g = key_from_signature(0, 1, KeyMode::Major);
    CHECK(key_label_text(key_label_parts(g, false, 0)) == "G major 1#");
    CHECK(key_label_text(key_label_parts(g, false, 2)) == "A major 3#");
    CHECK(key_label_text(key_label_parts(g, false, -2)) == "F major 1b");
    CHECK(key_label_text(key_label_parts(g, false, 1)) == "Ab major 4b");
    CHECK(key_label_text(key_label_parts(g, false, 12)) == "G major 1#");
    CHECK(key_label_text(key_label_parts(g, false, -12)) == "G major 1#");
    CHECK(key_label_text(key_label_parts(g, true, 2)) == "La major 3#");
    // A tie between six sharps and six flats follows the flats preference.
    const KeyChange c = key_from_signature(0, 0, KeyMode::Major);
    CHECK(key_label_text(key_label_parts(c, false, 6, false)) == "F# major 6#");
    CHECK(key_label_text(key_label_parts(c, false, 6, true)) == "Gb major 6b");
    const KeyChange am = key_from_signature(0, 0, KeyMode::Minor);
    CHECK(key_label_text(key_label_parts(am, false, 3)) == "C minor 3b");
    CHECK(transposed_key(g, 2).tonic == 9);
    // The mode stays.
    const KeyChange dd = key_from_signature(0, 0, KeyMode::Dorian);
    CHECK(key_label_text(key_label_parts(dd, false, 5)) == "G dorian 1b");
}

TEST_CASE("key: the key at a time or a tick") {
    std::vector<KeyChange> keys;
    keys.push_back(key_from_signature(480, 1, KeyMode::Major));
    keys.back().seconds = 0.5;
    keys.push_back(key_from_signature(1920, -2, KeyMode::Minor));
    keys.back().seconds = 2.0;
    CHECK(key_at_tick({}, 0) == nullptr);
    CHECK(key_at_seconds({}, 0.0) == nullptr);
    CHECK(key_index_at_seconds({}, 0.0) == -1);
    CHECK(key_at_tick(keys, 0) == &keys[0]);     // before the first change: the first key
    CHECK(key_at_tick(keys, 479) == &keys[0]);
    CHECK(key_at_tick(keys, 480) == &keys[0]);
    CHECK(key_at_tick(keys, 1919) == &keys[0]);
    CHECK(key_at_tick(keys, 1920) == &keys[1]);  // exactly at the change
    CHECK(key_index_at_seconds(keys, 0.0) == 0);
    CHECK(key_index_at_seconds(keys, 1.999) == 0);
    CHECK(key_index_at_seconds(keys, 2.0) == 1);
    CHECK(key_index_at_seconds(keys, 99.0) == 1);
}

TEST_CASE("key: mode names read from score files") {
    KeyMode m = KeyMode::Locrian;
    CHECK(key_mode_from_name("Major", m));
    CHECK(m == KeyMode::Major);
    CHECK(key_mode_from_name("aeolian", m));
    CHECK(m == KeyMode::Minor);
    CHECK(key_mode_from_name("MIXOLYDIAN", m));
    CHECK(m == KeyMode::Mixolydian);
    m = KeyMode::Dorian;
    CHECK_FALSE(key_mode_from_name("none", m));
    CHECK_FALSE(key_mode_from_name("", m));
    CHECK(m == KeyMode::Dorian);
}

// ---- The estimator

namespace {

Song song_of(std::initializer_list<int> pitches) {
    Song s;
    std::int64_t t = 0;
    for (int p : pitches) {
        Note n;
        n.pitch = static_cast<std::uint8_t>(p);
        n.start_tick = t;
        n.end_tick = t + 240;
        t += 240;
        s.notes.push_back(n);
    }
    s.end_tick = t;
    return s;
}

} // namespace

TEST_CASE("key estimate: a C major scale is C major") {
    const KeyEstimate e = estimate_song_key(song_of(kCMajorScale));
    REQUIRE(e.valid);
    CHECK(e.tonic == 0);
    CHECK(e.mode == KeyMode::Major);
    CHECK(e.correlation > 0.8f);
    CHECK(e.margin > 0.0f);
}

TEST_CASE("key estimate: an A minor melody is A minor") {
    const KeyEstimate e = estimate_song_key(song_of(kAMinorMelody));
    REQUIRE(e.valid);
    CHECK(e.tonic == 9);
    CHECK(e.mode == KeyMode::Minor);
}

TEST_CASE("key estimate: a G major tune is G major") {
    const KeyEstimate e = estimate_song_key(song_of(kOdeInG));
    REQUIRE(e.valid);
    CHECK(e.tonic == 7);
    CHECK(e.mode == KeyMode::Major);
    CHECK(e.correlation > e.c_major_correlation);
}

TEST_CASE("key estimate: too little to say") {
    CHECK_FALSE(estimate_song_key(song_of({60, 62, 64})).valid); // under kMinNotesForKey
    CHECK_FALSE(estimate_song_key(Song{}).valid);
    PitchClassWeights flat{};
    flat.fill(1.0);
    CHECK_FALSE(estimate_key(flat).valid); // all twelve alike: no key leads
    CHECK_FALSE(estimate_key(PitchClassWeights{}).valid);
    // Notes of one or two pitch classes are not a key either.
    CHECK_FALSE(estimate_song_key(song_of({60, 60, 60, 60, 60, 60, 60, 60})).valid);
    CHECK_FALSE(estimate_song_key(song_of({60, 67, 60, 67, 72, 79, 60, 67})).valid);
}

TEST_CASE("key estimate: exactly the least number of notes and of pitch classes is enough") {
    // Eight notes of a G major tune: enough. Seven: not.
    std::initializer_list<int> eight = {71, 71, 72, 74, 74, 72, 71, 69};
    CHECK(estimate_song_key(song_of(eight)).valid);
    CHECK_FALSE(estimate_song_key(song_of({71, 71, 72, 74, 74, 72, 71})).valid);
    static_assert(kMinNotesForKey == 8);
    // Three pitch classes: enough; two: not (the notes are plenty either way).
    CHECK(estimate_song_key(song_of({67, 71, 74, 67, 71, 74, 67, 71, 74})).valid);
    CHECK_FALSE(estimate_song_key(song_of({67, 71, 67, 71, 67, 71, 67, 71, 67, 71})).valid);
    static_assert(kMinPitchClassesForKey == 3);
}

TEST_CASE("key estimate: a C major placeholder gives way only when C major trails by the margin") {
    // From a C major scale played six times to a G major tune played six times, one repeat at a
    // time (`tunes` of the six). The file writes C major throughout.
    static_assert(kPlaceholderMargin == 0.10f);
    const struct {
        int tunes;
        bool replaced;
    } expected[] = {{0, false}, {1, false}, {2, false}, {3, true}, {4, true}, {5, true}, {6, true}};
    for (const auto& e : expected) {
        std::vector<int> pitches;
        for (int round = 0; round < 6; ++round) {
            const std::initializer_list<int> part = round < e.tunes ? kOdeInG : kCMajorScale;
            pitches.insert(pitches.end(), part.begin(), part.end());
        }
        Song s;
        std::int64_t t = 0;
        for (int p : pitches) {
            Note n;
            n.pitch = static_cast<std::uint8_t>(p);
            n.start_tick = t;
            n.end_tick = t + 240;
            t += 240;
            s.notes.push_back(n);
        }
        s.end_tick = t;
        s.keys.push_back(key_from_signature(0, 0, KeyMode::Major));
        complete_keys(s);
        CAPTURE(e.tunes);
        REQUIRE(s.keys.size() == 1);
        CHECK(s.keys[0].estimated == e.replaced);
        if (e.replaced) {
            CHECK(label_of(s) == "G major 1#");
        } else {
            CHECK(label_of(s) == "C major");
        }
    }
}

TEST_CASE("key estimate: drums do not count") {
    Song s = song_of(kOdeInG);
    for (int i = 0; i < 40; ++i) {
        Note d;
        d.pitch = 37; // a drum hit that would pull towards C#/Db
        d.drum = true;
        d.start_tick = i * 10;
        d.end_tick = i * 10 + 5000;
        s.notes.push_back(d);
    }
    const KeyEstimate e = estimate_song_key(s);
    REQUIRE(e.valid);
    CHECK(e.tonic == 7);
}

TEST_CASE("key estimate: longer notes weigh more") {
    // Five short Cs and Es over a long G major triad: the held notes say G.
    Song s = song_of({60, 64, 60, 64, 60, 64, 60, 64});
    for (Note& n : s.notes) {
        n.end_tick = n.start_tick + 20;
    }
    Note g;
    g.pitch = 67;
    g.start_tick = 0;
    g.end_tick = 4000;
    Note b = g;
    b.pitch = 71;
    Note d = g;
    d.pitch = 62;
    s.notes.push_back(g);
    s.notes.push_back(b);
    s.notes.push_back(d);
    const KeyEstimate e = estimate_song_key(s);
    REQUIRE(e.valid);
    CHECK(e.tonic == 7);
}

// ---- MIDI

TEST_CASE("key MIDI: the key-signature meta event, major and minor") {
    Track t;
    t.key_sig(0, 2, false);
    melody(t, kCMajorScale);
    t.end();
    const Song s = midi_song(t);
    REQUIRE(s.keys.size() == 1);
    CHECK(s.keys[0].tick == 0);
    CHECK_FALSE(s.keys[0].estimated);
    CHECK(label_of(s) == "D major 2#");

    Track m;
    m.key_sig(0, -3, true);
    melody(m, kCMajorScale);
    m.end();
    CHECK(label_of(midi_song(m)) == "C minor 3b");
}

TEST_CASE("key MIDI: a key change in the middle is a second key at its tick and time") {
    Track t;
    t.key_sig(0, 0, false);
    melody(t, kCMajorScale); // 15 eighths = 3600 ticks
    t.key_sig(0, 1, false);
    melody(t, kOdeInG);
    t.end();
    const Song s = midi_song(t);
    REQUIRE(s.keys.size() == 2);
    CHECK(s.keys[0].tick == 0);
    CHECK(s.keys[1].tick == 3600);
    CHECK(s.keys[1].seconds == doctest::Approx(s.tempo.seconds_at(3600)));
    CHECK(s.keys[1].seconds == doctest::Approx(3.75)); // 120 BPM: 3600 ticks = 7.5 beats
    CHECK(key_label_text(key_label_parts(s.keys[1], false)) == "G major 1#");
    CHECK(key_at_seconds(s.keys, 3.0)->fifths == 0);
    CHECK(key_at_seconds(s.keys, 3.75)->fifths == 1);
}

TEST_CASE("key MIDI: events from two tracks merge, and the same key twice is one") {
    Track a;
    a.key_sig(0, 1, false);
    melody(a, kOdeInG);
    a.end();
    Track b;
    b.key_sig(0, 1, false).key_sig(480, 1, false);
    b.end();
    const MidiLoadResult r = load_midi(test::smf({a, b}));
    REQUIRE(r.ok);
    CHECK(r.song.keys.size() == 1);
}

TEST_CASE("key MIDI: nonsense key events are ignored") {
    Track t;
    t.key_sig(0, 9, false); // 9 sharps
    t.meta(0, 0x59, {0x01});                      // too short
    t.meta(0, 0x59, {0x00, 0x05});                // mode byte neither 0 nor 1
    melody(t, kOdeInG);
    t.end();
    const Song s = midi_song(t);
    // Nothing valid was written, so the notes decide.
    REQUIRE(s.keys.size() == 1);
    CHECK(s.keys[0].estimated);
    CHECK(label_of(s) == "G major 1#");
}

TEST_CASE("key MIDI: no key in the file is estimated from the notes, shown like a written one") {
    Track t;
    melody(t, kOdeInG);
    t.end();
    const Song s = midi_song(t);
    REQUIRE(s.keys.size() == 1);
    CHECK(s.keys[0].estimated);
    CHECK(s.keys[0].tick == 0);
    CHECK(s.keys[0].confidence > 0.5f);
    CHECK(label_of(s) == "G major 1#"); // no marker of any kind in the text
    Track m;
    melody(m, kAMinorMelody);
    m.end();
    CHECK(label_of(midi_song(m)) == "A minor");
}

TEST_CASE("key MIDI: a C major placeholder the notes disagree with is treated as no key") {
    Track t;
    t.key_sig(0, 0, false); // sf 0, mi 0
    melody(t, kOdeInG);
    t.end();
    const Song s = midi_song(t);
    REQUIRE(s.keys.size() == 1);
    CHECK(s.keys[0].estimated);
    CHECK(label_of(s) == "G major 1#");
}

TEST_CASE("key MIDI: a real C major stays C major, and a written key is never overruled") {
    Track t;
    t.key_sig(0, 0, false);
    melody(t, kCMajorScale);
    t.end();
    const Song s = midi_song(t);
    REQUIRE(s.keys.size() == 1);
    CHECK_FALSE(s.keys[0].estimated);
    CHECK(label_of(s) == "C major");

    // A piece written in F major whose notes lean to G is still F major: only the placeholder
    // gives way.
    Track f;
    f.key_sig(0, -1, false);
    melody(f, kOdeInG);
    f.end();
    const Song fs = midi_song(f);
    REQUIRE(fs.keys.size() == 1);
    CHECK_FALSE(fs.keys[0].estimated);
    CHECK(label_of(fs) == "F major 1b");

    // A C major sf 0 written as a change later in a piece is not a lone placeholder.
    Track c;
    c.key_sig(0, 1, false);
    melody(c, kOdeInG);
    c.key_sig(0, 0, false);
    melody(c, kCMajorScale);
    c.end();
    const Song cs = midi_song(c);
    REQUIRE(cs.keys.size() == 2);
    CHECK_FALSE(cs.keys[0].estimated);
    CHECK_FALSE(cs.keys[1].estimated);
}

TEST_CASE("key MIDI: a placeholder on an A minor piece reads A minor; C major scale stays") {
    Track t;
    t.key_sig(0, 0, false);
    melody(t, kAMinorMelody);
    t.end();
    CHECK(label_of(midi_song(t)) == "A minor");
}

TEST_CASE("key MIDI: a song with too few notes to tell has no key") {
    Track t;
    melody(t, {60, 64, 67});
    t.end();
    CHECK(midi_song(t).keys.empty());
}

TEST_CASE("key MIDI: real pieces") {
    // Satie's Gymnopédie no. 1 is written with two sharps (the file says minor), Bach's first
    // prelude in C major.
    const Bytes satie = read_data("satie-gymnopedie-1.mid");
    const MidiLoadResult rs = load_midi(satie.data(), satie.size());
    REQUIRE(rs.ok);
    REQUIRE_FALSE(rs.song.keys.empty());
    // A key the file writes always wins, whatever the notes say: the file's two sharps, minor.
    CHECK(rs.song.keys[0].fifths == 2);
    CHECK(rs.song.keys[0].mode == KeyMode::Minor);
    CHECK(rs.song.keys[0].tonic == 11);
    CHECK_FALSE(rs.song.keys[0].estimated);
    CHECK(label_of(rs.song) == "B minor 2#");
    const Bytes bach = read_data("bach-bwv846-prelude.mid");
    const MidiLoadResult rb = load_midi(bach.data(), bach.size());
    REQUIRE(rb.ok);
    REQUIRE_FALSE(rb.song.keys.empty());
    CHECK(rb.song.keys[0].tonic == 0);
    CHECK(rb.song.keys[0].mode == KeyMode::Major);
}

// ---- MusicXML

namespace {

std::string xml_score(const std::string& measures) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<score-partwise version=\"3.1\"><part-list><score-part id=\"P1\"><part-name>Piano"
           "</part-name></score-part></part-list><part id=\"P1\">" +
           measures + "</part></score-partwise>";
}

std::string xml_notes(std::initializer_list<const char*> steps) {
    std::string out;
    for (const char* s : steps) {
        out += "<note><pitch><step>";
        out += s;
        out += "</step><octave>4</octave></pitch><duration>1</duration></note>";
    }
    return out;
}

std::string xml_bar(int number, const std::string& attributes, const std::string& body) {
    return "<measure number=\"" + std::to_string(number) + "\"><attributes>" + attributes +
           "</attributes>" + body + "</measure>";
}

const std::string kDivs = "<divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>";

} // namespace

TEST_CASE("key MusicXML: fifths and mode, with a key change in the third bar") {
    const std::string xml = xml_score(
        xml_bar(1, kDivs + "<key><fifths>2</fifths><mode>major</mode></key>",
                xml_notes({"D", "E", "F", "G"})) +
        xml_bar(2, "", xml_notes({"A", "B", "C", "D"})) +
        xml_bar(3, "<key><fifths>-1</fifths><mode>minor</mode></key>",
                xml_notes({"D", "E", "F", "G"})));
    const ScoreLoadResult r = load_musicxml(bytes_of(xml));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 2);
    CHECK(r.song.keys[0].tick == 0);
    CHECK(key_label_text(key_label_parts(r.song.keys[0], false)) == "D major 2#");
    CHECK(r.song.keys[1].tick == 8 * 480); // the third bar, 4 quarters a bar
    CHECK(r.song.keys[1].seconds == doctest::Approx(4.0));
    CHECK(key_label_text(key_label_parts(r.song.keys[1], false)) == "D minor 1b");
}

TEST_CASE("key MusicXML: the modes") {
    for (const char* mode : {"dorian", "phrygian", "lydian", "mixolydian", "locrian"}) {
        CAPTURE(mode);
        const std::string xml = xml_score(
            xml_bar(1, kDivs + std::string("<key><fifths>0</fifths><mode>") + mode + "</mode></key>",
                    xml_notes({"D", "E", "F", "G", "A", "B", "C", "D"})));
        const ScoreLoadResult r = load_musicxml(bytes_of(xml));
        REQUIRE(r.ok);
        REQUIRE(r.song.keys.size() == 1);
        CHECK(std::string(mode_word(r.song.keys[0].mode)) == mode);
    }
    // A key with no mode element is major; "none" is read as major too.
    const ScoreLoadResult r = load_musicxml(bytes_of(xml_score(
        xml_bar(1, kDivs + "<key><fifths>1</fifths></key>", xml_notes({"G", "A", "B", "C"})))));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 1);
    CHECK(r.song.keys[0].mode == KeyMode::Major);
}

TEST_CASE("key MusicXML: a transposing part's key is the sounding one") {
    // A clarinet in Bb writes D major (2 sharps) for a concert C major.
    const std::string xml = xml_score(xml_bar(
        1, kDivs + "<key><fifths>2</fifths></key><transpose><diatonic>-1</diatonic>"
                   "<chromatic>-2</chromatic></transpose>",
        xml_notes({"D", "E", "F", "G"})));
    const ScoreLoadResult r = load_musicxml(bytes_of(xml));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 1);
    CHECK(r.song.keys[0].fifths == 0);
}

TEST_CASE("key MusicXML: no key element is estimated, a repeat back is a key change again") {
    const std::string none = xml_score(xml_bar(
        1, kDivs, xml_notes({"G", "A", "B", "C", "D", "C", "B", "A", "G", "G", "A", "B"})));
    const ScoreLoadResult r = load_musicxml(bytes_of(none));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 1);
    CHECK(r.song.keys[0].estimated);

    // Bar 1 in G (a repeat sign starts here), bar 2 in D and the repeat back to bar 1: the key
    // changes at every bar that plays, so the second pass has its own changes.
    const std::string repeated = xml_score(
        "<measure number=\"1\"><attributes>" + kDivs +
        "<key><fifths>1</fifths></key></attributes>"
        "<barline location=\"left\"><repeat direction=\"forward\"/></barline>" +
        xml_notes({"G", "A", "B", "D"}) +
        "</measure><measure number=\"2\"><attributes><key><fifths>2</fifths></key></attributes>" +
        xml_notes({"D", "E", "F", "A"}) +
        "<barline location=\"right\"><repeat direction=\"backward\"/></barline></measure>");
    const ScoreLoadResult rr = load_musicxml(bytes_of(repeated));
    REQUIRE(rr.ok);
    REQUIRE(rr.song.keys.size() == 4);
    CHECK(rr.song.keys[0].fifths == 1);
    CHECK(rr.song.keys[1].fifths == 2);
    CHECK(rr.song.keys[2].fifths == 1);
    CHECK(rr.song.keys[2].tick == 2 * 4 * 480);
    CHECK(rr.song.keys[3].fifths == 2);
}

TEST_CASE("key MusicXML: real files") {
    for (const char* file : {"beethoven-op48-4-ehre-gottes.mxl"}) {
        const Bytes b = read_data(file);
        const ScoreLoadResult r = load_mxl(b.data(), b.size());
        REQUIRE(r.ok);
        REQUIRE_FALSE(r.song.keys.empty());
        CHECK_FALSE(r.song.keys[0].estimated);
    }
}

// ---- MuseScore

namespace {

std::string mscx_of(const std::string& key_a, const std::string& key_b = "") {
    return "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\">"
           "<StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName></Part>"
           "<Staff id=\"1\"><Measure><voice>" +
           key_a +
           "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>"
           "<Chord><durationType>whole</durationType><Note><pitch>67</pitch></Note></Chord>"
           "</voice></Measure><Measure><voice>" +
           key_b +
           "<Chord><durationType>whole</durationType><Note><pitch>71</pitch></Note></Chord>"
           "</voice></Measure></Staff></Score></museScore>";
}

} // namespace

TEST_CASE("key MuseScore: KeySig in MuseScore 3 and 4 form, with a change") {
    const Bytes three = bytes_of(mscx_of("<KeySig><accidental>2</accidental></KeySig>",
                                         "<KeySig><accidental>-3</accidental></KeySig>"));
    const ScoreLoadResult r3 = load_mscx(three.data(), three.size());
    REQUIRE(r3.ok);
    REQUIRE(r3.song.keys.size() == 2);
    CHECK(r3.song.keys[0].fifths == 2);
    CHECK(r3.song.keys[1].fifths == -3);
    CHECK(r3.song.keys[1].tick == 4 * 480);

    const Bytes four = bytes_of(mscx_of("<KeySig><concertKey>-2</concertKey><mode>minor</mode></KeySig>"));
    const ScoreLoadResult r4 = load_mscx(four.data(), four.size());
    REQUIRE(r4.ok);
    REQUIRE(r4.song.keys.size() == 1);
    CHECK(r4.song.keys[0].fifths == -2);
    CHECK(r4.song.keys[0].mode == KeyMode::Minor);
}

TEST_CASE("key MuseScore: real .mscz files carry the key") {
    for (const char* file : {"beethoven-op48-4-ehre-gottes-musescore3.mscz",
                             "beethoven-op48-4-ehre-gottes-musescore4.mscz"}) {
        CAPTURE(file);
        const Bytes b = read_data(file);
        const ScoreLoadResult r = load_mscz(b.data(), b.size());
        REQUIRE(r.ok);
        REQUIRE_FALSE(r.song.keys.empty());
    }
}

// ---- ABC

namespace {

ScoreLoadResult abc_of(const std::string& header_key, const std::string& music) {
    const std::string text = "X:1\nT:t\nM:4/4\nL:1/8\nK:" + header_key + "\n" + music + "\n";
    return load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

} // namespace

TEST_CASE("key ABC: K: with a mode, minor, sharps and flats") {
    const struct {
        const char* k;
        const char* text;
    } cases[] = {
        {"G", "G major 1#"},        {"Ddor", "D dorian"},     {"D dorian", "D dorian"},
        {"Am", "A minor"},          {"F#m", "F# minor 3#"},   {"Bb", "Bb major 2b"},
        {"Gmix", "G mixolydian"},   {"A Lydian", "A lydian 4#"}, {"Ephr", "E phrygian"},
        {"Bloc", "B locrian"},      {"C", "C major"},         {"Ebmin", "Eb minor 6b"},
    };
    for (const auto& c : cases) {
        CAPTURE(c.k);
        const ScoreLoadResult r = abc_of(c.k, "CDEF GABc|cBAG FEDC|CDEF GABc|cBAG FEDC|");
        REQUIRE(r.ok);
        REQUIRE_FALSE(r.song.keys.empty());
        CHECK_FALSE(r.song.keys[0].estimated);
        if (std::string(c.k) == "C") {
            continue; // a C major header is the placeholder; the notes may overrule it
        }
        CHECK(key_label_text(key_label_parts(r.song.keys[0], false)) == c.text);
    }
}

TEST_CASE("key ABC: an inline [K:] and a K: line mid-tune change the key") {
    const ScoreLoadResult r =
        abc_of("D", "DEFG ABcd|[K:G] GABc dedc|\nK:Amin\nABcd efge|[K:Ador]ABcd efge|");
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 4);
    CHECK(key_label_text(key_label_parts(r.song.keys[0], false)) == "D major 2#");
    CHECK(key_label_text(key_label_parts(r.song.keys[1], false)) == "G major 1#");
    CHECK(key_label_text(key_label_parts(r.song.keys[2], false)) == "A minor");
    CHECK(key_label_text(key_label_parts(r.song.keys[3], false)) == "A dorian 1#");
    REQUIRE(r.song.measures.size() >= 4);
    CHECK(r.song.keys[1].tick == r.song.measures[1].start_tick);
    CHECK(r.song.keys[2].tick == r.song.measures[2].start_tick);
    CHECK(r.song.keys[3].tick == r.song.measures[3].start_tick);
    CHECK(r.song.keys[3].mode == KeyMode::Dorian);
}

TEST_CASE("key ABC: K:none and K:HP name no tonic, so the notes decide") {
    const ScoreLoadResult r = abc_of("none", "GABc dedc|BGAB GABd|GABc dedc|BGAB G4|");
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 1);
    CHECK(r.song.keys[0].estimated);
}

TEST_CASE("key ABC: a K:C placeholder over a G major tune is estimated; real files") {
    const ScoreLoadResult r = abc_of("C", "GABc dedc|BGAB GABd|GABc dedc|BGAB G4|");
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 1);
    CHECK(r.song.keys[0].estimated);
    CHECK(r.song.keys[0].tonic == 7);

    const Bytes b = read_data("abc-dorian.abc");
    const ScoreLoadResult d = load_abc(b.data(), b.size());
    REQUIRE(d.ok);
    REQUIRE(d.song.keys.size() == 1);
    CHECK(key_label_text(key_label_parts(d.song.keys[0], false)) == "D dorian");
}

// ---- Where a key stands, which key wins, and what is refused

TEST_CASE("key MusicXML: a key named mid-bar changes the key mid-bar") {
    // Four quarter notes, then a <key> in the same bar before the last two (a division is a
    // quarter note here).
    const std::string xml = xml_score(
        "<measure number=\"1\"><attributes><divisions>1</divisions><key><fifths>0</fifths></key>"
        "<time><beats>4</beats><beat-type>4</beat-type></time></attributes>" +
        xml_notes({"C", "D"}) +
        "<attributes><key><fifths>2</fifths></key></attributes>" + xml_notes({"E", "F"}) +
        "</measure>");
    const ScoreLoadResult r = load_musicxml(bytes_of(xml));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 2);
    CHECK(r.song.keys[0].fifths == 0);
    CHECK(r.song.keys[1].fifths == 2);
    CHECK(r.song.keys[1].tick == 2 * r.song.tempo.ticks_per_quarter); // after two quarters
}

TEST_CASE("key MusicXML: of several keys in one attributes the unnumbered, else the lowest number wins") {
    const auto key_of = [](const std::string& keys) {
        const std::string xml = xml_score(xml_bar(1, kDivs + keys, xml_notes({"C", "D", "E", "F"})));
        const ScoreLoadResult r = load_musicxml(bytes_of(xml));
        REQUIRE(r.ok);
        REQUIRE(r.song.keys.size() >= 1);
        return static_cast<int>(r.song.keys[0].fifths);
    };
    CHECK(key_of("<key number=\"2\"><fifths>3</fifths></key><key number=\"1\"><fifths>1</fifths></key>") == 1);
    CHECK(key_of("<key number=\"1\"><fifths>1</fifths></key><key><fifths>-2</fifths></key>") == -2);
    CHECK(key_of("<key number=\"3\"><fifths>4</fifths></key><key number=\"2\"><fifths>-1</fifths></key>") == -1);
}

TEST_CASE("key MusicXML: more than seven accidentals is no key, written or after a transposition") {
    const auto keys_of = [](const std::string& attrs) {
        const std::string xml = xml_score(xml_bar(1, kDivs + attrs, xml_notes({"C", "D", "E", "F"})));
        const ScoreLoadResult r = load_musicxml(bytes_of(xml));
        REQUIRE(r.ok);
        return r.song.keys;
    };
    // Eight sharps is not written down; the notes (too few here) decide, so there is none.
    CHECK(keys_of("<key><fifths>8</fifths></key>").empty());
    CHECK(keys_of("<key><fifths>-8</fifths></key>").empty());
    CHECK(keys_of("<key><fifths>14</fifths></key>").empty());
    // Seven is the most.
    REQUIRE(keys_of("<key><fifths>7</fifths></key>").size() == 1);
    CHECK(keys_of("<key><fifths>7</fifths></key>")[0].fifths == 7);
    REQUIRE(keys_of("<key><fifths>-7</fifths></key>").size() == 1);
    // A written key within the limit that a transposition moves past it is refused as well as
    // one that stays: the concert key has to be a real signature.
    const auto moved = keys_of("<key><fifths>7</fifths></key><transpose><chromatic>-2</chromatic>"
                               "</transpose>");
    for (const KeyChange& k : moved) {
        CHECK(k.fifths >= -7);
        CHECK(k.fifths <= 7);
    }
}

TEST_CASE("key MuseScore: a KeySig named mid-measure changes the key mid-measure") {
    const std::string mscx =
        "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\">"
        "<StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName></Part>"
        "<Staff id=\"1\"><Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig>"
        "<KeySig><accidental>0</accidental></KeySig>"
        "<Chord><durationType>half</durationType><Note><pitch>60</pitch></Note></Chord>"
        "<KeySig><accidental>1</accidental></KeySig>"
        "<Chord><durationType>half</durationType><Note><pitch>67</pitch></Note></Chord>"
        "</voice></Measure></Staff></Score></museScore>";
    const Bytes b = bytes_of(mscx);
    const ScoreLoadResult r = load_mscx(b.data(), b.size());
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 2);
    CHECK(r.song.keys[0].fifths == 0);
    CHECK(r.song.keys[1].fifths == 1);
    CHECK(r.song.keys[1].tick == 2 * 480); // after the half note
}

TEST_CASE("key ABC: an inline [K:] in the middle of a bar changes the key there") {
    const ScoreLoadResult r = abc_of("D", "DEFG [K:G]ABcd|GABc dedc|");
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 2);
    CHECK(r.song.keys[0].fifths == 2);
    CHECK(r.song.keys[1].fifths == 1);
    // After four of the bar's eight eighth notes.
    CHECK(r.song.keys[1].tick == r.song.measures[0].start_tick + r.song.tempo.ticks_per_quarter * 2);
}

// ---- Which way black keys are spelled

TEST_CASE("key spelling: flats in a flat key, sharps in a sharp key and in none") {
    CHECK_FALSE(key_spells_flats(nullptr));
    CHECK_FALSE(key_spells_flats(nullptr, 5));
    const auto key = [](int fifths, KeyMode m = KeyMode::Major) {
        static KeyChange k;
        k = key_from_signature(0, fifths, m);
        return &k;
    };
    CHECK(key_spells_flats(key(-3)));                  // E flat major
    CHECK(key_spells_flats(key(-1)));                  // F major
    CHECK(key_spells_flats(key(-3, KeyMode::Minor)));  // C minor
    CHECK_FALSE(key_spells_flats(key(0)));             // C major
    CHECK_FALSE(key_spells_flats(key(0, KeyMode::Minor)));
    CHECK_FALSE(key_spells_flats(key(0, KeyMode::Dorian)));
    CHECK_FALSE(key_spells_flats(key(1)));             // G major
    CHECK_FALSE(key_spells_flats(key(5)));             // B major
    CHECK(key_spells_flats(key(-2, KeyMode::Dorian))); // C dorian, two flats: by its signature
    // The key that is played: G major (one sharp) up a semitone is A flat major.
    CHECK(key_spells_flats(key(1), 1));
    CHECK(key_spells_flats(key(-3), 2));       // E flat major up a tone: F major, one flat
    CHECK_FALSE(key_spells_flats(key(-3), 4)); // E flat major up a major third: G major
}

TEST_CASE("key: a tie of six sharps and six flats goes by the tonic, the scales' rule") {
    const auto label = [](int pc, KeyMode mode, bool flats = false) {
        return key_label_text(key_label_parts(key_from_pitch_class(0, pc, mode, flats), false));
    };
    CHECK(label(6, KeyMode::Major) == "F# major 6#");
    CHECK(label(6, KeyMode::Major, true) == "Gb major 6b");
    CHECK(label(3, KeyMode::Minor) == "Eb minor 6b");
    CHECK(label(8, KeyMode::Dorian) == "Ab dorian 6b");
    CHECK(label(5, KeyMode::Locrian) == "F locrian 6b");
    CHECK(label(11, KeyMode::Lydian) == "B lydian 6#");
    CHECK(label(10, KeyMode::Phrygian) == "Bb phrygian 6b");
    CHECK(label(1, KeyMode::Mixolydian) == "Db mixolydian 6b");
    // The ties' neighbours are unchanged: five sharps or flats and fewer.
    CHECK(label(1, KeyMode::Minor) == "C# minor 4#");
    CHECK(label(8, KeyMode::Minor) == "G# minor 5#");
    CHECK(label(10, KeyMode::Minor) == "Bb minor 5b");
    CHECK(label(6, KeyMode::Minor) == "F# minor 3#");
    // A key moved by semitones settles its tie the same way.
    const KeyChange a_minor = key_from_pitch_class(0, 9, KeyMode::Minor);
    CHECK(key_label_text(key_label_parts(transposed_key(a_minor, 6), false)) == "Eb minor 6b");
    const KeyChange c_major = key_from_pitch_class(0, 0, KeyMode::Major);
    CHECK(transposed_key(c_major, 6).fifths == 6); // F# major
}
