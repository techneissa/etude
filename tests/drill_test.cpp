// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/drill.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <algorithm>
#include <set>
#include <vector>

using namespace etude;

namespace {

DrillSpec scale_spec() {
    DrillSpec s;
    s.kind = DrillKind::Scale;
    s.root_pc = 7;
    s.scale = ScaleType::Major;
    s.octaves = 1;
    s.direction = Direction::Up;
    s.hands = HandsChoice::Right;
    s.bpm = 80;
    return s;
}

} // namespace

TEST_CASE("drill: a scale spec makes the scale's song, at the tempo, with its finger numbers") {
    const Drill d = make_drill(scale_spec());
    REQUIRE(d.ok);
    CHECK(d.title == "G major scale, RH, 1 oct");
    REQUIRE(d.song.notes.size() == 8);
    CHECK(d.song.notes.front().pitch % 12 == 7);
    CHECK(d.song.notes.front().finger == 1);
    for (const Note& n : d.song.notes) {
        CHECK(n.hand == Hand::Right);
    }
    // 80 beats a minute, an eighth note a note: 0.375 s apart.
    CHECK(d.song.notes[1].start_s - d.song.notes[0].start_s == doctest::Approx(0.375).epsilon(0.01));
    DrillSpec fast = scale_spec();
    fast.bpm = 160;
    const Drill f = make_drill(fast);
    REQUIRE(f.ok);
    CHECK(f.song.notes[1].start_s - f.song.notes[0].start_s ==
          doctest::Approx(0.1875).epsilon(0.01));
}

TEST_CASE("drill: direction, octaves and hands change the notes and the title") {
    DrillSpec s = scale_spec();
    s.octaves = 2;
    s.direction = Direction::UpDown;
    s.hands = HandsChoice::Together;
    const Drill d = make_drill(s);
    REQUIRE(d.ok);
    CHECK(d.title == "G major scale, both hands, 2 oct, up and down");
    // Up and down over two octaves is 15 + 14 notes a hand.
    CHECK(d.song.notes.size() == 2 * (15 + 14));
    s.direction = Direction::Down;
    s.hands = HandsChoice::Left;
    const Drill l = make_drill(s);
    REQUIRE(l.ok);
    CHECK(l.title == "G major scale, LH, 2 oct, down");
    CHECK(l.song.notes.size() == 15);
    CHECK(l.song.notes.front().pitch > l.song.notes.back().pitch);
}

TEST_CASE("drill: an arpeggio of a scale plays its triad; one of a chord plays the chord, "
          "inversion included") {
    DrillSpec s = scale_spec();
    s.kind = DrillKind::Arpeggio;
    s.scale = ScaleType::NaturalMinor;
    s.root_pc = 9; // A minor
    const Drill a = make_drill(s);
    REQUIRE(a.ok);
    CHECK(a.title == "A minor arpeggio, RH, 1 oct");
    REQUIRE(a.song.notes.size() == 4); // A C E A
    std::set<int> pcs;
    for (const Note& n : a.song.notes) {
        pcs.insert(n.pitch % 12);
    }
    CHECK(pcs == std::set<int>{9, 0, 4});

    DrillSpec c;
    c.kind = DrillKind::Arpeggio;
    c.root_pc = 0;
    c.chord = ChordQuality::Major7;
    c.inversion = 1;
    c.hands = HandsChoice::Right;
    const Drill chord = make_drill(c);
    REQUIRE(chord.ok);
    CHECK(chord.title.find("first inversion") != std::string::npos);
    REQUIRE(!chord.song.notes.empty());
    CHECK(chord.song.notes.front().pitch % 12 == 4); // the third in the bass
}

TEST_CASE("drill: the scales with a triad, and the quality it is") {
    CHECK(scale_triad(ScaleType::Major) == ChordQuality::Major);
    CHECK(scale_triad(ScaleType::Lydian) == ChordQuality::Major);
    CHECK(scale_triad(ScaleType::NaturalMinor) == ChordQuality::Minor);
    CHECK(scale_triad(ScaleType::Dorian) == ChordQuality::Minor);
    CHECK(scale_triad(ScaleType::MinorPentatonic) == ChordQuality::Minor);
    CHECK(scale_triad(ScaleType::MajorPentatonic) == ChordQuality::Major);
    CHECK(scale_triad(ScaleType::Locrian) == ChordQuality::Diminished);
    CHECK(scale_triad(ScaleType::WholeTone) == ChordQuality::Augmented);
    CHECK(scale_triad(ScaleType::Blues) == ChordQuality::Minor);
    CHECK_FALSE(scale_triad(ScaleType::Chromatic).has_value());
}

