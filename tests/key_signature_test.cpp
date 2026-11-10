// SPDX-License-Identifier: GPL-3.0-or-later
// The key a file writes, read from each format, and the key estimated from the notes when a file
// writes none or only the C major placeholder.
#include "etude/abc.hpp"
#include "etude/key_signature.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/song.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <string>
#include <vector>

using namespace etude;
using etude::test::Track;

namespace {

using Bytes = std::vector<std::uint8_t>;

Bytes bytes_of(const std::string& text) { return Bytes(text.begin(), text.end()); }

std::string label(const KeyChange& key) { return key_label_text(key_label_parts(key, false)); }

// Eighth notes one after another from tick 0.
void melody(Track& t, const std::vector<int>& pitches) {
    for (int p : pitches) {
        t.on(0, 0, p).off(240, 0, p);
    }
}

Song midi_song(const Track& track) {
    const MidiLoadResult r = load_midi(test::smf({track}));
    REQUIRE(r.ok);
    return r.song;
}

const std::vector<int> kCMajorScale = {60, 62, 64, 65, 67, 69, 71, 72, 71, 69, 67, 65, 64, 62, 60};
const std::vector<int> kAMinorMelody = {69, 71, 72, 74, 76, 74, 72, 71, 69,
                                        68, 69, 64, 69, 72, 71, 68, 69};
// Ode to Joy, the first phrase, in G major.
const std::vector<int> kOdeInG = {71, 71, 72, 74, 74, 72, 71, 69, 67, 67, 69, 71, 71, 69, 69};

Song song_of(const std::vector<int>& pitches) {
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

std::string xml_score(const std::string& measures) {
    return "<?xml version=\"1.0\"?><score-partwise version=\"3.1\"><part-list><score-part "
           "id=\"P1\">"
           "<part-name>Piano</part-name></score-part></part-list><part id=\"P1\">" +
           measures + "</part></score-partwise>";
}

// A bar of four quarter notes after `attributes` (the first bar adds divisions and 4/4).
std::string xml_bar(int number, const std::string& attributes) {
    const std::string first =
        number == 1
            ? "<divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>"
            : "";
    std::string notes;
    for (const char* step : {"D", "E", "F", "G"}) {
        notes += std::string("<note><pitch><step>") + step +
                 "</step><octave>4</octave></pitch><duration>1</duration></note>";
    }
    return "<measure number=\"" + std::to_string(number) + "\"><attributes>" + first + attributes +
           "</attributes>" + notes + "</measure>";
}

ScoreLoadResult abc_of(const std::string& key, const std::string& music) {
    const std::string text = "X:1\nT:t\nM:4/4\nL:1/8\nK:" + key + "\n" + music + "\n";
    return load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
}

} // namespace

TEST_CASE("key: a key written in a MIDI file is shown, a change comes at its tick") {
    Track major;
    major.key_sig(0, 2, false);
    melody(major, kCMajorScale);
    major.end();
    const Song s = midi_song(major);
    REQUIRE(s.keys.size() == 1);
    CHECK_FALSE(s.keys[0].estimated);
    CHECK(label(s.keys[0]) == "D major 2#");

    Track minor;
    minor.key_sig(0, -3, true);
    melody(minor, kCMajorScale);
    minor.end();
    CHECK(label(midi_song(minor).keys[0]) == "C minor 3b");

    Track change;
    change.key_sig(0, 0, false);
    melody(change, kCMajorScale); // 15 eighths = 3600 ticks
    change.key_sig(0, 1, false);
    melody(change, kOdeInG);
    change.end();
    const Song c = midi_song(change);
    REQUIRE(c.keys.size() == 2);
    CHECK(c.keys[1].tick == 3600);
    CHECK(c.keys[1].seconds == doctest::Approx(3.75)); // 120 BPM: 7.5 beats
    CHECK(key_at_seconds(c.keys, 3.0)->fifths == 0);
    CHECK(key_at_seconds(c.keys, 3.75)->fifths == 1);

    // The same key written twice, on two tracks, is one key.
    Track a;
    a.key_sig(0, 1, false);
    melody(a, kOdeInG);
    a.end();
    Track b;
    b.key_sig(0, 1, false).key_sig(480, 1, false);
    b.end();
    CHECK(load_midi(test::smf({a, b})).song.keys.size() == 1);
}

TEST_CASE("key: a key written in MusicXML or MuseScore is shown, with the key as played") {
    // Fifths and mode, with a key change in the third bar (4 quarters a bar).
    const ScoreLoadResult r = load_musicxml(bytes_of(
        xml_score(xml_bar(1, "<key><fifths>2</fifths><mode>major</mode></key>") + xml_bar(2, "") +
                  xml_bar(3, "<key><fifths>-1</fifths><mode>minor</mode></key>"))));
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 2);
    CHECK(label(r.song.keys[0]) == "D major 2#");
    CHECK(r.song.keys[1].tick == 8 * 480);
    CHECK(label(r.song.keys[1]) == "D minor 1b");

