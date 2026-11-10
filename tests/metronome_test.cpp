// SPDX-License-Identifier: GPL-3.0-or-later
// The beat grid and the count-in planner (etude/metronome.hpp), with the real Transport, tempo
// map and look-ahead where a rule crosses them.
#include "etude/metronome.hpp"

#include "etude/look_ahead.hpp"
#include "etude/midi_file.hpp"
#include "etude/playback.hpp"
#include "etude/transport.hpp"

#include "smf_builder.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <random>
#include <vector>

using namespace etude;
using namespace etude::test;
using Kind = TransportOp::Kind;

namespace {

constexpr std::int64_t sec(double s) { return static_cast<std::int64_t>(std::llround(s * 1e9)); }

Song load(const Track& t) {
    MidiLoadResult r = load_midi(smf({t}));
    REQUIRE(r.ok);
    return r.song;
}

// `bars` bars of `num`/`den` at `bpm` (quarter notes per minute).
Song bars_song(int bars, int bpm = 120, int num = 4, int den = 4) {
    Track t;
    t.bpm(0, bpm).time_sig(0, num, den);
    const auto bar_ticks = static_cast<std::uint32_t>(480 * 4 * num / den);
    t.on(0, 0, 60).off(bar_ticks * static_cast<std::uint32_t>(bars), 0, 60).end();
    return load(t);
}

// A one-beat pickup (bar 0, beat 4 of a 4/4 bar), then 4/4 bars from tick 480 at 120 BPM.
Song pickup_song() {
    Song s = load(Track()
                      .time_sig(0, 1, 4)
                      .on(0, 0, 67)
                      .off(480, 0, 67)
                      .time_sig(0, 4, 4)
                      .on(0, 0, 60)
                      .off(1920 * 2, 0, 60)
                      .end());
    REQUIRE(s.measures.size() >= 3);
    REQUIRE(s.measures[0].number == 0);
    return s;
}

// A Move of the song span [from, to) played at `speed` from wall time `at_ns`.
TransportOp move_of(double from, double to, int speed = 100, std::int64_t at_ns = 0) {
    TransportOp m;
    m.kind = Kind::Move;
    m.from_s = from;
    m.to_s = to;
    m.speed_percent = speed;
    m.at_ns = at_ns;
    m.end_ns = at_ns + std::llround((to - from) / (speed / 100.0) * 1e9);
    return m;
}

std::vector<TransportOp> clicks_of(const BeatGrid& grid, const TransportOp& move,
                                   const MetronomeSettings& settings = {}) {
    std::vector<TransportOp> out;
    grid.clicks(move, settings, out);
    return out;
}

BeatGrid grid_of(const Song& s) {
    BeatGrid g;
    g.set_song(s);
    return g;
}

std::vector<double> song_times(const std::vector<TransportOp>& ops) {
    std::vector<double> t;
    for (const TransportOp& o : ops) {
        t.push_back(o.to_s);
    }
    return t;
}

} // namespace

// ---- Beat grid ------------------------------------------------------------------------------

TEST_CASE("metronome: a click on every beat, beat one accented") {
    const BeatGrid grid = grid_of(bars_song(4)); // 120 BPM: a beat is 0.5 s
    const auto clicks = clicks_of(grid, move_of(0.0, 2.25));
    REQUIRE(clicks.size() == 5); // 0, .5, 1, 1.5, 2
    std::vector<int> beats;
    for (const TransportOp& c : clicks) {
        beats.push_back(c.beat);
        CHECK(c.kind == Kind::Click);
        CHECK_FALSE(c.count_in);
        CHECK(c.sub == 0);
        CHECK(c.accent == (c.beat == 1));
    }
    CHECK(beats == std::vector<int>{1, 2, 3, 4, 1});
    CHECK(clicks[3].to_s == 1.5);
    CHECK(clicks[3].at_ns == sec(1.5));
}

TEST_CASE("metronome: the accent can be turned off") {
    const BeatGrid grid = grid_of(bars_song(2));
    MetronomeSettings s;
    s.accent = false;
    for (const TransportOp& c : clicks_of(grid, move_of(0.0, 4.0), s)) {
        CHECK_FALSE(c.accent);
    }
}

