// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/song.hpp"

#include <doctest/doctest.h>

using namespace etude;

TEST_CASE("hand from a track or channel name") {
    for (const char* name : {"Right", "RH", "rh", "R.H.", "Piano R", "Piano RH", "Right Hand",
                             "right-hand", "Piano (right)", "Treble", "Upper staff"}) {
        CAPTURE(name);
        CHECK(hand_from_name(name) == Hand::Right);
    }
    for (const char* name :
         {"Left", "LH", "L.H.", "Piano L", "Piano: Left Hand", "lefthand", "Bass", "Lower"}) {
        CAPTURE(name);
        CHECK(hand_from_name(name) == Hand::Left);
    }
    for (const char* name : {"", "Piano", "Track 1", "Melody", "Strings", "Rhodes", "Lead",
                             "Leftover", "R", "Right and Left", "Piano RH+LH", "Bright Piano"}) {
        CAPTURE(name);
        CHECK(hand_from_name(name) == Hand::Unknown);
    }
}

namespace {
Song two_part_song(const char* name0, const char* name1) {
    Song song;
    Part a;
    a.track = 1;
    a.channel = 0;
    a.name = name0;
    Part b;
    b.track = 2;
    b.channel = 0;
    b.name = name1;
    song.parts = {a, b};
    Note n0;
    n0.track = 1;
    Note n1;
    n1.track = 2;
    song.notes = {n0, n1, n0};
    return song;
}
} // namespace

TEST_CASE("hands from names set each part and its notes") {
    Song song = two_part_song("Piano RH", "Piano LH");
    assign_hands_from_names(song);
    CHECK(song.parts[0].hand == Hand::Right);
    CHECK(song.parts[1].hand == Hand::Left);
    CHECK(song.notes[0].hand == Hand::Right);
    CHECK(song.notes[1].hand == Hand::Left);
    CHECK(song.notes[2].hand == Hand::Right);
}

TEST_CASE("hands from names never guess: unclear names stay Unknown") {
    Song song = two_part_song("Piano", "Track 2");
    song.notes[0].hand = Hand::Right; // stale value gets cleared
    assign_hands_from_names(song);
    CHECK(song.parts[0].hand == Hand::Unknown);
    CHECK(song.parts[1].hand == Hand::Unknown);
    CHECK(song.notes[0].hand == Hand::Unknown);
}

TEST_CASE("hands from names fall back to the instrument name") {
    Song song = two_part_song("Track 1", "Track 2");
    song.parts[0].instrument_name = "Piano Right";
    assign_hands_from_names(song);
    CHECK(song.parts[0].hand == Hand::Right);
    CHECK(song.parts[1].hand == Hand::Unknown);
}

TEST_CASE("hands from names skip drums and bass guitars") {
    Song song = two_part_song("Left", "Bass");
    song.parts[0].drum = true;
    song.parts[0].channel = 9;
    song.notes[0].drum = true;
    song.notes[0].channel = 9;
    song.notes[2].drum = true;
    song.notes[2].channel = 9;
    song.parts[1].program = 33; // Electric Bass (finger)
    song.parts[1].has_program = true;
    assign_hands_from_names(song);
    CHECK(song.parts[0].hand == Hand::Unknown);
    CHECK(song.notes[0].hand == Hand::Unknown);
    CHECK(song.parts[1].hand == Hand::Unknown);

    SUBCASE("a piano part named Bass is the left hand") {
        song.parts[1].program = 0;
        assign_hands_from_names(song);
        CHECK(song.parts[1].hand == Hand::Left);
        CHECK(song.notes[1].hand == Hand::Left);
    }
}

TEST_CASE("bar number at a tick") {
    Song song;
    song.measures = {{0, 0, 0.0, 1, 4}, {1, 480, 0.5, 4, 4}, {2, 2400, 2.5, 4, 4}};
    CHECK(song.measure_at(-5) == 0);
    CHECK(song.measure_at(0) == 0);
    CHECK(song.measure_at(479) == 0);
    CHECK(song.measure_at(480) == 1);
    CHECK(song.measure_at(2399) == 1);
    CHECK(song.measure_at(100000) == 2);
}

TEST_CASE("song_fingerprint: equal for copies, different when anything practice or playback reads differs") {
    Song song;
    for (int i = 0; i < 4; ++i) {
        Note n;
        n.start_s = 0.5 * i;
        n.duration_s = 0.4;
        n.pitch = static_cast<std::uint8_t>(60 + i);
        n.velocity = 80;
        n.channel = 1;
        n.hand = Hand::Right;
        n.measure = 1 + i / 2;
        song.notes.push_back(n);
    }
    Measure m1;
    Measure m2;
    m2.number = 2;
    m2.start_s = 1.0;
    song.measures = {m1, m2};
    ChannelEvent e;
    e.time_s = 0.1;
    e.channel = 1;
    e.kind = ChannelEventKind::Control;
    e.number = 64;
    e.value = 127;
    song.events.push_back(e);
    const std::uint64_t base = song_fingerprint(song);
    CHECK(song_fingerprint(Song{song}) == base);
    CHECK(song_fingerprint(Song{}) != base);

    const auto differs = [&](auto change) {
        Song other = song;
        change(other);
        return song_fingerprint(other) != base;
    };
    CHECK(differs([](Song& s) { s.notes[2].start_s += 0.001; }));
    CHECK(differs([](Song& s) { s.notes[2].duration_s += 0.001; }));
    CHECK(differs([](Song& s) { s.notes[2].pitch += 1; }));
    CHECK(differs([](Song& s) { s.notes[2].velocity += 1; }));
    CHECK(differs([](Song& s) { s.notes[2].channel += 1; }));
    CHECK(differs([](Song& s) { s.notes[2].hand = Hand::Left; }));
    CHECK(differs([](Song& s) { s.notes[2].measure += 1; }));
    CHECK(differs([](Song& s) { s.notes[2].drum = true; }));
    CHECK(differs([](Song& s) { s.notes[2].accompaniment = true; }));
    CHECK(differs([](Song& s) { s.measures[1].start_s += 0.5; }));
    CHECK(differs([](Song& s) { s.measures[1].number = 7; }));
    CHECK(differs([](Song& s) { s.events[0].value = 100; }));
    CHECK(differs([](Song& s) { s.events[0].time_s = 0.2; }));
    CHECK(differs([](Song& s) { s.events[0].channel = 2; }));
    CHECK(differs([](Song& s) { s.events[0].number = 65; }));
    CHECK(differs([](Song& s) { s.events[0].kind = ChannelEventKind::Program; }));
    CHECK(differs([](Song& s) { s.events.push_back(s.events[0]); }));
    CHECK(differs([](Song& s) { s.notes.pop_back(); }));
    // Order matters: two songs with the same notes the other way round are two songs.
    CHECK(differs([](Song& s) { std::swap(s.notes[0], s.notes[1]); }));
}
