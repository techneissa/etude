// SPDX-License-Identifier: GPL-3.0-or-later
// one_hand: accompaniment (split from one_hand_test.cpp).
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

namespace {

std::string xml_note(const char* step, int octave, int staff = 1) {
    return std::string("<note><pitch><step>") + step + "</step><octave>" + std::to_string(octave) +
           "</octave></pitch><duration>4</duration><voice>" + std::to_string(staff) +
           "</voice><staff>" + std::to_string(staff) + "</staff></note>";
}

// A piano part (two staves) and one more part named `other_name` with `other_extra` in its
// score-part (an instrument-sound, a midi program).
std::string piano_and(const std::string& other_name, const std::string& other_extra) {
    const std::string attrs =
        "<attributes><divisions>1</divisions><time><beats>4</beats><beat-type>4</beat-type></time>";
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<score-partwise version=\"3.1\"><part-list>"
           "<score-part id=\"P1\"><part-name>Klavier</part-name></score-part>"
           "<score-part id=\"P2\"><part-name>" +
           other_name + "</part-name>" + other_extra +
           "</score-part></part-list>"
           "<part id=\"P1\"><measure number=\"1\">" +
           attrs + "<staves>2</staves></attributes>" + xml_note("E", 5, 1) +
           "<backup><duration>4</duration></backup>" + xml_note("C", 3, 2) +
           "</measure></part><part id=\"P2\"><measure number=\"1\">" + attrs + "</attributes>" +
           xml_note("G", 4) + "</measure></part></score-partwise>";
}

ScoreLoadResult load_xml(const std::string& xml) {
    return load_musicxml(reinterpret_cast<const std::uint8_t*>(xml.data()), xml.size());
}

std::string ms_staff_body(int pitch) {
    return "<Measure><voice><TimeSig><sigN>4</sigN><sigD>4</sigD></TimeSig><Chord><durationType>"
           "whole</durationType><Note><pitch>" +
           std::to_string(pitch) + "</pitch><tpc>14</tpc></Note></Chord></voice></Measure>";
}

std::string ms_staff_type(int id) {
    return "<Staff id=\"" + std::to_string(id) +
           "\"><StaffType group=\"pitched\"><name>stdNormal</name></StaffType></Staff>";
}

// A piano part with two staves, then one more part with the instrument id `other_id`.
std::string mscx_piano_and(const std::string& other_id) {
    return "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<museScore version=\"3.02\"><Score>"
           "<Division>480</Division><Part>" +
           ms_staff_type(1) + ms_staff_type(2) +
           "<trackName>Piano</trackName><Instrument id=\"piano\"><Channel><program value=\"0\"/>"
           "</Channel></Instrument></Part><Part>" +
           ms_staff_type(3) + "<trackName>Part 2</trackName><Instrument id=\"" + other_id +
           "\"><Channel><program value=\"0\"/></Channel></Instrument></Part><Staff id=\"1\">" +
           ms_staff_body(76) + "</Staff><Staff id=\"2\">" + ms_staff_body(48) +
           "</Staff><Staff id=\"3\">" + ms_staff_body(67) + "</Staff></Score></museScore>";
}

} // namespace

// ---- Accompaniment

TEST_CASE("accompaniment: a voice part beside a two-staff piano is not a hand (MusicXML)") {
    // The name says voice, the program says piano (an exporter's default): the name wins.
    const ScoreLoadResult r =
        load_xml(piano_and("Singstimme", "<midi-instrument id=\"x\"><midi-program>1"
                                         "</midi-program></midi-instrument>"));
    REQUIRE(r.ok);
    const Song s = with_split_hands(r.song);
    REQUIRE(s.notes.size() == 3);
    int backing = 0;
    for (const Note& n : s.notes) {
        if (n.pitch == 67) { // the voice's G4
            CHECK(n.accompaniment);
            CHECK(n.hand == Hand::Unknown);
            ++backing;
        } else {
            CHECK_FALSE(n.accompaniment);
            CHECK(n.hand == (n.pitch == 76 ? Hand::Right : Hand::Left));
        }
    }
    CHECK(backing == 1);
    CHECK(hands_present(s).both());
    CHECK_FALSE(needs_hand_split(s));
}

