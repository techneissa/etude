// SPDX-License-Identifier: GPL-3.0-or-later
// What the tempo chip shows and the speed a typed tempo gives (etude/tempo_control.hpp).
#include "etude/tempo_control.hpp"

#include "etude/midi_file.hpp"
#include "etude/transport.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <utility>

using namespace etude;
using namespace etude::test;

namespace {

Song load(const Track& t) {
    MidiLoadResult r = load_midi(smf({t}));
    REQUIRE(r.ok);
    return r.song;
}

// 100 BPM for the first bar (2.4 s), then 50 BPM for two bars (9.6 s), then 100 BPM for one bar.
Song changing_song() {
    Track t;
    t.bpm(0, 100).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 50).bpm(1920 * 2, 100);
    t.off(1920 * 4, 0, 60).end();
    return load(t);
}

} // namespace

TEST_CASE("tempo: the song's tempo at a position follows the tempo map") {
    const Song s = changing_song();
    CHECK(song_bpm_at(s, 0.0) == doctest::Approx(100.0));
    CHECK(song_bpm_at(s, 2.39) == doctest::Approx(100.0));
    CHECK(song_bpm_at(s, 2.4) == doctest::Approx(50.0));
    CHECK(song_bpm_at(s, 9.0) == doctest::Approx(50.0));
    CHECK(song_bpm_at(s, 12.0) == doctest::Approx(100.0));
    CHECK(song_bpm_at(s, -1.0) == doctest::Approx(100.0)); // a count-in before the start
    CHECK(song_bpm_at(Song{}, 0.0) == doctest::Approx(120.0));
}

TEST_CASE("tempo: the chip shows the tempo at the playhead times the speed") {
    const Song s = changing_song();
    CHECK(effective_bpm(s, 0.0, 100) == doctest::Approx(100.0));
    CHECK(effective_bpm(s, 0.0, 70) == doctest::Approx(70.0));
    CHECK(effective_bpm(s, 5.0, 100) == doctest::Approx(50.0));
    CHECK(effective_bpm(s, 5.0, 150) == doctest::Approx(75.0));
    CHECK(effective_bpm(s, 5.0, 200) == doctest::Approx(100.0));
    CHECK(effective_bpm(s, 5.0, 25) == doctest::Approx(12.5));
}

TEST_CASE("tempo: the main tempo is the one that covers most of the song") {
    CHECK(main_bpm(changing_song()) == doctest::Approx(50.0));
    CHECK(main_bpm(Song{}) == doctest::Approx(120.0));
    // A tie goes to the earlier tempo.
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).off(1920 * 2, 0, 60).end();
    const Song tie = load(t);
    // 120 BPM covers 2 s, 60 BPM covers 4 s: the slower one wins.
    CHECK(main_bpm(tie) == doctest::Approx(60.0));
}

TEST_CASE("tempo: a typed tempo converts to a speed and back") {
    const Song s = load(Track().bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).off(1920, 0, 60).end());
    CHECK(speed_for_target(s, 60.0, 0.0) == 50);
    CHECK(speed_for_target(s, 90.0, 0.0) == 75);
    CHECK(speed_for_target(s, 120.0, 0.0) == 100);
    CHECK(speed_for_target(s, 180.0, 0.0) == 150);
    CHECK(speed_for_target(s, 240.0, 0.0) == 200);
    // The speed is a whole percent, so a typed tempo lands within a percent of it: 100 BPM at a
    // main tempo of 120 is 83% (83.3), and the chip then shows 100.
    CHECK(speed_for_target(s, 100.0, 0.0) == 83);
    CHECK(display_bpm(effective_bpm(s, 0.0, speed_for_target(s, 100.0, 0.0))) == 100);
    CHECK(speed_for_target(s, 97.0, 0.0) == 81);
    CHECK(display_bpm(effective_bpm(s, 0.0, 81)) == 97);
    // Round trip at every whole percent: the shown tempo, typed back, gives the same speed.
    for (int p = kMinSpeedPercent; p <= kMaxSpeedPercent; ++p) {
        CAPTURE(p);
        CHECK(speed_for_target(s, effective_bpm(s, 0.0, p), 0.0) == p);
    }
}

TEST_CASE("tempo: in 6/8 the chip counts dotted quarters and a typed tempo means them") {
    // 6/8 at 120 quarter notes a minute: 80 dotted quarters a minute.
    Track t;
    t.bpm(0, 120).time_sig(0, 6, 8).on(0, 0, 60).off(1440 * 2, 0, 60).end();
    const Song s = load(t);
    const BeatUnit u = beat_unit_at(s, 0.0);
    CHECK(u.dotted);
    CHECK(u.quarters == doctest::Approx(1.5));
    CHECK(effective_bpm(s, 0.0, 100) == doctest::Approx(80.0));
    CHECK(effective_bpm(s, 0.0, 70) == doctest::Approx(56.0)); // "dotted quarter 56"
    CHECK(main_beat_bpm(s, 0.0) == doctest::Approx(80.0));
    CHECK(speed_for_target(s, 56.0, 0.0) == 70);
    CHECK(speed_for_target(s, 80.0, 0.0) == 100);
    CHECK(speed_for_target(s, 40.0, 0.0) == 50);
    const BpmRange r = bpm_range(s, 0.0);
    CHECK(r.low == doctest::Approx(20.0));
    CHECK(r.high == doctest::Approx(160.0));
    // The quarter-note tempo underneath is unchanged.
    CHECK(song_bpm_at(s, 0.0) == doctest::Approx(120.0));
}

TEST_CASE("tempo: 9/8 and 12/8 are dotted quarters too; 3/8, 5/8, 2/2 and 4/4 count quarters") {
    const auto unit = [](int num, int den) {
        Track t;
        t.bpm(0, 120).time_sig(0, num, den).on(0, 0, 60);
        t.off(static_cast<std::uint32_t>(480 * 4 * num / den) * 2, 0, 60).end();
        return beat_unit_at(load(t), 0.0);
    };
    CHECK(unit(9, 8).dotted);
    CHECK(unit(12, 8).dotted);
    for (const auto& meter : {std::pair{4, 4}, std::pair{3, 4}, std::pair{2, 2}, std::pair{3, 8},
                              std::pair{5, 8}, std::pair{7, 8}}) {
        CAPTURE(meter.first);
        CAPTURE(meter.second);
        CHECK_FALSE(unit(meter.first, meter.second).dotted);
        CHECK(unit(meter.first, meter.second).quarters == 1.0);
    }
    CHECK_FALSE(beat_unit_at(Song{}, 0.0).dotted); // no bars: 4/4
}

TEST_CASE("tempo: the chip's beat follows the meter at the playhead") {
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60);
    t.time_sig(1920, 6, 8).off(1920 + 1440, 0, 60).end();
    const Song s = load(t);
    CHECK_FALSE(beat_unit_at(s, 0.5).dotted);
    CHECK(effective_bpm(s, 0.5, 100) == doctest::Approx(120.0));
    CHECK(beat_unit_at(s, 2.5).dotted);
    CHECK(effective_bpm(s, 2.5, 100) == doctest::Approx(80.0));
}