TEST_CASE("metronome: a click is made once when a span ends on a beat and the next starts there") {
    const BeatGrid grid = grid_of(bars_song(4));
    CHECK(clicks_of(grid, move_of(0.0, 0.5)).size() == 1); // beat at 0; the one at 0.5 is not in it
    CHECK(clicks_of(grid, move_of(0.5, 1.0)).size() == 1); // beat at 0.5
    CHECK(clicks_of(grid, move_of(0.25, 0.5)).empty());    // [0.25, 0.5) holds no beat
}

TEST_CASE("metronome: clicks follow the speed: wall time is song time over the rate") {
    const BeatGrid grid = grid_of(bars_song(4));
    const auto half = clicks_of(grid, move_of(0.0, 1.25, 50, sec(10)));
    REQUIRE(half.size() == 3); // song beats at 0, .5, 1 are wall 10, 11, 12 s
    CHECK(half[0].at_ns == sec(10));
    CHECK(half[1].at_ns == sec(11));
    CHECK(half[2].at_ns == sec(12));
    const auto fast = clicks_of(grid, move_of(0.0, 1.25, 200, sec(10)));
    REQUIRE(fast.size() == 3);
    CHECK(fast[2].at_ns == sec(10.5));
    const auto top = clicks_of(grid, move_of(0.0, 1.25, 150, sec(0)));
    REQUIRE(top.size() == 3);
    CHECK(top[1].at_ns == sec(0.5 / 1.5));
}

TEST_CASE("metronome: Divide and Triplets split each beat; the extra clicks are never accented") {
    const BeatGrid grid = grid_of(bars_song(2));
    MetronomeSettings s;
    s.subdivision = ClickSubdivision::Divide;
    auto eighths = clicks_of(grid, move_of(0.0, 1.0), s);
    REQUIRE(eighths.size() == 4); // 0, .25, .5, .75
    CHECK(song_times(eighths) == std::vector<double>{0.0, 0.25, 0.5, 0.75});
    CHECK(eighths[0].sub == 0);
    CHECK(eighths[1].sub == 1);
    CHECK(eighths[1].beat == 1); // inside beat one, not a new beat
    CHECK_FALSE(eighths[1].accent);
    CHECK(eighths[0].accent);

    s.subdivision = ClickSubdivision::Triplets;
    auto triplets = clicks_of(grid, move_of(0.0, 0.5), s);
    REQUIRE(triplets.size() == 3);
    CHECK(triplets[1].to_s == doctest::Approx(0.5 / 3.0).epsilon(1e-6));
    CHECK(triplets[2].to_s == doctest::Approx(1.0 / 3.0).epsilon(1e-6));
    CHECK(triplets[1].sub == 1);
    CHECK(triplets[2].sub == 2);
    CHECK_FALSE(triplets[2].accent);

    s.subdivision = ClickSubdivision::Off;
    CHECK(clicks_of(grid, move_of(0.0, 0.5), s).size() == 1);
}

TEST_CASE("metronome: in 6/8, 9/8 and 12/8 the beat is a dotted quarter, Divide gives its three eighths") {
    // 120 quarter notes a minute: a dotted quarter is 0.75 s, an eighth 0.25 s.
    struct Meter {
        int num;
        int beats;
    };
    for (const Meter m : {Meter{6, 2}, Meter{9, 3}, Meter{12, 4}}) {
        CAPTURE(m.num);
        const BeatGrid grid = grid_of(bars_song(2, 120, m.num, 8));
        const double bar_s = m.num * 0.25;
        auto beats = clicks_of(grid, move_of(0.0, bar_s));
        REQUIRE(static_cast<int>(beats.size()) == m.beats);
        for (int i = 0; i < m.beats; ++i) {
            CHECK(beats[static_cast<std::size_t>(i)].to_s == doctest::Approx(0.75 * i));
            CHECK(beats[static_cast<std::size_t>(i)].beat == i + 1);
            CHECK(beats[static_cast<std::size_t>(i)].accent == (i == 0));
        }
        MetronomeSettings s;
        s.subdivision = ClickSubdivision::Divide;
        const auto eighths = clicks_of(grid, move_of(0.0, bar_s), s);
        REQUIRE(static_cast<int>(eighths.size()) == m.num); // every eighth of the bar
        for (int i = 0; i < m.num; ++i) {
            const TransportOp& c = eighths[static_cast<std::size_t>(i)];
            CHECK(c.to_s == doctest::Approx(0.25 * i));
            CHECK(c.beat == i / 3 + 1);
            CHECK(c.sub == i % 3);
            CHECK(c.accent == (i == 0)); // only beat one
        }
        // Triplets means nothing in threes: it plays as Divide.
        s.subdivision = ClickSubdivision::Triplets;
        CHECK(song_times(clicks_of(grid, move_of(0.0, bar_s), s)) == song_times(eighths));
    }
}

