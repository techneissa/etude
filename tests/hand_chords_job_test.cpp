// SPDX-License-Identifier: GPL-3.0-or-later
// The worker that names each hand's chords (app/hand_chords_job.hpp): off the frame thread, over
// the notes the evaluators hold, the newest request winning, a transposition named again from the
// kept harmony; and the feed the frame loop keeps of it.
#include "hand_chords_job.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <memory>
#include <vector>

using namespace etude;
using namespace etude::app;

namespace {

constexpr std::int64_t kQuarter = 480;

// Two bars in G: the left hand G B D then D F# A, the right hand A C D F# in the second.
struct Tune {
    Song song;
    FlowEvaluator::NoteTable notes;
    FlowEvaluator::MeasureTable measures;

    Tune() {
        song.tempo.ticks_per_quarter = static_cast<int>(kQuarter);
        for (int b = 0; b < 2; ++b) {
            Measure m;
            m.number = b + 1;
            m.start_tick = b * 4 * kQuarter;
            m.start_s = static_cast<double>(m.start_tick) / (2.0 * kQuarter);
            song.measures.push_back(m);
        }
        song.end_tick = 8 * kQuarter;
        song.duration_s = 8.0 * kQuarter / (2.0 * kQuarter);
        song.keys.push_back(key_from_signature(0, 1, KeyMode::Major));
        twice(0, {43, 47, 50}, Hand::Left);
        twice(1, {50, 54, 57}, Hand::Left);
        twice(1, {57, 60, 62, 66}, Hand::Right);
        std::stable_sort(song.notes.begin(), song.notes.end(), [](const Note& a, const Note& b) {
            return a.start_tick != b.start_tick ? a.start_tick < b.start_tick : a.pitch < b.pitch;
        });
        notes = std::make_shared<const std::vector<Note>>(song.notes);
        measures = std::make_shared<const std::vector<Measure>>(song.measures);
    }
    void twice(int bar, std::initializer_list<int> pitches, Hand hand) {
        for (const double beat : {0.0, 2.0}) {
            for (const int p : pitches) {
                Note n;
                n.start_tick = bar * 4 * kQuarter + static_cast<std::int64_t>(beat * kQuarter);
                n.end_tick = n.start_tick + static_cast<std::int64_t>(1.9 * kQuarter);
                n.start_s = static_cast<double>(n.start_tick) / (2.0 * kQuarter);
                n.duration_s = static_cast<double>(n.end_tick - n.start_tick) / (2.0 * kQuarter);
                n.pitch = static_cast<std::uint8_t>(p);
                n.hand = hand;
                n.measure = bar + 1;
                song.notes.push_back(n);
            }
        }
    }
    HandChordsRequest request(std::uint64_t serial, int semitones = 0, bool flats = false) const {
        HandChordsRequest r;
        r.serial = serial;
        r.notes = notes;
        r.measures = measures;
        r.tempo = song.tempo;
        r.keys = song.keys;
        r.end_tick = song.end_tick;
        r.duration_s = song.duration_s;
        r.semitones = semitones;
        r.prefer_flats = flats;
        return r;
    }
};

} // namespace

TEST_CASE("hand chords job: the worker names the hands as the core does") {
    const Tune tune;
    HandChordsJob job;
    job.request(tune.request(7));
    job.wait_idle();
    const std::optional<HandChordsResult> answer = job.take();
    REQUIRE(answer.has_value());
    CHECK(answer->serial == 7);
    CHECK(answer->chords == build_hand_chords(tune.song, 0, false));
    // The second bar: the left hand D, the right hand D7/A.
    REQUIRE(answer->chords.left.size() == 2);
    CHECK(answer->chords.left[0].name.plain() == "G");
    CHECK(answer->chords.left[1].name.plain() == "D");
    REQUIRE(answer->chords.right.size() == 1);
    CHECK(answer->chords.right[0].name.plain() == "D7/A");
    CHECK_FALSE(job.take().has_value()); // taken once
}

TEST_CASE("hand chords job: only the notes the hands play are named, drums and parts are not") {
    Tune tune;
    // An accompaniment part and a drum hit over the same bar change nothing.
    std::vector<Note> more = *tune.notes;
    Note part = more.front();
    part.accompaniment = true;
    part.hand = Hand::Unknown;
    more.push_back(part);
    Note drum = more.front();
    drum.drum = true;
    more.push_back(drum);
    HandChordsRequest r = tune.request(1);
    r.notes = std::make_shared<const std::vector<Note>>(more);
    HandChordsJob job;
    job.request(std::move(r));
    job.wait_idle();
    REQUIRE(job.take().has_value());
}

TEST_CASE("hand chords job: the newest request wins") {
    const Tune tune;
    HandChordsJob job;
    job.request(tune.request(1));
    job.request(tune.request(2, 2));
    job.request(tune.request(3, 5));
    job.wait_idle();
    const std::optional<HandChordsResult> answer = job.take();
    REQUIRE(answer.has_value());
    CHECK(answer->serial == 3);
    CHECK(answer->chords == build_hand_chords(tune.song, 5, false));
}

TEST_CASE(
    "hand chords job: another transposition over the same notes names again, never analyses") {
    const Tune tune;
    HandChordsJob job;
    job.request(tune.request(1));
    job.wait_idle();
    REQUIRE(job.take().has_value());
    CHECK(job.analyses() == 1);
    job.request(tune.request(2, 2));
    job.wait_idle();
    const std::optional<HandChordsResult> up = job.take();
    REQUIRE(up.has_value());
    CHECK(job.analyses() == 1); // the harmony was kept
    CHECK(up->chords == build_hand_chords(tune.song, 2, false));
    CHECK(up->chords.left[0].name.plain() == "A"); // G a tone higher, in A major
    // Other notes (another split of the hands) are analysed.
    Tune other;
    job.request(other.request(3));
    job.wait_idle();
    REQUIRE(job.take().has_value());
    CHECK(job.analyses() == 2);
}

TEST_CASE("hand chords job: a cancel drops the answer") {
    const Tune tune;
    HandChordsJob job;
    job.request(tune.request(1));
    job.wait_idle();
    job.cancel();
    CHECK_FALSE(job.take().has_value());
}

TEST_CASE("hand chords feed: no song, or the label switched off, drops the labels and the answer") {
    const Tune tune;
    HandChordsFeed feed;
    int made = 0;
    const auto make = [&] {
        ++made;
        return tune.request(0);
    };
    feed.update({true, 1, 1, 0, false}, make);
    feed.update({false, 1, 1, 0, false}, make); // the song is gone before the answer is taken
    feed.wait_idle();
    CHECK(feed.chords() == nullptr); // the answer was for a song that is gone
    CHECK(made == 1);
    feed.update({true, 3, 1, 0, false}, make);
    feed.wait_idle();
    CHECK(feed.chords() != nullptr);
    feed.update({false, 3, 1, 0, false}, make);
    CHECK(feed.chords() == nullptr);
    CHECK(made == 2);
}
