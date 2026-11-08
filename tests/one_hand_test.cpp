// SPDX-License-Identifier: GPL-3.0-or-later
// Songs with one hand's notes, single melodic lines and accompaniment parts: which hands a song
// has, where a line goes, what is a practice hand, and what the controller does with them.
#include "song_controller.hpp"
#include "song_transport.hpp"
#include "etude/hand_split.hpp"
#include "etude/hands_present.hpp"
#include "etude/instrument.hpp"
#include "etude/library.hpp"
#include "etude/midi_file.hpp"
#include "etude/musicxml.hpp"
#include "etude/song_builder.hpp"
#include "smf_builder.hpp"
#include <doctest/doctest.h>
#include <algorithm>
#include <array>
#include <string>
#include <utility>
#include <vector>

#include "one_hand_test_support.hpp"

using namespace etude;
using namespace etude::app;
using etude::test::Track;
using namespace one_hand_test_support;

// ---- MIDI

// ---- MusicXML

// ---- MuseScore

// ---- Controller

// A note of a probe track: start tick, ticks held, pitch.
struct Ev {
    int start;
    int hold;
    int pitch;
};

Track events_track(const std::vector<Ev>& notes, int channel = 0) {
    std::vector<std::array<int, 3>> events; // tick, on (1) or off (0), pitch
    for (const Ev& n : notes) {
        events.push_back({n.start, 1, n.pitch});
        events.push_back({n.start + n.hold, 0, n.pitch});
    }
    std::stable_sort(events.begin(), events.end(), [](const auto& a, const auto& b) {
        return a[0] != b[0] ? a[0] < b[0] : a[1] < b[1]; // an off before an on at one tick
    });
    Track t;
    int at = 0;
    for (const auto& e : events) {
        const auto delta = static_cast<std::uint32_t>(e[0] - at);
        at = e[0];
        e[1] == 1 ? t.on(delta, channel, e[2]) : t.off(delta, channel, e[2]);
    }
    return t.end();
}

// A line that wanders over middle C, 55..67 and back, one note every 240 ticks (a quarter at
// 120 BPM), `count` notes, held `hold` ticks.
std::vector<Ev> wander(int count, int hold, int first_tick = 0) {
    std::vector<Ev> out;
    for (int i = 0; i < count; ++i) {
        const int step = i % 24;
        out.push_back({first_tick + i * 240, hold, 55 + (step <= 12 ? step : 24 - step)});
    }
    return out;
}

// ---- Instruments

namespace {

InstrumentKind kind_of(const char* name, const char* instrument, const char* id, int program) {
    Part p;
    p.name = name;
    p.instrument_name = instrument;
    p.instrument_id = id;
    p.has_program = program >= 0;
    p.program = static_cast<std::uint8_t>(std::max(program, 0));
    return classify_instrument(p);
}

} // namespace