TEST_CASE("metronome: 3/8 and 5/8 stay simple: a click on each eighth, Divide splits it in two") {
    const BeatGrid three = grid_of(bars_song(2, 120, 3, 8));
    CHECK(song_times(clicks_of(three, move_of(0.0, 0.75))) == std::vector<double>{0.0, 0.25, 0.5});
    MetronomeSettings s;
    s.subdivision = ClickSubdivision::Divide;
    CHECK(clicks_of(three, move_of(0.0, 0.75), s).size() == 6);
    const BeatGrid five = grid_of(bars_song(2, 120, 5, 8));
    CHECK(clicks_of(five, move_of(0.0, 1.25)).size() == 5);
}

TEST_CASE("metronome: a compound meter in the middle of a song changes the beat there") {
    // Two bars of 4/4 at 120 (1 s... 2 s each), then 6/8.
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60);
    t.time_sig(1920 * 2, 6, 8).off(1920 * 2 + 1440 * 2, 0, 60).end();
    const BeatGrid grid = grid_of(load(t));
    const auto c = clicks_of(grid, move_of(3.5, 8.0));
    REQUIRE(c.size() >= 3);
    // 4/4 beats at 3.5 s (the last of bar 2 in 4/4), then 4.0 s starts the 6/8 bar: 4.0, 4.75.
    CHECK(song_times(c)[0] == doctest::Approx(3.5));
    CHECK(c[1].to_s == doctest::Approx(4.0));
    CHECK(c[1].beat == 1);
    CHECK(c[2].to_s == doctest::Approx(4.75));
    CHECK(c[2].beat == 2);
}

TEST_CASE("metronome: a subdivision never reaches into the next bar") {
    // 3/4 bars of 1.5 s: the last beat's parts end before the bar line.
    const BeatGrid grid = grid_of(bars_song(2, 120, 3, 4));
    MetronomeSettings s;
    s.subdivision = ClickSubdivision::Triplets;
    const auto clicks = clicks_of(grid, move_of(0.0, 1.5), s);
    CHECK(clicks.size() == 9);
    CHECK(clicks.back().to_s < 1.5);
}

TEST_CASE("metronome: the grid follows the time signature: 3/4 and 6/8") {
    const BeatGrid three = grid_of(bars_song(2, 120, 3, 4));
    auto c = clicks_of(three, move_of(0.0, 3.0));
    std::vector<int> beats;
    for (const TransportOp& x : c) {
        beats.push_back(x.beat);
    }
    CHECK(beats == std::vector<int>{1, 2, 3, 1, 2, 3});
    CHECK(c[3].to_s == 1.5);
    CHECK(c[3].accent);

    // 6/8 at 120 quarter notes a minute: the beat is a dotted quarter, 0.75 s, a bar of two beats
    // 1.5 s.
    const BeatGrid six = grid_of(bars_song(2, 120, 6, 8));
    c = clicks_of(six, move_of(0.0, 3.0));
    REQUIRE(c.size() == 4);
    CHECK(song_times(c) == std::vector<double>{0.0, 0.75, 1.5, 2.25});
    CHECK(c[0].beat == 1);
    CHECK(c[1].beat == 2);
    CHECK(c[2].beat == 1);
    CHECK(c[2].accent);
    CHECK_FALSE(c[1].accent);
}

