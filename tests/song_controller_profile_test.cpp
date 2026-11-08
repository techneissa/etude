// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: a run is scored with the song it was played on and kept for its player's
// profile, whatever is opened or switched after it.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"

#include "etude/run_score.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cstdint>
#include <memory>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

namespace {

// Plays the first `count` right-hand notes of two_hand_song() on time, half a second apart.
void play_notes(SongController& c, FakePiano& piano, std::int64_t& now, int count) {
    const std::int64_t start = now;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (int i = 0; i < count; ++i) {
        const std::int64_t t = start + i * 500 * kMs;
        script.push_back({t, press_with(72 + (i % 4) * 2, 80, t)});
    }
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    const std::int64_t end = start + count * 500 * kMs + 100 * kMs;
    while (now < end) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
}

LoadedSong rated(LoadedSong song, int half_stars) {
    SongDifficulty d;
    d.bars = {3.0f};
    d.numbers = {1};
    d.half_stars = half_stars;
    song.difficulty = std::make_shared<const SongDifficulty>(std::move(d));
    return song;
}

} // namespace

TEST_CASE("a run ended by opening another song is scored with the song it was played on") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    c.load(rated(two_hand_song(), 2), now); // a one-star song, two bars
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    play_notes(c, piano, now, 4);
    const int old_bars = static_cast<int>(c.song().song.measures.size());
    // The next song is a four-star one with other bars; it is loaded after this run ends.
    c.load(rated(two_hand_song(4), 8), now);
    const std::vector<FinishedRun> runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    const RunRecord& r = runs[0].record;
    REQUIRE(r.all.graded > 0);
    RunRecord bare = r;
    bare.score = 0;
    bare.covered_percent = 0;
    ScoreContext old_song;
    old_song.song_notes = runs[0].events.expected_notes;
    old_song.song_bars = old_bars;
    old_song.difficulty_half_stars = 2;
    ScoreContext new_song = old_song;
    new_song.song_bars = 4;
    new_song.difficulty_half_stars = 8;
    CHECK(r.score > 0);
    CHECK(r.score == scored(bare, old_song).score);
    CHECK(r.covered_percent == scored(bare, old_song).covered_percent);
    CHECK(scored(bare, new_song).score != r.score); // the song loaded after would have said otherwise
}

TEST_CASE("a run keeps the profile it was played under") {
    SongController c;
    FakePiano piano;
    std::int64_t now = kSec;
    CHECK(c.profile_id() == kDefaultProfileId);
    c.set_profile(2, now);
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    play_notes(c, piano, now, 3);
    // Opening another song ends the run; the profile has not changed, so it is profile 2's.
    c.load(two_hand_song(), now);
    std::vector<FinishedRun> runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 2);

    // Switching profile in the middle of a run ends it under the old one.
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    play_notes(c, piano, now, 3);
    c.set_profile(3, now);
    CHECK(c.profile_id() == 3);
    runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 2);

    // And the next run is the new profile's.
    c.set_profile(3, now); // no change: nothing ends
    c.load(two_hand_song(), now);
    c.set_practice_hands(HandFilter::Right, now);
    c.play(now);
    play_notes(c, piano, now, 3);
    c.load(two_hand_song(), now);
    runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].profile_id == 3);
    c.set_profile(0, now); // not a profile: ignored
    CHECK(c.profile_id() == 3);
}
