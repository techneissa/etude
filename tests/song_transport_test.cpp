// SPDX-License-Identifier: GPL-3.0-or-later
#include "song_transport.hpp"

#include "etude/song_builder.hpp"

#include <doctest/doctest.h>

#include <utility>

using namespace etude;
using etude::app::SongController;
using etude::app::SongTransport;
namespace ui = etude::app::ui;

namespace {

constexpr std::int64_t kS = 1'000'000'000;

// 16 bars of quarter notes at 120 BPM: 32 s.
app::LoadedSong test_song() {
    app::LoadedSong loaded;
    Song& song = loaded.song;
    constexpr int kTpq = 480;
    song.tempo = make_tempo_map(kTpq, 120.0);
    for (int beat = 0; beat < 64; ++beat) {
        Note n;
        n.start_tick = static_cast<std::int64_t>(beat) * kTpq;
        n.end_tick = n.start_tick + kTpq - 20;
        n.pitch = static_cast<std::uint8_t>(60 + beat % 8);
        n.hand = Hand::Right;
        song.notes.push_back(n);
    }
    finish_song(song);
    loaded.title = "Test";
    loaded.hash = 7;
    return loaded;
}

struct Fixture {
    SongController songs;
    SongTransport port{songs};

    explicit Fixture(int resume_s = 3, int back_s = 5, int forward_s = 5) {
        app::SongOptions o;
        o.resume_countdown_s = resume_s;
        o.skip_back_seconds = back_s;
        o.skip_forward_seconds = forward_s;
        songs.set_options(o, kS);
        songs.load(test_song(), kS);
        songs.update(kS, {});
        port.update(kS);
    }
    void at(std::int64_t now) {
        songs.update(now, {});
        port.update(now);
    }
};

} // namespace

TEST_CASE("the view shows the loaded song paused at the start, with its bars") {
    Fixture f;
    const ui::SongView& v = f.port.view();
    CHECK(v.has_song);
    CHECK(v.title == "Test");
    CHECK_FALSE(v.playing);
    CHECK(v.position_s == 0.0);
    CHECK(v.duration_s == doctest::Approx(32.0).epsilon(0.05));
    CHECK(v.bar_starts_s.size() == 16);
}

TEST_CASE("a skip forward inside a loop wraps to the loop start") {
    Fixture f(3, 5, 7);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(8.0, 2 * kS);
    f.port.skip_forward(2 * kS); // 15 s is past the loop end
    CHECK(f.port.view().position_s == doctest::Approx(4.0));
    CHECK(f.port.view().loop_enabled);
    f.port.skip_forward(2 * kS); // 11 s is still past it
    CHECK(f.port.view().position_s == doctest::Approx(4.0));
}

TEST_CASE("a skip back inside a loop stops at the loop start") {
    Fixture f(3, 5, 5);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(6.0, 2 * kS);
    f.port.skip_back(2 * kS); // 1 s is before the loop start
    CHECK(f.port.view().position_s == doctest::Approx(4.0));
    CHECK(f.port.view().loop_enabled);
}

TEST_CASE("a scrub outside the loop turns the loop off, one inside keeps it") {
    Fixture f;
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(7.0, 2 * kS);
    CHECK(f.port.view().loop_enabled);
    CHECK(f.port.view().position_s == doctest::Approx(7.0));
    f.port.seek(20.0, 2 * kS);
    CHECK_FALSE(f.port.view().loop_enabled);
    CHECK(f.port.view().position_s == doctest::Approx(20.0));
    REQUIRE(f.songs.set_loop(4.0, 10.0, 3 * kS));
    f.port.seek(1.0, 3 * kS);
    CHECK_FALSE(f.port.view().loop_enabled);
}

TEST_CASE("playing on after a scrub out of the loop does not wrap") {
    Fixture f;
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(5.0, 2 * kS);
    f.port.toggle_play(2 * kS);
    f.port.seek(12.0, 2 * kS);
    f.at(3 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(13.0).epsilon(0.05));
    CHECK_FALSE(f.port.view().loop_enabled);
}

TEST_CASE("a skip from outside the loop that lands outside it ends the loop") {
    Fixture f(3, 5, 5);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(20.0, 2 * kS);
    CHECK_FALSE(f.port.view().loop_enabled);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 3 * kS));
    f.port.skip_forward(3 * kS); // from 20 s: the position was outside the loop already
    CHECK_FALSE(f.port.view().loop_enabled);
    CHECK(f.port.view().position_s == doctest::Approx(25.0));
}

TEST_CASE("a skip from before the loop into it keeps the loop") {
    Fixture f(3, 5, 5);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.skip_forward(2 * kS); // from 0 s, 5 s is inside the loop
    CHECK(f.port.view().loop_enabled);
    CHECK(f.port.view().position_s == doctest::Approx(5.0));
}

TEST_CASE("a skip back at the start of the song that stays outside the loop ends it") {
    Fixture f(3, 5, 5);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.skip_back(2 * kS);
    CHECK_FALSE(f.port.view().loop_enabled);
    CHECK(f.port.view().position_s == doctest::Approx(0.0));
}
