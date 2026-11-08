// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/exercise_sound.hpp"
#include "etude/exercises.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <map>

using namespace etude;

namespace {

ExerciseResult scale(HandsChoice hands, Direction direction = Direction::UpDown, int octaves = 1) {
    ScaleSpec spec;
    spec.root_pc = 7;
    spec.hands = hands;
    spec.direction = direction;
    spec.octaves = octaves;
    const ExerciseResult r = make_scale_exercise(spec);
    REQUIRE(r.ok);
    return r;
}

} // namespace

TEST_CASE("exercise sound: every note starts and ends once, due at its own time") {
    const ExerciseResult r = scale(HandsChoice::Right);
    const std::int64_t start = 5'000'000'000;
    const std::vector<SongSoundEvent> ev = exercise_sound_events(r.song, start);
    REQUIRE(ev.size() == r.song.notes.size() * 2);
    std::size_t ons = 0;
    std::size_t offs = 0;
    for (const SongSoundEvent& e : ev) {
        CHECK(e.kind == SongSoundEvent::Kind::Message);
        ons += starts_note(e.message) ? 1 : 0;
        offs += ends_note(e.message) ? 1 : 0;
    }
    CHECK(ons == r.song.notes.size());
    CHECK(offs == r.song.notes.size());
    // The first note is due at the start, the others each at the start plus its time.
    CHECK(ev.front().message.at_ns == start);
    for (const Note& n : r.song.notes) {
        const auto due = start + static_cast<std::int64_t>(n.start_s * 1e9 + 0.5);
        const bool found =
            std::any_of(ev.begin(), ev.end(), [&](const SongSoundEvent& e) {
                return starts_note(e.message) && e.message.data == n.pitch &&
                       std::llabs(e.message.at_ns - due) <= 1;
            });
        CHECK(found);
    }
}

TEST_CASE("exercise sound: in due order, a note-off before a note-on due at the same time") {
    const ExerciseResult r = scale(HandsChoice::Together, Direction::UpDown, 2);
    const std::vector<SongSoundEvent> ev = exercise_sound_events(r.song, 0);
    REQUIRE_FALSE(ev.empty());
    for (std::size_t i = 1; i < ev.size(); ++i) {
        CHECK(ev[i - 1].message.at_ns <= ev[i].message.at_ns);
        if (ev[i - 1].message.at_ns == ev[i].message.at_ns) {
            const bool on_then_off = starts_note(ev[i - 1].message) && ends_note(ev[i].message);
            CHECK_FALSE(on_then_off);
        }
    }
    // Nothing is left sounding: every key has as many offs as ons.
    std::map<int, int> open;
    for (const SongSoundEvent& e : ev) {
        open[e.message.data] += starts_note(e.message) ? 1 : -1;
        CHECK(open[e.message.data] >= 0);
    }
    for (const auto& [key, count] : open) {
        CHECK(count == 0);
    }
}

TEST_CASE("exercise sound: both hands sound together, an octave apart, in the same steps") {
    const ExerciseResult both = scale(HandsChoice::Together, Direction::Up);
    const ExerciseResult right = scale(HandsChoice::Right, Direction::Up);
    const std::vector<SongSoundEvent> ev = exercise_sound_events(both.song, 0);
    CHECK(ev.size() == 2 * right.song.notes.size() * 2);
    // The first instant starts two notes, twelve semitones apart.
    std::vector<int> first;
    for (const SongSoundEvent& e : ev) {
        if (starts_note(e.message) && e.message.at_ns == 0) {
            first.push_back(e.message.data);
        }
    }
    REQUIRE(first.size() == 2);
    std::sort(first.begin(), first.end());
    CHECK(first[1] - first[0] == 12);
}

TEST_CASE("exercise sound: the length runs from the first note to the last note's end") {
    const ExerciseResult r = scale(HandsChoice::Right, Direction::Up);
    // Eight eighth notes (the octave up, the tonic on top) at 80 bpm: 0.375 s each.
    CHECK(exercise_length_s(r.song) == doctest::Approx(8 * 0.375).epsilon(0.01));
    CHECK(exercise_length_s(Song{}) == 0.0);
}

TEST_CASE("exercise sound: a song too long for the sound thread's queue gives nothing") {
    Song big;
    for (int i = 0; i < 300; ++i) {
        Note n;
        n.start_s = i * 0.1;
        n.duration_s = 0.05;
        big.notes.push_back(n);
    }
    CHECK(exercise_sound_events(big, 0).empty());
    // The longest exercise a card plays fits: two octaves of the chromatic scale up and down, both
    // hands.
    ScaleSpec spec;
    spec.type = ScaleType::Chromatic;
    spec.octaves = 2;
    spec.direction = Direction::UpDown;
    spec.hands = HandsChoice::Together;
    const ExerciseResult r = make_scale_exercise(spec);
    REQUIRE(r.ok);
    CHECK_FALSE(exercise_sound_events(r.song, 0).empty());
}