TEST_CASE("accompaniment: the MusicXML instrument sound and a program name a part too") {
    const auto backing_notes = [](const Song& s) {
        return std::count_if(s.notes.begin(), s.notes.end(),
                             [](const Note& n) { return n.accompaniment; });
    };
    // A generic part name: its instrument-sound says voice.
    const ScoreLoadResult sound =
        load_xml(piano_and("Part 2", "<score-instrument id=\"P2-I1\"><instrument-name>x"
                                     "</instrument-name><instrument-sound>voice.soprano"
                                     "</instrument-sound></score-instrument>"));
    REQUIRE(sound.ok);
    CHECK(std::any_of(sound.song.parts.begin(), sound.song.parts.end(),
                      [](const Part& p) { return p.instrument_id == "voice.soprano"; }));
    CHECK(backing_notes(with_split_hands(sound.song)) == 1);
    // Only a program: 53 (1-based) is a choir.
    const ScoreLoadResult program =
        load_xml(piano_and("Part 2", "<midi-instrument id=\"x\"><midi-program>53</midi-program>"
                                     "</midi-instrument>"));
    REQUIRE(program.ok);
    CHECK(backing_notes(with_split_hands(program.song)) == 1);
    // A second piano is no backing part.
    const ScoreLoadResult pianos = load_xml(piano_and("Piano 2", ""));
    REQUIRE(pianos.ok);
    CHECK(backing_notes(with_split_hands(pianos.song)) == 0);
}

TEST_CASE("accompaniment: a MuseScore instrument id names the part") {
    const std::string with_voice = mscx_piano_and("voice");
    const ScoreLoadResult voice =
        load_mscx(reinterpret_cast<const std::uint8_t*>(with_voice.data()), with_voice.size());
    REQUIRE(voice.ok);
    const Song s = with_split_hands(voice.song);
    REQUIRE(s.notes.size() == 3);
    for (const Note& n : s.notes) {
        CHECK(n.accompaniment == (n.pitch == 67));
    }
    // Another piano is a second keyboard part, not a backing part.
    const std::string two_pianos = mscx_piano_and("piano");
    const ScoreLoadResult pianos =
        load_mscx(reinterpret_cast<const std::uint8_t*>(two_pianos.data()), two_pianos.size());
    REQUIRE(pianos.ok);
    const Song p = with_split_hands(pianos.song);
    CHECK(std::none_of(p.notes.begin(), p.notes.end(),
                       [](const Note& n) { return n.accompaniment; }));
}

TEST_CASE("accompaniment: MIDI programs decide when tracks have no names") {
    // A piano line and a choir line: the choir is backing, the piano line is the one hand.
    const Song s = load_split(
        etude::test::smf({line_track(60, 72, 0, 240, 0), line_track(48, 60, 1, 240, 52)}));
    for (const Note& n : s.notes) {
        CHECK(n.accompaniment == (n.channel == 1));
        CHECK(n.hand == (n.channel == 1 ? Hand::Unknown : Hand::Right));
    }
    const HandsPresent present = hands_present(s);
    CHECK(present.right);
    CHECK_FALSE(present.left);
    // Two string tracks and no keyboard: nothing is backing, both lines are hands.
    const Song strings = load_split(
        etude::test::smf({line_track(60, 72, 0, 240, 40), line_track(36, 48, 1, 240, 41)}));
    CHECK(std::none_of(strings.notes.begin(), strings.notes.end(),
                       [](const Note& n) { return n.accompaniment; }));
    CHECK(count_hand(strings, Hand::Right) == 13);
    CHECK(count_hand(strings, Hand::Left) == 13);
}

TEST_CASE("accompaniment: an unnamed two-track song keeps both tracks as hands") {
    const etude::test::Bytes bytes =
        etude::test::smf({line_track(60, 72, 0), line_track(36, 48, 1)});
    const Song s = load_split(bytes);
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 13);
    CHECK(hands_present(s).both());
    // The library entry is made after the split: not "0 left, 0 right".
    const LibraryLoad lib = load_library_entry(bytes, "two.mid");
    REQUIRE(lib.ok);
    CHECK(lib.entry.left_notes == 13);
    CHECK(lib.entry.right_notes == 13);
    CHECK(lib.entry.unknown_notes == 0);
    CHECK(lib.entry.hands_known());
}