TEST_CASE("metronome: clicks across a tempo change fall on the tempo map's beats") {
    // 120 BPM in 4/4 for the first bar (2 s), then 60 BPM from tick 1920 (bar 2).
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).off(1920 * 2, 0, 60).end();
    const Song song = load(t);
    REQUIRE(song.measures.size() >= 2);
    REQUIRE(song.measures[1].start_s == doctest::Approx(2.0));
    const BeatGrid grid = grid_of(song);
    const auto clicks = clicks_of(grid, move_of(0.0, 6.0, 100, sec(5)));
    // Bar 1: 0, .5, 1, 1.5 s; bar 2 at 60 BPM: 2, 3, 4, 5 s.
    REQUIRE(clicks.size() == 8);
    const std::vector<double> want{0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0};
    for (std::size_t i = 0; i < want.size(); ++i) {
        CAPTURE(i);
        CHECK(clicks[i].to_s == doctest::Approx(want[i]));
        CHECK(clicks[i].at_ns == sec(5 + want[i]));
        CHECK(clicks[i].beat == static_cast<int>(i % 4) + 1);
    }
}

TEST_CASE("metronome: a pickup bar's beats are numbered back from the end of the bar") {
    const BeatGrid grid = grid_of(pickup_song());
    const auto clicks = clicks_of(grid, move_of(0.0, 1.25));
    REQUIRE(clicks.size() == 3);
    CHECK(clicks[0].beat == 4); // the last beat of a 4/4 bar, not an accented beat one
    CHECK_FALSE(clicks[0].accent);
    CHECK(clicks[1].beat == 1); // bar 1 starts at 0.5 s
    CHECK(clicks[1].accent);
    CHECK(clicks[1].to_s == 0.5);
    CHECK(clicks[2].beat == 2);
    CHECK(clicks[2].to_s == 1.0);
}

TEST_CASE("metronome: a song with no bars has no clicks") {
    Song empty;
    empty.duration_s = 10.0;
    CHECK(clicks_of(grid_of(empty), move_of(0.0, 5.0)).empty());
    CHECK(clicks_of(grid_of(bars_song(2)), move_of(1.0, 1.0)).empty());
}

// The planner's spans are the look-ahead's Moves: walk a Transport the way the app does and check
// every beat sounds once, whatever the frame length, speed and loop.
namespace {

struct Walk {
    Song song;
    BeatGrid grid;
    Transport tp;
    double cursor = 0.0;
    int planned_wraps = 0;
    std::vector<TransportOp> clicks;
    std::int64_t window = 30'000'000;

    explicit Walk(const Song& s) : song(s) {
        grid.set_song(s);
        tp.set_song(s);
    }
    void plan(std::int64_t now_ns, const MetronomeSettings& settings) {
        LookAheadInput in;
        in.position_s = tp.position_s();
        in.speed_percent = tp.speed_percent();
        in.moving = tp.state() == TransportState::Playing;
        in.duration_s = tp.duration_s();
        in.loop_on = tp.loop_enabled();
        in.loop_start_s = tp.loop_start_s();
        in.loop_end_s = tp.loop_end_s();
        in.wraps_ahead = tp.loop_count() - planned_wraps;
        std::vector<TransportOp> ops;
        planned_wraps += plan_look_ahead(in, now_ns, window, cursor, ops).wraps_planned;
        for (const TransportOp& op : ops) {
            if (op.kind == Kind::Move) {
                grid.clicks(op, settings, clicks);
            }
        }
    }
};

} // namespace

TEST_CASE("metronome: every beat is sounded once whatever the frame length and speed") {
    for (const int speed : {50, 100, 150, 200}) {
        for (const std::int64_t frame_ms : {7, 16, 33}) {
            Walk w(bars_song(4));
            w.tp.set_speed_percent(0, speed);
            w.tp.play(0);
            w.plan(0, {});
            for (std::int64_t ms = frame_ms; ms <= 4000; ms += frame_ms) {
                w.tp.update(ms * 1'000'000);
                w.plan(ms * 1'000'000, {});
            }
            CAPTURE(speed);
            CAPTURE(frame_ms);
            // Beats at 0.5 s of song time each, as far as the clock got plus the look-ahead.
            const double reached = w.cursor;
            const auto expected = static_cast<std::size_t>(std::ceil(reached / 0.5 - 1e-9));
            REQUIRE(w.clicks.size() == expected);
            for (std::size_t i = 0; i < w.clicks.size(); ++i) {
                CHECK(w.clicks[i].to_s == doctest::Approx(0.5 * static_cast<double>(i)));
                // The wall time is the song time over the rate, to the nanosecond.
                CHECK(std::llabs(w.clicks[i].at_ns -
                                 sec(0.5 * static_cast<double>(i) / (speed / 100.0))) <= 2);
            }
        }
    }
}

