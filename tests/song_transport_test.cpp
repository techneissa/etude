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

TEST_CASE("without a song the transport is hidden") {
    SongController songs;
    SongTransport port(songs);
    port.update(kS);
    CHECK_FALSE(port.view().has_song);
    CHECK(port.view().countdown_left_s == 0.0);
}

TEST_CASE("the skip steps come from the settings and move the song") {
    Fixture f(3, 2, 7);
    CHECK(f.port.view().skip_back_s == 2);
    CHECK(f.port.view().skip_forward_s == 7);
    f.port.skip_forward(2 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(7.0));
    f.port.skip_back(2 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(5.0));
}

TEST_CASE("closing the settings counts down the saved seconds, smoothly, then plays on") {
    Fixture f(4);
    f.port.toggle_play(kS);
    f.at(2 * kS);
    CHECK(f.port.view().playing);
    f.songs.menu_opened(2 * kS);
    f.at(3 * kS);
    CHECK_FALSE(f.port.view().playing);
    f.songs.menu_closed(3 * kS);
    f.at(3 * kS);
    CHECK(f.port.view().countdown_total_s == 4);
    CHECK(f.port.view().countdown_left_s == doctest::Approx(4.0).epsilon(0.01));
    f.at(3 * kS + kS / 2);
    CHECK(f.port.view().countdown_left_s == doctest::Approx(3.5).epsilon(0.01));
    CHECK_FALSE(f.port.view().playing);
    f.at(7 * kS + kS / 10);
    CHECK(f.port.view().countdown_left_s == 0.0);
    CHECK(f.port.view().playing);
}

TEST_CASE("the countdown runs the whole 10 seconds the stepper allows, and no more") {
    // What was asked, and the whole seconds the countdown runs (a value past 10 runs 10).
    for (const auto& [asked, counted] : {std::pair{10, 10}, std::pair{11, 10}, std::pair{1, 1}}) {
        Fixture f(asked);
        f.port.toggle_play(kS);
        f.at(2 * kS);
        f.songs.menu_opened(2 * kS);
        f.at(3 * kS);
        f.songs.menu_closed(3 * kS);
        f.at(3 * kS);
        CHECK(f.port.view().countdown_total_s == counted);
        // Half a second before its end it still counts down and the song waits.
        f.at(3 * kS + counted * kS - kS / 2);
        CHECK(f.port.view().countdown_left_s == doctest::Approx(0.5).epsilon(0.05));
        CHECK_FALSE(f.port.view().playing);
        // A moment after the whole run it is over and the song plays on.
        f.at(3 * kS + counted * kS + kS / 10);
        CHECK(f.port.view().countdown_left_s == 0.0);
        CHECK(f.port.view().playing);
    }
}

TEST_CASE("the song sound goes to the controller") {
    Fixture f;
    f.port.set_sound(ui::SongSound::BothHands, kS);
    CHECK(f.songs.song_sound() == app::SongSound::BothHands);
    CHECK(f.port.view().sound == ui::SongSound::BothHands);
    f.port.set_sound(ui::SongSound::Mute, kS);
    CHECK(f.songs.song_sound() == app::SongSound::Mute);
}

TEST_CASE("replay starts from the beginning and plays") {
    Fixture f;
    f.port.seek(10.0, kS);
    CHECK(f.port.view().position_s == doctest::Approx(10.0));
    f.port.replay(2 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(0.0));
    CHECK(f.port.view().playing);
}

TEST_CASE("the view carries the tempo, the speed and the count-in for the bar's chip") {
    Fixture f;
    const ui::SongView& v = f.port.view();
    CHECK(v.speed_percent == 100);
    CHECK(v.tempo_bpm == doctest::Approx(120.0));
    CHECK(v.main_bpm == doctest::Approx(120.0));
    CHECK(v.count_in_bars == 0); // the controller starts with none; the app sets the session's
    CHECK(v.count_in_beat == 0);
    CHECK_FALSE(v.beat_active);
    f.port.set_speed(70, kS);
    CHECK(v.speed_percent == 70);
    CHECK(v.tempo_bpm == doctest::Approx(84.0));
    f.port.set_target_bpm(60.0, kS);
    CHECK(v.speed_percent == 50);
    CHECK(v.tempo_bpm == doctest::Approx(60.0));
    f.port.set_speed(500, kS);
    CHECK(v.speed_percent == 200);
    CHECK(v.tempo_bpm == doctest::Approx(240.0));
}

TEST_CASE("the view shows the count-in's beat and progress while it counts") {
    Fixture f;
    f.port.set_count_in_bars(1, kS);
    CHECK(f.port.view().count_in_bars == 1);
    f.port.toggle_play(kS);
    CHECK(f.port.view().playing); // counting in is playing
    CHECK(f.port.view().count_in_beat == 1);
    CHECK(f.port.view().count_in_beats == 4);
    CHECK(f.port.view().count_in_progress == doctest::Approx(0.0));
    f.at(kS + 1'250'000'000);
    CHECK(f.port.view().count_in_beat == 3); // 0.5 s a beat: 2.5 beats in
    CHECK(f.port.view().count_in_progress == doctest::Approx(0.625));
    CHECK(f.port.view().position_s == 0.0);
    f.at(kS + 2'100'000'000);
    CHECK(f.port.view().count_in_beat == 0);
    CHECK(f.port.view().position_s > 0.0);
    f.port.set_count_in_bars(7, kS);
    CHECK(f.port.view().count_in_bars == 2); // kept to two bars
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

TEST_CASE("a skip inside a loop that lands inside it just moves") {
    Fixture f(3, 1, 2);
    REQUIRE(f.songs.set_loop(4.0, 10.0, 2 * kS));
    f.port.seek(5.0, 2 * kS);
    f.port.skip_forward(2 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(7.0));
    f.port.skip_back(2 * kS);
    CHECK(f.port.view().position_s == doctest::Approx(6.0));
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