TEST_CASE("instrument kinds: clear names, ids and sounds say keyboard or other") {
    CHECK(kind_of("Piano", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("Klavier", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("Pianoforte", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("Organ", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("Harpsichord", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("Celesta", "", "", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("", "", "piano", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("", "", "grand-piano", -1) == InstrumentKind::Keyboard);
    CHECK(kind_of("", "", "keyboard.piano", -1) == InstrumentKind::Keyboard);
    for (const char* name :
         {"Voice", "Vocals", "Singstimme", "Soprano",   "Alto",  "Tenor",   "Baritone", "Violin I",
          "Viola", "Cello",  "Flute",      "Clarinet",  "Sax",   "Trumpet", "Guitar",   "Strings",
          "Choir", "Chorus", "Accordion",  "Harmonica", "VOICE", "voice 2", "Voice1"}) {
        CAPTURE(name);
        CHECK(kind_of(name, "", "", -1) == InstrumentKind::Other);
        CHECK(kind_of(name, "", "", 0) == InstrumentKind::Other); // beats a piano program
    }
    CHECK(kind_of("", "", "voice", -1) == InstrumentKind::Other);
    CHECK(kind_of("", "", "voice.soprano", -1) == InstrumentKind::Other);
    CHECK(kind_of("Part", "", "voice", 0) == InstrumentKind::Other);
    CHECK(kind_of("Part", "Violin", "", 0) == InstrumentKind::Other);
}

TEST_CASE("instrument kinds: names are whole words, so Chords is not Choir or Chorus") {
    for (const char* name : {"Chords", "Chord", "Chordal", "Sharp", "Altitude", "Voiceover",
                             "Saxony", "Stringent", "Pianola"}) {
        CAPTURE(name);
        CHECK(kind_of(name, "", "", -1) == InstrumentKind::Unknown);
    }
    CHECK(kind_of("Chords", "", "", 0) == InstrumentKind::Keyboard);
}

TEST_CASE("instrument kinds: role words say nothing, the program decides") {
    for (const char* role : {"Melody", "Chords", "Bass", "Lead", "Right", "Left", "RH", "LH",
                             "Treble", "Accomp", "Accompaniment", "Track 3", "Track3", ""}) {
        CAPTURE(role);
        CHECK(kind_of(role, "", "", -1) == InstrumentKind::Unknown);
        CHECK(kind_of(role, "", "", 0) == InstrumentKind::Keyboard);
        CHECK(kind_of(role, "", "", 19) == InstrumentKind::Keyboard);
        CHECK(kind_of(role, "", "", 80) == InstrumentKind::Other); // a synth lead
        CHECK(kind_of(role, "", "", 52) == InstrumentKind::Other); // a choir
    }
    // The program wins over a keyboard word that is only a name.
    CHECK(kind_of("Piano", "", "", 52) == InstrumentKind::Other);
}

TEST_CASE("instrument kinds: General MIDI programs 0-8 and 16-20 are keyboards") {
    for (int program = 0; program < 128; ++program) {
        const bool keyboard = program <= 8 || (program >= 16 && program <= 20);
        CAPTURE(program);
        CHECK(kind_of("", "", "", program) ==
              (keyboard ? InstrumentKind::Keyboard : InstrumentKind::Other));
    }
    // Pianos, harpsichord (6), clavinet (7), celesta (8), organs (16..20).
    CHECK(kind_of("", "", "", 6) == InstrumentKind::Keyboard);
    CHECK(kind_of("", "", "", 8) == InstrumentKind::Keyboard);
    CHECK(kind_of("", "", "", 19) == InstrumentKind::Keyboard);
    // Accordion 21, harmonica 22 and tango accordion 23 are not keyboards.
    for (int program : {21, 22, 23}) {
        CHECK(kind_of("", "", "", program) == InstrumentKind::Other);
    }
    CHECK(classify_instrument(Part{}) == InstrumentKind::Unknown); // no program change at all
}

TEST_CASE("accompaniment: another instrument is a backing part only beside a keyboard part") {
    Song song;
    Part piano;
    piano.name = "Piano";
    piano.track = 0;
    Part voice;
    voice.name = "Voice";
    voice.track = 1;
    song.parts = {voice};
    CHECK_FALSE(has_keyboard_part(song));
    CHECK_FALSE(is_accompaniment_part(song, voice)); // no keyboard in the file: split as before
    song.parts = {piano, voice};
    CHECK(has_keyboard_part(song));
    CHECK(is_accompaniment_part(song, voice));
    CHECK_FALSE(is_accompaniment_part(song, piano));
    Part drums;
    drums.drum = true;
    drums.has_program = true;
    drums.program = 40;
    CHECK_FALSE(is_accompaniment_part(song, drums)); // drums have their own handling
}

// ---- A single line stays in one hand

TEST_CASE("single line: a melody 60-72 goes whole to the right hand") {
    const Song s = load_split(etude::test::smf({line_track(60, 72)}));
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 0);
    CHECK_FALSE(needs_hand_split(s));
}

TEST_CASE("single line: a melody 36-48 goes whole to the left hand") {
    const Song s = load_split(etude::test::smf({line_track(36, 48)}));
    CHECK(count_hand(s, Hand::Left) == 13);
    CHECK(count_hand(s, Hand::Right) == 0);
}

TEST_CASE("single line: a melody 55-67 across middle C is not shared between the hands") {
    const Song s = load_split(etude::test::smf({line_track(55, 67)}));
    // Median 61, at or above G3 (55): all right hand. The pitch split gave 6 left, 10 right.
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 0);
    // Both methods keep the line whole.
    MidiLoadResult r = load_midi(etude::test::smf({line_track(55, 67)}));
    REQUIRE(r.ok);
    for (SplitMethod m : {SplitMethod::Fixed, SplitMethod::Smart}) {
        HandSplitOptions o;
        o.method = m;
        for (Hand h : split_hands(r.song, o)) {
            CHECK(h == Hand::Right);
        }
    }
}

TEST_CASE("single line: a melody across a drawn split point is cut by it, under Auto it is whole") {
    MidiLoadResult r = load_midi(etude::test::smf({line_track(55, 67)}));
    REQUIRE(r.ok);
    for (const Hand h : split_hands(r.song, options_for_split_point(kAutoSplit))) {
        CHECK(h == Hand::Right);
    }
    int left = 0;
    int right = 0;
    const std::vector<Hand> cut = split_hands(r.song, options_for_split_point(62));
    for (std::size_t i = 0; i < cut.size(); ++i) {
        const bool below = r.song.notes[i].pitch < 62;
        CHECK(cut[i] == (below ? Hand::Left : Hand::Right));
        left += cut[i] == Hand::Left ? 1 : 0;
        right += cut[i] == Hand::Right ? 1 : 0;
    }
    CHECK(left == 7);  // 55..61
    CHECK(right == 6); // 62..67
}

TEST_CASE("single line: the median decides, G3 and up is right, a note under it is left") {
    // 48..61: median 54.5, under 55.
    const Song low = load_split(etude::test::smf({line_track(48, 61)}));
    CHECK(count_hand(low, Hand::Left) == 14);
    // 49..61: median 55.
    const Song edge = load_split(etude::test::smf({line_track(49, 61)}));
    CHECK(count_hand(edge, Hand::Right) == 13);
}

TEST_CASE("single line: a note held into the next never counts, however long") {
    // Each note held 10 ms, then 100 ms, then two whole beats into the next ones: legato or pedal,
    // the onsets are a quarter apart.
    for (int hold : {250, 336, 960}) {
        CAPTURE(hold);
        const Song s = load_split(etude::test::smf({line_track(55, 67, 0, hold)}));
        CHECK(count_hand(s, Hand::Right) == 13);
        CHECK(count_hand(s, Hand::Left) == 0);
    }
}

namespace {

// Singles wandering 58..64, one a quarter; at the `chords` positions (onset numbers) the onset
// is a wide chord instead (40 and 76), which the pitch split cuts between the hands.
Song with_chords(int onsets, std::initializer_list<int> chords) {
    std::vector<Ev> notes;
    for (int i = 0; i < onsets; ++i) {
        const bool chord = std::find(chords.begin(), chords.end(), i) != chords.end();
        if (chord) {
            notes.push_back({i * 240, 200, 40});
            notes.push_back({i * 240, 200, 76});
        } else {
            notes.push_back({i * 240, 200, 58 + i % 7});
        }
    }
    return load_split(etude::test::smf({events_track(notes)}));
}

} // namespace

TEST_CASE("single line: exactly 10% chord onsets is a line, just over is split") {
    static_assert(kSingleLineChordFraction == 0.10);
    // 20 onsets, 2 of them chords: 10%.
    const Song exact = with_chords(20, {5, 14});
    CHECK(count_hand(exact, Hand::Left) == 0);
    CHECK(count_hand(exact, Hand::Right) == 22);
    // 19 onsets, 2 of them chords: 10.5%. The chords are cut between the hands.
    const Song over = with_chords(19, {5, 13});
    CHECK(count_hand(over, Hand::Left) >= 2);
    CHECK(count_hand(over, Hand::Right) > 2);
    // One chord in 11 onsets is 9.1%, in 9 onsets 11.1%.
    CHECK(count_hand(with_chords(11, {5}), Hand::Left) == 0);
    CHECK(count_hand(with_chords(9, {4}), Hand::Left) >= 1);
}

TEST_CASE("single line: a melody with a closing 4-note chord is one line") {
    std::vector<Ev> notes = wander(12, 200);
    for (int p : {57, 60, 64, 67}) {
        notes.push_back({12 * 240, 480, p});
    }
    const Song s = load_split(etude::test::smf({events_track(notes)}));
    CHECK(count_hand(s, Hand::Right) == 16);
    CHECK(count_hand(s, Hand::Left) == 0);
}

TEST_CASE("single line: an Alberti or stride pattern that leaps is two hands, not a line") {
    static_assert(kInterleavedLeapFraction == 0.25);
    // Alberti across the split: low, high, middle, high with leaps of 16 and 9 semitones.
    std::vector<Ev> alberti;
    for (int i = 0; i < 32; ++i) {
        const int pitch = std::array<int, 4>{48, 64, 55, 64}[static_cast<std::size_t>(i % 4)];
        alberti.push_back({i * 120, 110, pitch});
    }
    const Song a = load_split(etude::test::smf({events_track(alberti)}));
    CHECK(count_hand(a, Hand::Left) > 0);
    CHECK(count_hand(a, Hand::Right) > 0);
    // Stride: a bass note, then a note two octaves up, again and again.
    std::vector<Ev> stride;
    for (int i = 0; i < 16; ++i) {
        stride.push_back({i * 240, 200, i % 2 == 0 ? 36 + i : 64 + i});
    }
    const Song t = load_split(etude::test::smf({events_track(stride)}));
    CHECK(count_hand(t, Hand::Left) > 0);
    CHECK(count_hand(t, Hand::Right) > 0);
    // A line that leaps now and then (an octave and a bit once in eight steps) stays a line.
    std::vector<Ev> leaping = wander(40, 200);
    leaping[10].pitch = 76;
    leaping[20].pitch = 40;
    const Song l = load_split(etude::test::smf({events_track(leaping)}));
    CHECK(count_hand(l, Hand::Left) == 0);
    CHECK(count_hand(l, Hand::Right) == 40);
}

TEST_CASE("single line: three cases a review found are one hand each") {
    // A: 65 single notes and a closing 4-note chord (69 onsets). Before: L15 R54.
    std::vector<Ev> a = wander(65, 200);
    for (int p : {57, 60, 64, 67}) {
        a.push_back({65 * 240, 480, p});
    }
    // B: 19 notes, one held 100 ms (96 ticks) into the next. Before: L9 R10.
    std::vector<Ev> b = wander(19, 200);
    b[9].hold = 240 + 96;
    // C: 60 notes, each held 100 ms into the next. Before: L15 R45.
    const std::vector<Ev> c = wander(60, 240 + 96);
    for (const auto& [notes, total] :
         {std::pair<const std::vector<Ev>&, int>{a, 69}, {b, 19}, {c, 60}}) {
        const Song s = load_split(etude::test::smf({events_track(notes)}));
        CHECK(static_cast<int>(s.notes.size()) == total);
        CHECK(count_hand(s, Hand::Right) == total);
        CHECK(count_hand(s, Hand::Left) == 0);
    }
}

TEST_CASE("single line: each part is judged alone, a line beside a two-hand part") {
    // Track 0: a bass line 36-48. Track 1: a melody 72-84. Unnamed: each is one line.
    const Song s = load_split(etude::test::smf({line_track(36, 48, 0), line_track(72, 84, 1)}));
    CHECK(count_hand(s, Hand::Left) == 13);
    CHECK(count_hand(s, Hand::Right) == 13);
    for (const Note& n : s.notes) {
        CHECK(n.hand == (n.pitch < 60 ? Hand::Left : Hand::Right));
    }
}

TEST_CASE("single line: a part named for a hand keeps that hand, whatever its pitch") {
    const Song s = load_split(etude::test::smf({line_track(36, 48, 0, 240, -1, "Right Hand")}));
    CHECK(count_hand(s, Hand::Right) == 13);
}