TEST_CASE("metronome: clicks across a loop wrap start the grid again from the loop start") {
    Walk w(bars_song(4)); // beats every 0.5 s
    REQUIRE(w.tp.set_loop(0, 1.2, 2.7)); // loop 1.5 s: beats at 1.5, 2.0, 2.5 in, then again
    w.tp.seek(0, 1.2);
    w.cursor = 1.2;
    w.tp.play(0);
    w.plan(0, {});
    for (std::int64_t ms = 16; ms <= 3600; ms += 16) {
        w.tp.update(ms * 1'000'000);
        w.plan(ms * 1'000'000, {});
    }
    REQUIRE(w.clicks.size() >= 6);
    // Each pass: 1.5, 2.0, 2.5 (the loop's beat grid is the song's, not shifted by the wrap).
    const std::vector<double> pass{1.5, 2.0, 2.5};
    for (std::size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(w.clicks[i].to_s == doctest::Approx(pass[i % 3]));
    }
    // The wall time of the first beat after the wrap: a pass is 1.5 s long, the first beat of the
    // next one 0.3 s after the wrap (1.5 - 1.2), 1.8 s after the start.
    CHECK(w.clicks[0].at_ns == sec(0.3));
    CHECK(w.clicks[3].at_ns == sec(1.5 + 0.3));
}

TEST_CASE("metronome: a beat exactly at the loop start sounds, one exactly at the loop end does not") {
    Walk w(bars_song(4)); // beats every 0.5 s
    REQUIRE(w.tp.set_loop(0, 1.0, 2.0));
    w.tp.seek(0, 1.0);
    w.cursor = 1.0;
    w.tp.play(0);
    w.plan(0, {});
    for (std::int64_t ms = 16; ms <= 4200; ms += 16) {
        w.tp.update(ms * 1'000'000);
        w.plan(ms * 1'000'000, {});
    }
    REQUIRE(w.clicks.size() >= 6);
    // 1.0 and 1.5 each pass; 2.0 is the loop end, the next pass's 1.0 stands in for it.
    for (std::size_t i = 0; i < 6; ++i) {
        CAPTURE(i);
        CHECK(w.clicks[i].to_s == doctest::Approx(i % 2 == 0 ? 1.0 : 1.5));
        CHECK(w.clicks[i].at_ns == sec(0.5 * static_cast<double>(i)));
    }
}

TEST_CASE("metronome: clicks across a tempo change at a speed keep the grid") {
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).off(1920 * 3, 0, 60).end();
    Walk w(load(t));
    w.tp.set_speed_percent(0, 50);
    w.tp.play(0);
    w.plan(0, {});
    for (std::int64_t ms = 16; ms <= 16000; ms += 16) {
        w.tp.update(ms * 1'000'000);
        w.plan(ms * 1'000'000, {});
    }
    // Song beats: 0 .5 1 1.5 | 2 3 4 5 | 6 7 8 9; at 50% they are twice as far apart in wall time.
    const std::vector<double> beats{0.0, 0.5, 1.0, 1.5, 2.0, 3.0, 4.0, 5.0, 6.0, 7.0};
    REQUIRE(w.clicks.size() >= beats.size());
    for (std::size_t i = 0; i < beats.size(); ++i) {
        CAPTURE(i);
        CHECK(w.clicks[i].to_s == doctest::Approx(beats[i]));
        CHECK(std::llabs(w.clicks[i].at_ns - sec(beats[i] * 2.0)) <= 2);
    }
}

TEST_CASE("metronome: a random split of the span gives the same clicks as one span") {
    const Song song = bars_song(8);
    const BeatGrid grid = grid_of(song);
    MetronomeSettings s;
    s.subdivision = ClickSubdivision::Triplets;
    const auto whole = clicks_of(grid, move_of(0.0, 16.0), s);
    std::mt19937 rng(7);
    std::vector<double> cuts{0.0};
    std::uniform_real_distribution<double> step(0.001, 0.4);
    while (cuts.back() < 16.0) {
        cuts.push_back(std::min(16.0, cuts.back() + step(rng)));
    }
    std::vector<TransportOp> pieces;
    for (std::size_t i = 1; i < cuts.size(); ++i) {
        grid.clicks(move_of(cuts[i - 1], cuts[i]), s, pieces);
    }
    REQUIRE(pieces.size() == whole.size());
    for (std::size_t i = 0; i < whole.size(); ++i) {
        CHECK(pieces[i].to_s == whole[i].to_s);
        CHECK(pieces[i].sub == whole[i].sub);
    }
}

