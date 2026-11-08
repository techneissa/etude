// SPDX-License-Identifier: GPL-3.0-or-later
// A Learn drill opened in the player (#373) is played as the card asks: at its own tempo, 100%
// speed, no loop and no transposition, whatever practice choices are saved under its hash (the same
// scale at the same tempo is the same drill), and nothing it is played with is written.
#include "history_store.hpp"
#include "song_controller.hpp"
#include "song_practice_sync.hpp"

#include "etude/drill.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <filesystem>
#include <random>
#include <string>

using namespace etude;
using namespace etude::app;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

constexpr std::int64_t kMs = 1'000'000;
constexpr std::int64_t kSec = 1'000'000'000;

struct TempDir {
    fs::path path;
    TempDir() {
        path = fs::temp_directory_path() / ("etude-drill-sync-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

HistoryOptions quiet() {
    HistoryOptions o;
    o.log = [](std::string_view) {};
    return o;
}

// The drill the card makes for G major at 120 beats a minute, as the window loads it.
LoadedSong drill_song() {
    DrillSpec spec;
    spec.bpm = 120;
    const Drill made = make_drill(spec);
    REQUIRE(made.ok);
    LoadedSong out;
    out.song = made.song;
    out.title = made.title;
    out.hash = made.hash;
    return out;
}

struct Rig {
    HistoryStore history;
    SongController songs;
    SongPracticeSync sync;
    std::int64_t now = kSec;

    Rig(const fs::path& dir, bool wait_for_history) : history(dir, quiet()), sync(history, songs) {
        SongOptions o;
        o.default_count_in_bars = 1;
        songs.set_options(o, now);
        history.open();
        if (wait_for_history) {
            REQUIRE(history.wait_until_settled(10s));
            history.poll();
        }
    }
    ~Rig() {
        std::vector<SongSoundEvent> out;
        songs.shutdown(now, out);
    }
    void frames(int ms) {
        for (int t = 0; t < ms; t += 100) {
            now += 100 * kMs;
            sync.update(now);
        }
    }
};

} // namespace

TEST_CASE("drill practice: a drill opens at 100% speed with no loop whatever is saved under its hash, "
          "and nothing it is played with is written") {
    const std::uint64_t hash = drill_song().hash;
    TempDir dir;
    double card_tempo = 0.0; // the tempo the card asked for: the drill at 100% speed
    {
        // The same drill played once as a song: speed 70, a loop, +3 saved under its hash.
        Rig rig(dir.path, true);
        rig.sync.song_closing();
        rig.songs.load(drill_song(), rig.now);
        rig.sync.song_opened(false, rig.now);
        card_tempo = rig.songs.tempo_bpm();
        REQUIRE(card_tempo > 0.0);
        rig.songs.set_speed(70, rig.now);
        rig.songs.set_transpose(3, rig.now);
        REQUIRE(rig.songs.set_loop(0.5, 1.5, rig.now));
        rig.frames(600);
        REQUIRE(rig.history.flush());
        REQUIRE(rig.history.song_practice().count(hash) == 1);
    }
    for (const bool loaded : {true, false}) {
        CAPTURE(loaded);
        Rig rig(dir.path, loaded);
        rig.sync.song_closing();
        rig.songs.load(drill_song(), rig.now);
        rig.sync.drill_opened(rig.now);
        if (!loaded) {
            REQUIRE(rig.history.wait_until_settled(10s));
            rig.history.poll(); // the stored row comes in over a drill: it must not apply
            rig.now += 16 * kMs;
            rig.sync.update(rig.now);
        }
        CHECK(rig.songs.speed_percent() == 100);
        // The tempo it plays at is the card's, not the 70% of it that is saved under the hash.
        CHECK(rig.songs.tempo_bpm() == doctest::Approx(card_tempo));
        CHECK(rig.songs.tempo_bpm() == doctest::Approx(rig.songs.main_tempo_bpm()));
        CHECK(rig.songs.tempo_bpm() != doctest::Approx(0.7 * card_tempo));
        CHECK_FALSE(rig.songs.loop_enabled());
        CHECK(rig.songs.transpose() == 0);
        // The player slows it down: that is not saved under the drill's hash either.
        rig.songs.set_speed(60, rig.now);
        const int saves = rig.sync.saves();
        rig.frames(600);
        rig.sync.song_closing();
        CHECK(rig.sync.saves() == saves);
        CHECK(rig.history.song_practice().at(hash).speed_percent == 70);
    }
}