TEST_CASE("accompaniment: library counts leave the backing part out") {
    const etude::test::Bytes bytes =
        etude::test::smf({line_track(60, 72, 0, 240, 0), line_track(48, 60, 1, 240, 52)});
    const LibraryLoad lib = load_library_entry(bytes, "voice.mid");
    REQUIRE(lib.ok);
    CHECK(lib.entry.note_count == 13);
    CHECK(lib.entry.right_notes == 13);
    CHECK(lib.entry.left_notes == 0);
    CHECK(lib.entry.highest_pitch == 72);
    CHECK(lib.entry.lowest_pitch == 60);
}

// ---- Which hands a song has

TEST_CASE("hands present: the hands with playable notes, per song and per stretch") {
    const LoadedSong both = make_loaded(
        {{0, 72, 1, Hand::Right, 0}, {0, 48, 1, Hand::Left, 1}, {4, 74, 1, Hand::Right, 0}});
    CHECK(hands_present(both.song) == HandsPresent{true, true});
    // The left hand rests after the first beat.
    CHECK(hands_present(both.song, 2.0, 100.0) == HandsPresent{false, true});
    CHECK(hands_present(both.song, 0.0, 0.5) == HandsPresent{true, true});
    // Right only, drums and backing notes do not count as a left hand.
    LoadedSong right = make_loaded({{0, 72, 1, Hand::Right, 0},
                                    {0, 40, 1, Hand::Unknown, 2, true},
                                    {1, 36, 1, Hand::Unknown, 9}});
    CHECK(hands_present(right.song) == HandsPresent{false, true});
    // A song with nothing to play, or notes with no hand, locks nothing.
    CHECK(hands_present(Song{}) == HandsPresent{true, true});
    CHECK(hands_present(make_loaded({{0, 60, 1, Hand::Unknown, 0}}).song) ==
          HandsPresent{true, true});
}

TEST_CASE("hand toggles: the state model with both hands and with one") {
    const HandsPresent both{true, true};
    const HandsPresent right_only{false, true};
    const HandsPresent left_only{true, false};

    HandToggles t = hand_toggles_for(HandFilter::Both, both);
    CHECK(t.left_on);
    CHECK(t.right_on);
    CHECK(t.left_enabled);
    CHECK(t.right_enabled);
    CHECK(t.both_offered);
    CHECK(t.other_hand_sound_offered);

    // Right only: the left toggle is unavailable, the right hand is on and cannot be turned off,
    // "both" and the other hand's sound are not offered. Whatever was chosen before.
    for (HandFilter chosen : {HandFilter::Both, HandFilter::Left, HandFilter::Right}) {
        t = hand_toggles_for(chosen, right_only);
        CHECK_FALSE(t.left_on);
        CHECK(t.right_on);
        CHECK_FALSE(t.left_enabled);
        CHECK(t.right_enabled);
        CHECK_FALSE(t.both_offered);
        CHECK_FALSE(t.other_hand_sound_offered);
        CHECK(toggle_hand(chosen, right_only, true) == HandFilter::Right);  // the missing hand
        CHECK(toggle_hand(chosen, right_only, false) == HandFilter::Right); // the last one on
    }
    t = hand_toggles_for(HandFilter::Both, left_only);
    CHECK(t.left_on);
    CHECK_FALSE(t.right_on);
    CHECK_FALSE(t.right_enabled);
    CHECK(toggle_hand(HandFilter::Right, left_only, false) == HandFilter::Left);

    // Two hands: a tap flips its hand, and the last hand on stays.
    CHECK(toggle_hand(HandFilter::Both, both, true) == HandFilter::Right);
    CHECK(toggle_hand(HandFilter::Both, both, false) == HandFilter::Left);
    CHECK(toggle_hand(HandFilter::Right, both, true) == HandFilter::Both);
    CHECK(toggle_hand(HandFilter::Right, both, false) == HandFilter::Right);
    CHECK(toggle_hand(HandFilter::Left, both, true) == HandFilter::Left);
    CHECK(clamp_hands(HandFilter::Left, both) == HandFilter::Left);
}