// ---- Count-in -------------------------------------------------------------------------------

namespace {

int count_in_beats_for(const Song& song, int bars) {
    Transport t;
    t.set_song(song);
    t.set_count_in_bars(bars);
    t.play(0);
    return t.count_in_total();
}

} // namespace

TEST_CASE("count-in: one and two bars of 4/4, 3/4 and the compound meters count the felt beats") {
    CHECK(count_in_beats_for(bars_song(4), 1) == 4);
    CHECK(count_in_beats_for(bars_song(4), 2) == 8);
    CHECK(count_in_beats_for(bars_song(4, 120, 3, 4), 1) == 3);
    CHECK(count_in_beats_for(bars_song(4, 120, 3, 4), 2) == 6);
    CHECK(count_in_beats_for(bars_song(4, 120, 6, 8), 1) == 2); // the felt beats: two dotted quarters
    CHECK(count_in_beats_for(bars_song(4, 120, 6, 8), 2) == 4);
    CHECK(count_in_beats_for(bars_song(4, 120, 9, 8), 1) == 3);
    CHECK(count_in_beats_for(bars_song(4, 120, 12, 8), 1) == 4);
    CHECK(count_in_beats_for(bars_song(4, 120, 3, 8), 1) == 3); // 3/8 is simple: three eighths
    CHECK(count_in_beats_for(bars_song(4, 120, 5, 8), 1) == 5);
    CHECK(count_in_beats_for(bars_song(4, 120, 2, 2), 1) == 2);
    CHECK(count_in_beats_for(bars_song(4), 0) == 0);
}

TEST_CASE("count-in: the bars are kept to four, and a bar count of zero or less is off") {
    CHECK(count_in_beats_for(bars_song(4), 9) == 16);
    CHECK(count_in_beats_for(bars_song(4), 4) == 16);
    CHECK(count_in_beats_for(bars_song(4), -3) == 0);
}

TEST_CASE("count-in: a pickup bar's beats are left out so the pickup falls on the last beats") {
    // A one-beat pickup before 4/4: 1 2 3 then the pickup on 4; two bars: 1 2 3 4 1 2 3 + pickup.
    CHECK(count_in_beats_for(pickup_song(), 1) == 3);
    CHECK(count_in_beats_for(pickup_song(), 2) == 7);

    Transport t;
    t.set_song(pickup_song());
    t.set_count_in_bars(1);
    t.play(0);
    CHECK(t.count_in_numerator() == 4);
    CHECK(t.count_in_beat_seconds() == doctest::Approx(0.5));
}

TEST_CASE("count-in: a count-in from the middle of the song uses the meter and tempo there") {
    // 120 BPM in 4/4, then from bar 2 (tick 1920) 60 BPM in 3/4.
    Track t;
    t.bpm(0, 120).time_sig(0, 4, 4).on(0, 0, 60).bpm(1920, 60).time_sig(0, 3, 4);
    t.off(1440 * 2, 0, 60).end();
    const Song song = load(t);
    REQUIRE(song.measures.size() >= 2);
    Transport tr;
    tr.set_song(song);
    tr.set_count_in_bars(2);
    tr.seek(0, song.measures[1].start_s);
    tr.play(0);
    CHECK(tr.count_in_total() == 6);
    CHECK(tr.count_in_numerator() == 3);
    CHECK(tr.count_in_beat_seconds() == doctest::Approx(1.0));
    CHECK(tr.count_in_start_s() == doctest::Approx(2.0));
}

TEST_CASE("count-in: the transport waits out the bars and exposes no clicks of its own") {
    Transport t;
    t.set_song(bars_song(4));
    t.set_count_in_bars(1);
    t.play(sec(10));
    CHECK(t.state() == TransportState::CountIn);
    t.update(sec(11.9));
    CHECK(t.position_s() == 0.0);
    t.update(sec(12.5));
    CHECK(t.position_s() == doctest::Approx(0.5));
    std::vector<TransportOp> ops;
    t.take_ops(ops);
    for (const TransportOp& o : ops) {
        CHECK(o.kind != Kind::Click);
    }
}

