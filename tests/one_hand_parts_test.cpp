// SPDX-License-Identifier: GPL-3.0-or-later
// one_hand: parts (split from one_hand_test.cpp).
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

// ---- No grades, no score

TEST_CASE("a run with nothing graded shows a dash, not 100%, and is not saved") {
    RunRecord none; // nothing graded
    none.hands = HandFilter::Both;
    const std::string line = results_line(none, compare_with_last_attempt({}, none));
    CHECK(line.find("\xE2\x80\x93") != std::string::npos);
    CHECK(line.find("100%") == std::string::npos);

    // A song with only a backing part: keys pressed, nothing to grade.
    std::vector<NoteSpec> notes;
    for (int i = 0; i < 4; ++i) {
        notes.push_back({static_cast<double>(i), 60 + i, 1.0, Hand::Unknown, 2, true});
    }
    SongController c;
    std::int64_t now = kSec;
    c.load(make_loaded(notes), now);
    c.play(now);
    const std::int64_t start = now;
    std::size_t next = 0;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < 4; ++i) {
        script.push_back({start + i * 500 * kMs, key_on(60 + i, start + i * 500 * kMs)});
    }
    std::vector<MidiEvent> frame;
    std::vector<SongSoundEvent> out;
    while (c.state() != TransportState::Ended && now < start + 10 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        out.clear();
        c.take_sound_events(out);
    }
    REQUIRE(c.state() == TransportState::Ended);
    CHECK(c.take_finished_runs().empty());
    CHECK(c.take_ungraded_run());
    CHECK_FALSE(c.take_ungraded_run()); // once
}

// ---- Which parts are hands, in a file with a keyboard part

namespace {

int backing_count(const Song& s) {
    return static_cast<int>(std::count_if(s.notes.begin(), s.notes.end(),
                                          [](const Note& n) { return n.accompaniment; }));
}

} // namespace

TEST_CASE("accompaniment: a piano named Chords, and one named Bass, are hands") {
    // Role words say nothing: the program (0, a piano) decides.
    const Song s = load_split(etude::test::smf(
        {line_track(60, 72, 0, 240, 0, "Chords"), line_track(36, 48, 1, 240, 0, "Bass")}));
    CHECK(backing_count(s) == 0);
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 13);
}

TEST_CASE("accompaniment: a harmonica beside a piano is backing, an unnamed celesta is not") {
    const Song harmonica = load_split(
        etude::test::smf({line_track(60, 72, 0, 240, 0), line_track(48, 60, 1, 240, 22)}));
    CHECK(backing_count(harmonica) == 13);
    for (const Note& n : harmonica.notes) {
        CHECK(n.accompaniment == (n.channel == 1));
    }
    // Celesta is program 8: a keyboard, so both lines stay hands.
    const Song celesta = load_split(
        etude::test::smf({line_track(60, 72, 0, 240, 0), line_track(36, 48, 1, 240, 8)}));
    CHECK(backing_count(celesta) == 0);
    CHECK(count_hand(celesta, Hand::Right) == 13);
    CHECK(count_hand(celesta, Hand::Left) == 13);
}

TEST_CASE("accompaniment: organ 19 and harpsichord 6 are keyboards, beside a voice") {
    const Song s = load_split(
        etude::test::smf({line_track(60, 72, 0, 240, 19), line_track(36, 48, 1, 240, 6),
                            line_track(55, 67, 2, 240, 0, "Voice")}));
    for (const Note& n : s.notes) {
        CHECK(n.accompaniment == (n.channel == 2));
    }
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 13);
}

TEST_CASE(
    "accompaniment: a Melody synth lead beside a piano is backing; a voice on program 0 too") {
    const Song lead = load_split(etude::test::smf(
        {line_track(60, 72, 0, 240, 0), line_track(60, 72, 1, 240, 80, "Melody")}));
    for (const Note& n : lead.notes) {
        CHECK(n.accompaniment == (n.channel == 1));
    }
    // A MuseScore export keeps program 0 on a voice: the name still says voice.
    const Song voice = load_split(etude::test::smf(
        {line_track(60, 72, 0, 240, 0), line_track(60, 72, 1, 240, 0, "Voice")}));
    for (const Note& n : voice.notes) {
        CHECK(n.accompaniment == (n.channel == 1));
    }
}

TEST_CASE("accompaniment: a file with no keyboard part splits all its parts as before") {
    const Song s = load_split(etude::test::smf(
        {line_track(60, 72, 0, 240, 73, "Melody"), line_track(36, 48, 1, 240, 33, "Chords")}));
    CHECK(backing_count(s) == 0);
    CHECK(count_hand(s, Hand::Right) == 13);
    CHECK(count_hand(s, Hand::Left) == 13);
}

// ---- Keyboard fit, no accuracy

TEST_CASE("the key range is made of the notes to play, not the backing part") {
    std::vector<NoteSpec> notes = right_only_notes(); // 60..74
    notes.push_back({0, 21, 1.0, Hand::Unknown, 2, true});
    notes.push_back({1, 120, 1.0, Hand::Unknown, 2, true});
    SongController c;
    c.load(make_loaded(notes), kSec);
    const std::optional<KeyRange> range = c.key_range();
    REQUIRE(range.has_value());
    CHECK(range->lowest == 60);
    CHECK(range->highest == 74);
}

TEST_CASE("a run record with nothing graded has no accuracy") {
    RunRecord none;
    CHECK_FALSE(none.accuracy(HandFilter::Both).has_value());
    CHECK_FALSE(none.accuracy(HandFilter::Left).has_value());
    CHECK_FALSE(none.accuracy(HandFilter::Right).has_value());
    none.all.graded = 4;
    none.all.hit = 3;
    REQUIRE(none.accuracy(HandFilter::Both).has_value());
    CHECK(*none.accuracy(HandFilter::Both) == doctest::Approx(0.75));
    CHECK_FALSE(none.accuracy(HandFilter::Left).has_value()); // the left hand graded none
}
