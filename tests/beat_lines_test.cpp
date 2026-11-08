// SPDX-License-Identifier: GPL-3.0-or-later
#include "etude/abc.hpp"
#include "etude/beat_lines.hpp"

#include <doctest/doctest.h>

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <vector>

using namespace etude;

namespace {

constexpr int kTpq = 480;

struct Bar {
    int numerator;
    int denominator;
};

// A song of `bars` in order (the first a pickup of `pickup_ticks` when that is above 0), with
// `tempo` changes (tick, microseconds a quarter).
Song make_song(const std::vector<Bar>& bars,
               const std::vector<std::pair<std::int64_t, std::uint32_t>>& tempo,
               std::int64_t pickup_ticks = 0) {
    Song s;
    s.tempo.ticks_per_quarter = kTpq;
    s.tempo.changes.clear();
    double seconds = 0.0;
    std::int64_t last_tick = 0;
    std::uint32_t last_us = 500'000;
    for (const auto& [tick, us] : tempo) {
        seconds += static_cast<double>(tick - last_tick) * last_us / 1e6 / kTpq;
        s.tempo.changes.push_back({tick, seconds, us});
        last_tick = tick;
        last_us = us;
    }
    std::int64_t tick = 0;
    int number = pickup_ticks > 0 ? 0 : 1;
    for (std::size_t i = 0; i < bars.size(); ++i) {
        Measure m;
        m.number = number++;
        m.start_tick = tick;
        m.start_s = s.tempo.seconds_at(tick);
        m.numerator = static_cast<std::uint8_t>(bars[i].numerator);
        m.denominator = static_cast<std::uint8_t>(bars[i].denominator);
        s.measures.push_back(m);
        tick += i == 0 && pickup_ticks > 0
                    ? pickup_ticks
                    : static_cast<std::int64_t>(4 * kTpq * bars[i].numerator / bars[i].denominator);
    }
    s.duration_s = s.tempo.seconds_at(tick);
    return s;
}

std::vector<double> times(const Song& song, BeatLines mode, double now, double ahead = 4.0,
                          float area = 1000.0f, BeatLines* level = nullptr,
                          std::size_t capacity = 256) {
    std::array<float, 256> buf{};
    BeatLineWindow w;
    w.mode = mode;
    w.now_s = now;
    w.ahead_s = ahead;
    w.area_px = area;
    w.min_gap_px = 8.0f;
    const BeatLineResult r = beat_line_heights(song, w, std::span<float>(buf.data(), capacity));
    if (level != nullptr) {
        *level = r.level;
    }
    std::vector<double> t;
    for (std::size_t i = 0; i < r.count; ++i) {
        t.push_back(now + static_cast<double>(buf[i]) * ahead);
    }
    return t;
}

void check_times(const std::vector<double>& got, const std::vector<double>& want) {
    REQUIRE(got.size() == want.size());
    for (std::size_t i = 0; i < got.size(); ++i) {
        CHECK(got[i] == doctest::Approx(want[i]).epsilon(1e-4));
    }
}

const std::vector<std::pair<std::int64_t, std::uint32_t>> k120{{0, 500'000}};

} // namespace

TEST_CASE("4/4 at 120: a line each beat, half beat or bar, at song times above the hit line") {
    const Song song = make_song({{4, 4}, {4, 4}, {4, 4}}, k120);
    check_times(times(song, BeatLines::Beats, 0.0), {0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5, 4.0});
    check_times(times(song, BeatLines::Bars, 0.0), {2.0, 4.0});
    CHECK(times(song, BeatLines::HalfBeats, 0.0).size() == 16);
    CHECK(times(song, BeatLines::Off, 0.0).empty());
}

TEST_CASE("the lines are the song's grid, not each note's: they move with the playhead") {
    const Song song = make_song({{4, 4}, {4, 4}, {4, 4}}, k120);
    check_times(times(song, BeatLines::Beats, 0.3, 1.0), {0.5, 1.0});
    // Exactly on a beat: that line is at the hit line, not in the window; the next one is.
    check_times(times(song, BeatLines::Beats, 0.5, 0.5), {1.0});
}

TEST_CASE("the beat follows the meter: dotted quarter in 6/8, half in 2/2, quarter in 3/4") {
    // 6/8 with a dotted quarter of a hair under one second (a quarter is 2/3 s).
    const Song six = make_song({{6, 8}, {6, 8}, {6, 8}}, {{0, 666'666}});
    check_times(times(six, BeatLines::Beats, 0.0), {1.0, 2.0, 3.0, 4.0});
    check_times(times(six, BeatLines::Bars, 0.0), {2.0, 4.0});
    CHECK(times(six, BeatLines::HalfBeats, 0.0).size() == 8);
    const Song cut = make_song({{2, 2}, {2, 2}, {2, 2}}, k120);
    check_times(times(cut, BeatLines::Beats, 0.0), {1.0, 2.0, 3.0, 4.0});
    const Song waltz = make_song({{3, 4}, {3, 4}, {3, 4}}, k120);
    check_times(times(waltz, BeatLines::Bars, 0.0), {1.5, 3.0});
}

TEST_CASE("tempo and meter changes move the lines") {
    // Two bars at 120, then 60: the beat goes from 0.5 s to 1 s at 4 s.
    const Song slow = make_song({{4, 4}, {4, 4}, {4, 4}}, {{0, 500'000}, {3840, 1'000'000}});
    check_times(times(slow, BeatLines::Beats, 3.0, 3.5), {3.5, 4.0, 5.0, 6.0});
    // 3/4 then 4/4 at 120: bars at 1.5 s, then every 2 s.
    const Song change = make_song({{3, 4}, {4, 4}, {4, 4}, {4, 4}}, k120);
    check_times(times(change, BeatLines::Bars, 0.0, 6.0), {1.5, 3.5, 5.5});
    check_times(times(change, BeatLines::Beats, 1.0, 1.0), {1.5, 2.0});
}

TEST_CASE("a pickup bar's beats fall where a full bar's would, counted back from its end") {
    // A one-and-a-half-beat pickup at 120 (0.75 s), then 4/4.
    const Song song = make_song({{4, 4}, {4, 4}, {4, 4}}, k120, 720);
    check_times(times(song, BeatLines::Beats, 0.0, 1.5), {0.25, 0.75, 1.25});
    check_times(times(song, BeatLines::Bars, 0.0, 3.0), {0.75, 2.75});
}

TEST_CASE("lines thin out when they would crowd: half beats, then beats, then bars, then none") {
    const Song song = make_song(std::vector<Bar>(8, Bar{4, 4}), k120);
    BeatLines level = BeatLines::Off;
    // 1000 px for 4 s: a half beat (0.25 s) is 62.5 px apart.
    CHECK(times(song, BeatLines::HalfBeats, 0.0, 4.0, 1000.0f, &level).size() == 16);
    CHECK(level == BeatLines::HalfBeats);
    // 100 px: half beats 6.25 px (under 8), beats 12.5 px.
    CHECK(times(song, BeatLines::HalfBeats, 0.0, 4.0, 100.0f, &level).size() == 8);
    CHECK(level == BeatLines::Beats);
    // 40 px: beats 5 px, bars 20 px.
    CHECK(times(song, BeatLines::HalfBeats, 0.0, 4.0, 40.0f, &level).size() == 2);
    CHECK(level == BeatLines::Bars);
    // 10 px: bars 5 px, none.
    CHECK(times(song, BeatLines::Beats, 0.0, 4.0, 10.0f, &level).empty());
    CHECK(level == BeatLines::Off);
    // Exactly at the minimum gap the finer level still shows: 8 px for half a beat.
    CHECK(times(song, BeatLines::HalfBeats, 0.0, 4.0, 128.0f, &level).size() == 16);
    CHECK(level == BeatLines::HalfBeats);
    // Too many for the buffer: the next coarser level.
    CHECK(times(song, BeatLines::HalfBeats, 0.0, 4.0, 1000.0f, &level, 10).size() == 8);
    CHECK(level == BeatLines::Beats);
}

TEST_CASE("no lines for an empty song, an empty area, a window with no time or past the end") {
    const Song empty;
    CHECK(times(empty, BeatLines::Beats, 0.0).empty());
    const Song song = make_song({{4, 4}}, k120);
    CHECK(times(song, BeatLines::Beats, 0.0, 4.0, 0.0f).empty());
    CHECK(times(song, BeatLines::Beats, 0.0, 0.0).empty());
    CHECK(times(song, BeatLines::Beats, 10.0).empty());
    // The song's last bar ends at 2 s: no line at its end, where nothing starts.
    check_times(times(song, BeatLines::Beats, 0.0), {0.5, 1.0, 1.5});
}

namespace {

Song load_test_song(const char* name) {
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/scripts/test-data/" + name,
                     std::ios::binary);
    const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    const ScoreLoadResult r =
        load_abc(reinterpret_cast<const std::uint8_t*>(text.data()), text.size());
    REQUIRE(r.ok);
    return r.song;
}

} // namespace

TEST_CASE("a real 6/8 song: a line every dotted quarter, two to a bar") {
    const Song song = load_test_song("beat-lines-6-8.abc");
    check_times(times(song, BeatLines::Beats, 0.0, 4.5), {1.0, 2.0, 3.0, 4.0});
    check_times(times(song, BeatLines::Bars, 0.0, 4.5), {2.0, 4.0});
}

TEST_CASE("a real song with a tempo change and a meter change") {
    const Song song = load_test_song("beat-lines-tempo.abc");
    // Bars 1-2 at 120 (2 s each), bars 3-4 at 60 (4 s each), then 3/4 at 60 (3 s).
    check_times(times(song, BeatLines::Bars, 0.0, 15.0), {2.0, 4.0, 8.0, 12.0, 15.0});
    check_times(times(song, BeatLines::Beats, 3.0, 2.0), {3.5, 4.0, 5.0});
}