    // A mode word, and no mode element at all (major).
    const ScoreLoadResult dorian = load_musicxml(
        bytes_of(xml_score(xml_bar(1, "<key><fifths>0</fifths><mode>dorian</mode></key>"))));
    REQUIRE(dorian.ok);
    CHECK(dorian.song.keys[0].mode == KeyMode::Dorian);
    const ScoreLoadResult bare =
        load_musicxml(bytes_of(xml_score(xml_bar(1, "<key><fifths>1</fifths></key>"))));
    REQUIRE(bare.ok);
    CHECK(bare.song.keys[0].mode == KeyMode::Major);

    // A clarinet in Bb writes D major (2 sharps) for a concert C major: the sounding key is shown.
    const ScoreLoadResult clarinet = load_musicxml(bytes_of(xml_score(
        xml_bar(1, "<key><fifths>2</fifths></key><transpose><diatonic>-1</diatonic><chromatic>-2"
                   "</chromatic></transpose>"))));
    REQUIRE(clarinet.ok);
    CHECK(clarinet.song.keys[0].fifths == 0);

    // MuseScore 3 (an accidental count) and 4 (concertKey and mode), with a change a bar later.
    auto mscx = [](const std::string& key_a, const std::string& key_b) {
        const std::string xml =
            "<museScore version=\"3.02\"><Score><Division>480</Division><Part><Staff id=\"1\">"
            "<StaffType group=\"pitched\"/></Staff><trackName>Piano</trackName></Part>"
            "<Staff id=\"1\"><Measure><voice>" +
            key_a +
            "<TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>whole"
            "</durationType><Note><pitch>67</pitch></Note></Chord></voice></Measure><Measure>"
            "<voice>" +
            key_b +
            "<Chord><durationType>whole</durationType><Note><pitch>71</pitch>"
            "</Note></Chord></voice></Measure></Staff></Score></museScore>";
        return load_mscx(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
    };
    const ScoreLoadResult three = mscx("<KeySig><accidental>2</accidental></KeySig>",
                                       "<KeySig><accidental>-3</accidental></KeySig>");
    REQUIRE(three.ok);
    REQUIRE(three.song.keys.size() == 2);
    CHECK(three.song.keys[1].fifths == -3);
    CHECK(three.song.keys[1].tick == 4 * 480);
    const ScoreLoadResult four =
        mscx("<KeySig><concertKey>-2</concertKey><mode>minor</mode></KeySig>", "");
    REQUIRE(four.ok);
    CHECK(four.song.keys[0].fifths == -2);
    CHECK(four.song.keys[0].mode == KeyMode::Minor);
}

TEST_CASE("key: K: in ABC, with a mode, and changes in the middle of a tune") {
    struct Row {
        const char* key;
        const char* label;
    };
    for (Row row : {Row{"G", "G major 1#"}, Row{"Ddor", "D dorian"}, Row{"Am", "A minor"},
                    Row{"F#m", "F# minor 3#"}, Row{"Bb", "Bb major 2b"},
                    Row{"A Lydian", "A lydian 4#"}, Row{"Ebmin", "Eb minor 6b"}}) {
        CAPTURE(row.key);
        const ScoreLoadResult r = abc_of(row.key, "CDEF GABc|cBAG FEDC|CDEF GABc|cBAG FEDC|");
        REQUIRE(r.ok);
        CHECK_FALSE(r.song.keys[0].estimated);
        CHECK(label(r.song.keys[0]) == row.label);
    }
    // An inline [K:] and a K: line in the middle change the key from their bar.
    const ScoreLoadResult r =
        abc_of("D", "DEFG ABcd|[K:G] GABc dedc|\nK:Amin\nABcd efge|[K:Ador]ABcd efge|");
    REQUIRE(r.ok);
    REQUIRE(r.song.keys.size() == 4);
    CHECK(label(r.song.keys[1]) == "G major 1#");
    CHECK(label(r.song.keys[2]) == "A minor");
    CHECK(label(r.song.keys[3]) == "A dorian 1#");
    CHECK(r.song.keys[2].tick == r.song.measures[2].start_tick);
}

TEST_CASE("key: with no key written, or only the C major placeholder, the notes decide") {
    struct Row {
        std::vector<int> notes;
        int tonic;
        KeyMode mode;
    };
    for (const Row& row :
         {Row{kCMajorScale, 0, KeyMode::Major}, Row{kAMinorMelody, 9, KeyMode::Minor},
          Row{kOdeInG, 7, KeyMode::Major}}) {
        const KeyEstimate e = estimate_song_key(song_of(row.notes));
        REQUIRE(e.valid);
        CHECK(e.tonic == row.tonic);
        CHECK(e.mode == row.mode);
    }
    // Eight notes are enough, seven are not; three pitch classes are enough, two are not.
    CHECK(estimate_song_key(song_of({71, 71, 72, 74, 74, 72, 71, 69})).valid);
    CHECK_FALSE(estimate_song_key(song_of({71, 71, 72, 74, 74, 72, 71})).valid);
    CHECK(estimate_song_key(song_of({67, 71, 74, 67, 71, 74, 67, 71, 74})).valid);
    CHECK_FALSE(estimate_song_key(song_of({67, 71, 67, 71, 67, 71, 67, 71, 67, 71})).valid);

    // A MIDI file with no key is estimated, and shown like a written one; too few notes: no key.
    Track none;
    melody(none, kOdeInG);
    none.end();
    const Song s = midi_song(none);
    REQUIRE(s.keys.size() == 1);
    CHECK(s.keys[0].estimated);
    CHECK(label(s.keys[0]) == "G major 1#");
    Track few;
    melody(few, {60, 64, 67});
    few.end();
    CHECK(midi_song(few).keys.empty());

    // A C major placeholder gives way only when C major trails by the margin: from a C major scale
    // played six times to a G major tune played six times, one repeat at a time.
    struct Mix {
        int tunes;
        bool replaced;
    };
    for (Mix mix : {Mix{0, false}, Mix{2, false}, Mix{3, true}, Mix{6, true}}) {
        std::vector<int> pitches;
        for (int round = 0; round < 6; ++round) {
            const std::vector<int>& part = round < mix.tunes ? kOdeInG : kCMajorScale;
            pitches.insert(pitches.end(), part.begin(), part.end());
        }
        Song song = song_of(pitches);
        song.keys.push_back(key_from_signature(0, 0, KeyMode::Major));
        complete_keys(song);
        CAPTURE(mix.tunes);
        REQUIRE(song.keys.size() == 1);
        CHECK(song.keys[0].estimated == mix.replaced);
        CHECK(label(song.keys[0]) == (mix.replaced ? "G major 1#" : "C major"));
    }
    // A written key other than the placeholder is never overruled: F major over a G tune stays.
    Track written;
    written.key_sig(0, -1, false);
    melody(written, kOdeInG);
    written.end();
    const Song f = midi_song(written);
    CHECK_FALSE(f.keys[0].estimated);
    CHECK(label(f.keys[0]) == "F major 1b");
}