TEST_CASE("count-in: it runs on while Wait Mode holds the clock at the first chord") {
    Transport t;
    t.set_song(bars_song(4));
    t.set_count_in_bars(1);
    t.play(0);
    t.hold(0);
    CHECK(t.state() == TransportState::CountIn);
    t.update(sec(1));
    CHECK(t.count_in_beats_left() == doctest::Approx(2.0));
    t.update(sec(2.5));
    CHECK_FALSE(t.counting_in());
    CHECK(t.state() == TransportState::Held);
    CHECK(t.position_s() == 0.0);
}

namespace {

CountInInput four_beats(int speed = 100, double left = 4.0) {
    CountInInput in;
    in.total_beats = 4;
    in.beats_left = left;
    in.beat_s = 0.5;
    in.numerator = 4;
    in.speed_percent = speed;
    in.start_s = 0.0;
    return in;
}

} // namespace

TEST_CASE("count-in clicks: due at their beat, accent on beat one, the first at once") {
    std::vector<TransportOp> out;
    int next = 0;
    plan_count_in(four_beats(), sec(10), sec(5), {}, next, out);
    REQUIRE(out.size() == 4);
    CHECK(next == 4);
    for (int i = 0; i < 4; ++i) {
        const auto k = static_cast<std::size_t>(i);
        CAPTURE(i);
        CHECK(out[k].count_in);
        CHECK(out[k].beat == i + 1);
        CHECK(out[k].accent == (i == 0));
        CHECK(out[k].at_ns == sec(10 + 0.5 * i));
        CHECK(out[k].to_s == doctest::Approx(-0.5 * (4 - i)));
    }
}

TEST_CASE("count-in clicks: only those inside the window, each handed over once") {
    std::vector<TransportOp> out;
    int next = 0;
    plan_count_in(four_beats(), sec(10), 30'000'000, {}, next, out);
    CHECK(out.size() == 1); // the first beat is due now; the next is 500 ms away
    CHECK(next == 1);
    // A frame later (16 ms) nothing new; at 485 ms the second beat is inside the 30 ms window.
    plan_count_in(four_beats(100, 4.0 - 0.016 / 0.5), sec(10.016), 30'000'000, {}, next, out);
    CHECK(out.size() == 1);
    plan_count_in(four_beats(100, 4.0 - 0.485 / 0.5), sec(10.485), 30'000'000, {}, next, out);
    REQUIRE(out.size() == 2);
    CHECK(out[1].at_ns == sec(10.5));
    // The same moment again hands nothing over.
    plan_count_in(four_beats(100, 4.0 - 0.485 / 0.5), sec(10.485), 30'000'000, {}, next, out);
    CHECK(out.size() == 2);
}

TEST_CASE("count-in clicks: the speed sets the wall time of each beat") {
    std::vector<TransportOp> out;
    int next = 0;
    plan_count_in(four_beats(50), sec(0), sec(10), {}, next, out);
    REQUIRE(out.size() == 4);
    CHECK(out[1].at_ns == sec(1.0));
    CHECK(out[3].at_ns == sec(3.0));
    out.clear();
    next = 0;
    plan_count_in(four_beats(200), sec(0), sec(10), {}, next, out);
    CHECK(out[3].at_ns == sec(0.75));
}

TEST_CASE("count-in clicks: a speed change mid-count moves the rest, none is lost or doubled") {
    // Two beats counted at 100% (1 s), then the speed halves: planned again from the first beat
    // that has not sounded.
    CountInInput half = four_beats(50, 2.0);
    int next = first_unsounded_count_in_beat(half);
    CHECK(next == 2); // beats 0 and 1 are behind
    std::vector<TransportOp> out;
    plan_count_in(half, sec(1), sec(10), {}, next, out);
    REQUIRE(out.size() == 2);
    CHECK(out[0].at_ns == sec(1)); // beat 2 is due now
    CHECK(out[1].at_ns == sec(2)); // beat 3, a beat later at half speed
    CHECK(out[0].beat == 3);
    CHECK(out[1].beat == 4);
}