TEST_CASE("drill: a request that cannot be met says so, never throws or returns a song") {
    DrillSpec s = scale_spec();
    s.kind = DrillKind::Arpeggio;
    s.scale = ScaleType::Chromatic;
    const Drill none = make_drill(s);
    CHECK_FALSE(none.ok);
    CHECK(none.message.find("Chromatic") != std::string::npos);
    CHECK(none.song.notes.empty());

    // A keyboard too small for two octaves of both hands.
    DrillSpec big = scale_spec();
    big.octaves = 2;
    big.hands = HandsChoice::Together;
    const Drill small = make_drill(big, KeyRange{60, 72});
    CHECK_FALSE(small.ok);
    CHECK_FALSE(small.message.empty());
    CHECK(small.song.notes.empty());

    // A chord the arpeggio cannot do (a ninth chord).
    DrillSpec nine;
    nine.kind = DrillKind::Arpeggio;
    nine.chord = ChordQuality::Dominant9;
    const Drill ninth = make_drill(nine);
    CHECK_FALSE(ninth.ok);
    CHECK_FALSE(ninth.message.empty());
}

TEST_CASE("drill: the history knows a drill by its title and a hash that tells drills apart") {
    const DrillSpec a = scale_spec();
    DrillSpec b = a;
    b.bpm = 85;
    DrillSpec c = a;
    c.root_pc = 0;
    const Drill da = make_drill(a);
    CHECK(da.hash != 0);
    CHECK(da.hash == make_drill(a).hash);
    CHECK(da.hash != make_drill(b).hash);
    CHECK(da.hash != make_drill(c).hash);
}

TEST_CASE("drill: the tempo is kept in range and on the step") {
    CHECK(drill_tempo(0) == kDrillMinBpm);
    CHECK(drill_tempo(1000) == kDrillMaxBpm);
    CHECK(drill_tempo(83) == 85);
    CHECK(drill_tempo(81) == 80);
    CHECK(drill_tempo(80) == 80);
}

// ---- The drill in the player: the run it makes is kept under the drill's title ----

#include "song_controller.hpp"
#include "song_controller_test_support.hpp"

using namespace etude::app;
using namespace song_test;

TEST_CASE("drill: lent to the song controller it plays and grades like a song, and its run is "
          "kept under the drill's title and hash, with no library path") {
    const Drill d = make_drill(scale_spec());
    REQUIRE(d.ok);
    LoadedSong loaded;
    loaded.song = d.song;
    loaded.title = d.title;
    loaded.display_path = d.title;
    loaded.hash = d.hash;
    SongController c;
    std::int64_t now = kSec;
    c.load(std::move(loaded), now);
    c.set_practice_hands(HandFilter::Both, now);
    c.play(now);
    const std::int64_t start = now;
    // The eight notes pressed as they come due, each a little late.
    FakePiano piano;
    std::vector<std::pair<std::int64_t, MidiEvent>> script;
    for (const Note& n : d.song.notes) {
        const std::int64_t t = start + static_cast<std::int64_t>(n.start_s * 1e9) + 20 * kMs;
        script.push_back({t, press_with(n.pitch, 80, t)});
        script.push_back({t + 150 * kMs, note_off(n.pitch, t + 150 * kMs)});
    }
    std::sort(script.begin(), script.end(),
              [](const auto& a, const auto& b) { return a.first < b.first; });
    std::size_t next = 0;
    std::vector<MidiEvent> frame;
    while (c.state() != TransportState::Ended && now < start + 20 * kSec) {
        now += 16 * kMs;
        frame.clear();
        while (next < script.size() && script[next].first <= now) {
            frame.push_back(script[next++].second);
        }
        c.update(now, frame);
        feed(c, piano, now);
    }
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].record.song_title == "G major scale, RH, 1 oct");
    CHECK(runs[0].record.song_hash == d.hash);
    CHECK(runs[0].song_path.empty());
    // Every note of the drill was graded, and these were played.
    CHECK(runs[0].record.all.graded == 8);
    CHECK(runs[0].record.all.hit == 8);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