TEST_CASE("count-in clicks: a beat due exactly now counts as not sounded yet") {
    CHECK(first_unsounded_count_in_beat(four_beats(100, 3.0)) == 1); // one beat elapsed exactly
    CHECK(first_unsounded_count_in_beat(four_beats(100, 4.0)) == 0);
    CHECK(first_unsounded_count_in_beat(four_beats(100, 3.5)) == 1); // beat 0 sounded, 1 is next
    CHECK(first_unsounded_count_in_beat(four_beats(100, 0.0)) == 4);
}

TEST_CASE("count-in clicks: beats numbered across two bars, and none when no count-in runs") {
    CountInInput two = four_beats();
    two.total_beats = 8;
    two.beats_left = 8.0;
    std::vector<TransportOp> out;
    int next = 0;
    plan_count_in(two, 0, sec(10), {}, next, out);
    REQUIRE(out.size() == 8);
    std::vector<int> beats;
    for (const TransportOp& o : out) {
        beats.push_back(o.beat);
    }
    CHECK(beats == std::vector<int>{1, 2, 3, 4, 1, 2, 3, 4});
    out.clear();
    next = 0;
    plan_count_in(CountInInput{}, 0, sec(10), {}, next, out);
    CHECK(out.empty());
}

TEST_CASE("count-in clicks: a count-in plan with the look-ahead hands the first notes over early") {
    // The look-ahead puts the first note due when the count-in ends, not when it is asked for.
    LookAheadInput in;
    in.position_s = 0.0;
    in.speed_percent = 100;
    in.moving = true;
    in.duration_s = 10.0;
    in.delay_ns = 10'000'000; // the count ends in 10 ms
    double cursor = 0.0;
    std::vector<TransportOp> ops;
    plan_look_ahead(in, sec(5), 30'000'000, cursor, ops);
    REQUIRE(ops.size() == 1);
    CHECK(ops[0].at_ns == sec(5) + 10'000'000); // starts after the delay
    CHECK(ops[0].to_s == doctest::Approx(0.02)); // 20 ms of the 30 ms window are left
    // A count that ends beyond the window hands over nothing.
    in.delay_ns = 40'000'000;
    cursor = 0.0;
    ops.clear();
    plan_look_ahead(in, sec(5), 30'000'000, cursor, ops);
    CHECK(ops.empty());
}

// ---- The click's voice ----------------------------------------------------------------------

TEST_CASE("click playback: a click is a one-shot percussion note, marked as a click") {
    Playback pb;
    pb.set_click_sound(click_sound_for(ClickVoice::WoodBlock, kDefaultClickVolume));
    std::vector<PlaybackMessage> out;
    TransportOp accent;
    accent.kind = Kind::Click;
    accent.at_ns = sec(3);
    accent.accent = true;
    accent.beat = 1;
    pb.apply(accent, out);
    TransportOp sub = accent;
    sub.at_ns = sec(4);
    sub.accent = false;
    sub.sub = 1;
    pb.apply(sub, out);
    TransportOp beat = accent;
    beat.at_ns = sec(5);
    beat.accent = false;
    beat.beat = 2;
    pb.apply(beat, out);
    REQUIRE(out.size() == 6);
    for (const PlaybackMessage& m : out) {
        CHECK(m.click);
        CHECK(m.channel == 9);
    }
    CHECK(out[0].kind == PlaybackMessage::Kind::NoteOn);
    CHECK(out[0].data == 76);
    CHECK(out[0].at_ns == sec(3));
    CHECK(out[1].kind == PlaybackMessage::Kind::NoteOff);
    CHECK(out[1].at_ns == sec(3) + 20'000'000);
    CHECK(out[2].data == 77);
    CHECK(out[2].value < out[4].value); // a subdivision click is quieter than a beat
    CHECK(out[4].value < out[0].value);
    // Nothing of it is a held note: the piano has nothing to stop.
    CHECK(pb.sounding_count() == 0);
}

TEST_CASE("click playback: a muted click sound sends nothing") {
    Playback pb;
    pb.set_click_sound(click_sound_for(ClickVoice::Click, 0));
    std::vector<PlaybackMessage> out;
    TransportOp c;
    c.kind = Kind::Click;
    pb.apply(c, out);
    CHECK(out.empty());
}
