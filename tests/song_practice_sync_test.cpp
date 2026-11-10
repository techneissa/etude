// SPDX-License-Identifier: GPL-3.0-or-later
// A song's speed, count-in, hands and transposition in the history: restored when it is opened,
// kept per song across a switch and a restart, the defaults for a song never opened, applied late
// when the history had not loaded, the player's change winning over a late arrival.
#include "song_practice_sync.hpp"

#include "alloc_count.hpp"
#include "history_store.hpp"
#include "song_transport.hpp"
#include "sections_controller.hpp"
#include "song_controller.hpp"

#include "etude/session_restore.hpp"
#include "etude/song_builder.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdint>
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
        path = fs::temp_directory_path() /
               ("etude-practice-" + std::to_string(std::random_device{}()));
        fs::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        fs::remove_all(path, ec);
    }
};

HistoryOptions quiet() {
    HistoryOptions o;
    o.sync_commits = false;
    o.log = [](std::string_view) {};
    return o;
}

// Two bars of 4/4 with both hands, whose content hash and file name are given, and `backing`
// accompaniment parts (tracks 2, 3, ... on their own channels).
LoadedSong song_of(std::uint64_t hash, const std::string& path, int backing = 0) {
    Song s;
    s.tempo = make_tempo_map(480, 120.0);
    for (int bar = 0; bar < 2; ++bar) {
        for (int beat = 0; beat < 4; ++beat) {
            Note n;
            n.start_tick = (bar * 4 + beat) * 480;
            n.end_tick = n.start_tick + 400;
            n.pitch = static_cast<std::uint8_t>(72 + beat * 2);
            n.velocity = 80;
            n.hand = Hand::Right;
            s.notes.push_back(n);
        }
        Note l;
        l.start_tick = bar * 4 * 480;
        l.end_tick = l.start_tick + 4 * 480 - 10;
        l.pitch = 48;
        l.velocity = 70;
        l.hand = Hand::Left;
        l.channel = 1;
        l.track = 1;
        s.notes.push_back(l);
    }
    for (int b = 0; b < backing; ++b) {
        Note n;
        n.start_tick = 0;
        n.end_tick = 960;
        n.pitch = static_cast<std::uint8_t>(40 + b);
        n.velocity = 70;
        n.hand = Hand::Unknown;
        n.channel = static_cast<std::uint8_t>(2 + b);
        n.track = static_cast<std::uint16_t>(2 + b);
        n.accompaniment = true;
        s.notes.push_back(n);
    }
    REQUIRE(finish_song(s));
    for (Part& part : s.parts) {
        part.accompaniment = part.track >= 2;
        part.name = part.accompaniment ? "Backing " + std::to_string(part.track - 1) : part.name;
    }
    LoadedSong out;
    out.song = std::move(s);
    out.title = "Test";
    out.hash = hash;
    out.library_path = path;
    return out;
}

// What a session of the app has: a store, a controller and the sync between them, on one clock.
struct Rig {
    HistoryStore history;
    SongController songs;
    SongPracticeSync sync;
    std::int64_t now = kSec;

    explicit Rig(const fs::path& dir, bool wait_for_history = true)
        : history(dir, quiet()), sync(history, songs) {
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
    void open(std::uint64_t hash, const std::string& path, bool from_session = false,
              int backing = 0) {
        sync.song_closing();
        songs.load(song_of(hash, path, backing), now);
        sync.song_opened(from_session, now);
    }
    // The saved session's song comes back the way start-up does it (SongPracticeSync::
    // restore_session, with plan_restore deciding the outcome).
    RestorePlan restore(const SessionState& session, bool found, bool hash_matches,
                        std::uint64_t file_hash, const std::string& path) {
        const LoadedSong loaded = song_of(file_hash, path);
        const RestorePlan plan = plan_restore(session, found, hash_matches, &loaded.song);
        sync.restore_session(
            plan, plan.state,
            [&](bool from_session) {
                sync.song_closing();
                songs.load(song_of(file_hash, path), now);
                sync.song_opened(from_session, now);
            },
            now);
        return plan;
    }
    // Frames go by for `ms`, one update each 100 ms.
    void frames(int ms) {
        for (int t = 0; t < ms; t += 100) {
            now += 100 * kMs;
            sync.update(now);
        }
    }
    // The history thread has loaded: the frame that takes it in.
    void history_arrives() {
        REQUIRE(history.wait_until_settled(10s));
        history.poll();
        now += 16 * kMs;
        sync.update(now);
    }
    void change_all() {
        songs.set_speed(70, now);
        songs.set_count_in_bars(2);
        songs.set_practice_hands(HandFilter::Left, now);
        songs.set_transpose(3, now);
    }
};

// The last-song session as the app saved it.
SessionState session_of(std::uint64_t hash) {
    SessionState s;
    s.song_path = "a.mid";
    s.song_hash = hash;
    s.speed_percent = 85;
    s.count_in_bars = 0;
    s.practice_hands = HandFilter::Right;
    s.transpose_semitones = -1;
    return s;
}

SongPractice session_practice() {
    SongPractice p;
    p.speed_percent = 85;
    p.count_in_bars = 0;
    p.hands = HandFilter::Right;
    p.transpose_semitones = -1;
    return p;
}

// A history with a row for song 0xA1 (speed 70, count-in 2, left hand, +3), closed again.
void store_changed_row(const fs::path& dir) {
    Rig rig(dir);
    rig.open(0xA1, "a.mid");
    rig.change_all();
    rig.frames(600);
    REQUIRE(rig.history.flush());
}

SongPractice changed() {
    SongPractice p;
    p.speed_percent = 70;
    p.count_in_bars = 2;
    p.hands = HandFilter::Left;
    p.transpose_semitones = 3;
    return p;
}

} // namespace

TEST_CASE("song practice sync: a song's choices survive a restart") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.practice() == SongPractice{});
        rig.change_all();
        rig.frames(600);
        REQUIRE(rig.history.flush());
        CHECK(rig.history.song_practice().at(0xA1) == changed());
    }
    Rig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK(again.songs.practice() == changed());
    CHECK(again.songs.speed_percent() == 70);
    CHECK(again.songs.count_in_bars() == 2);
    CHECK(again.songs.practice_hands() == HandFilter::Left);
    CHECK(again.songs.transpose() == 3);
}

TEST_CASE("song practice sync: a song opened on a section loops it over its other choices") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.change_all();
    rig.frames(600);
    // The library's section chip: the loop on [2, 4] s, the rest of what the song was left on.
    rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
    rig.open(0xA1, "a.mid");
    const SongPractice now = rig.songs.practice();
    CHECK(now.loop_enabled);
    CHECK(now.loop_start_s == doctest::Approx(2.0));
    CHECK(now.loop_end_s == doctest::Approx(4.0).epsilon(0.01));
    CHECK(now.speed_percent == 70);
    CHECK(now.hands == HandFilter::Left);
    // It starts at the loop, not before it.
    CHECK(rig.songs.position_s() == doctest::Approx(2.0));
}

TEST_CASE("song practice sync: a section chip's loop is for that open and is never saved") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.songs.set_speed(70, rig.now);
        rig.frames(600);
        const int saves = rig.sync.saves();
        rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
        rig.open(0xA1, "a.mid");
        REQUIRE(rig.songs.practice().loop_enabled);
        rig.frames(600);
        CHECK(rig.sync.saves() == saves); // the loop alone writes nothing
        // A change of the player's on top of it is saved, without the loop.
        rig.songs.set_speed(80, rig.now);
        rig.frames(600);
        REQUIRE(rig.history.flush());
        const SongPractice row = rig.history.song_practice().at(0xA1);
        CHECK(row.speed_percent == 80);
        CHECK_FALSE(row.loop_enabled);
        CHECK(row.loop_end_s == 0.0);
        // A plain open afterwards does not loop.
        rig.open(0xA1, "a.mid");
        CHECK_FALSE(rig.songs.practice().has_loop());
        CHECK(rig.songs.speed_percent() == 80);
    }
    Rig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK_FALSE(again.songs.practice().has_loop());
    CHECK(again.songs.speed_percent() == 80);
}

TEST_CASE("song practice sync: a loop the player sets over a chip's loop is theirs and is saved") {
    TempDir dir;
    Rig rig(dir.path);
    rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
    rig.open(0xA1, "a.mid");
    REQUIRE(rig.songs.practice().loop_enabled);
    REQUIRE(rig.songs.set_loop(0.5, 3.0, rig.now));
    rig.frames(600);
    REQUIRE(rig.history.flush());
    const SongPractice row = rig.history.song_practice().at(0xA1);
    CHECK(row.loop_enabled);
    CHECK(row.loop_start_s == doctest::Approx(0.5));
}

TEST_CASE("song practice sync: a loop for a song that did not open is forgotten") {
    TempDir dir;
    Rig rig(dir.path);
    // The request's load failed: the window clears the loop, and the next open has none.
    rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
    rig.sync.clear_opening_loop();
    rig.open(0xA1, "a.mid");
    CHECK_FALSE(rig.songs.practice().has_loop());
    // A newer request replaces the one before.
    rig.sync.open_with_loop(0xA1, 2.0, 4.0, 0);
    rig.sync.open_with_loop(0xB2, 6.0, 8.0, 0);
    rig.open(0xA1, "a.mid");
    CHECK_FALSE(rig.songs.practice().has_loop());
}

TEST_CASE("song practice sync: a section asked for another song does not loop this one") {
    TempDir dir;
    Rig rig(dir.path);
    rig.sync.open_with_loop(0xB2, 2.0, 4.0, 0);
    rig.open(0xA1, "a.mid");
    CHECK_FALSE(rig.songs.practice().has_loop());
    // And it was forgotten: opening the song it was for gives it no loop either.
    rig.open(0xB2, "b.mid");
    CHECK_FALSE(rig.songs.practice().has_loop());
}

TEST_CASE("song practice sync: switching songs keeps each one's choices") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.songs.set_speed(70, rig.now);
    rig.songs.set_practice_hands(HandFilter::Left, rig.now);
    rig.frames(600);

    rig.open(0xB2, "b.mid");
    // A song never opened has the defaults, not what the last one was left on.
    CHECK(rig.songs.practice() == SongPractice{});
    rig.songs.set_speed(150, rig.now);
    rig.songs.set_transpose(-2, rig.now);
    rig.frames(600);

    rig.open(0xA1, "a.mid");
    CHECK(rig.songs.speed_percent() == 70);
    CHECK(rig.songs.practice_hands() == HandFilter::Left);
    CHECK(rig.songs.transpose() == 0);
    rig.open(0xB2, "b.mid");
    CHECK(rig.songs.speed_percent() == 150);
    CHECK(rig.songs.transpose() == -2);
    CHECK(rig.songs.practice_hands() == HandFilter::Both);
}

TEST_CASE("song practice sync: a change made in the frame a song is replaced is kept") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.songs.set_speed(80, rig.now);
    rig.open(0xB2, "b.mid"); // no update in between: song_closing writes it
    rig.open(0xA1, "a.mid");
    CHECK(rig.songs.speed_percent() == 80);
}

TEST_CASE("song practice sync: a song never opened has the defaults whatever came before") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.change_all();
    rig.frames(600);
    rig.open(0xC3, "c.mid");
    CHECK(rig.songs.practice() == SongPractice{});
    CHECK(rig.songs.count_in_bars() == 1);
    // Looking at it writes nothing.
    rig.frames(1000);
    CHECK(rig.history.song_practice().count(0xC3) == 0);
}

TEST_CASE("song practice sync: a renamed or moved file keeps its choices (the hash is the key)") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "old/name.mid");
    rig.songs.set_speed(60, rig.now);
    rig.frames(600);
    rig.open(0xB2, "b.mid");
    rig.open(0xA1, "elsewhere/new name.mid");
    CHECK(rig.songs.speed_percent() == 60);
}

TEST_CASE("song practice sync: a drag is written once, after it stops") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    for (int speed = 100; speed >= 60; speed -= 5) {
        rig.songs.set_speed(speed, rig.now);
        rig.frames(100);
    }
    // Still moving (each step within the delay of the last): nothing stored yet.
    CHECK(rig.history.song_practice().count(0xA1) == 0);
    rig.frames(500);
    REQUIRE(rig.history.song_practice().count(0xA1) == 1);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 60);
}

TEST_CASE("song practice sync: flush writes a change at once") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.songs.set_speed(90, rig.now);
        rig.frames(100);
        rig.sync.flush();
        REQUIRE(rig.history.flush());
    }
    Rig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK(again.songs.speed_percent() == 90);
}

TEST_CASE("song practice sync: a song opened before the history loaded gets its choices when they "
          "arrive") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.change_all();
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    Rig late(dir.path, false);
    REQUIRE(late.history.loading());
    late.open(0xA1, "a.mid");
    CHECK(late.songs.practice() == SongPractice{}); // the defaults until then
    late.frames(500);
    late.history_arrives();
    CHECK(late.songs.practice() == changed());
    // Taking them in is not the player's change: nothing is written back.
    late.frames(1000);
    CHECK(late.sync.saves() == 0);
    CHECK(late.history.song_practice().at(0xA1) == changed());
}

TEST_CASE("song practice sync: a late arrival does not undo what the player changed meanwhile") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.change_all();
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    Rig late(dir.path, false);
    late.open(0xA1, "a.mid");
    late.songs.set_speed(120, late.now);
    late.songs.set_practice_hands(HandFilter::Right, late.now);
    late.history_arrives();
    SongPractice want = changed();
    want.speed_percent = 120;
    want.hands = HandFilter::Right;
    CHECK(late.songs.practice() == want); // theirs where they changed it, the stored for the rest
    late.frames(600);
    REQUIRE(late.history.flush());
    CHECK(late.history.song_practice().at(0xA1) == want);
}

TEST_CASE("song practice sync: nothing is written while the history is still loading") {
    TempDir dir;
    Rig late(dir.path, false);
    late.open(0xA1, "a.mid");
    late.songs.set_speed(120, late.now);
    late.frames(1000);
    CHECK(late.history.song_practice().count(0xA1) == 0);
    late.history_arrives(); // no row for it: the player's change is kept and then saved
    CHECK(late.songs.speed_percent() == 120);
    late.frames(600);
    CHECK(late.history.song_practice().at(0xA1).speed_percent == 120);
}

TEST_CASE("song practice sync: the saved session's song keeps all four of the session's values "
          "and teaches the history, whether the history has loaded or not") {
    for (const bool late : {false, true}) {
        CAPTURE(late);
        TempDir dir;
        store_changed_row(dir.path);
        Rig rig(dir.path, !late);
        const RestorePlan plan = rig.restore(session_of(0xA1), true, true, 0xA1, "a.mid");
        REQUIRE(plan.outcome == RestoreOutcome::Restored);
        CHECK(rig.songs.practice() == session_practice());
        if (late) {
            rig.history_arrives();
        }
        rig.frames(1000);
        CHECK(rig.songs.practice() == session_practice());
        CHECK(rig.history.song_practice().at(0xA1) == session_practice());
        CHECK(rig.sync.saves() == 1);
    }
}

TEST_CASE("song practice sync: a file that changed since the session keeps its own row, not the "
          "session's defaults") {
    for (const bool late : {false, true}) {
        CAPTURE(late);
        TempDir dir;
        store_changed_row(dir.path);
        Rig rig(dir.path, !late);
        // The session knew the song under another hash: the file changed.
        const RestorePlan plan = rig.restore(session_of(0x9999), true, false, 0xA1, "a.mid");
        REQUIRE(plan.outcome == RestoreOutcome::SongChanged);
        if (late) {
            CHECK(rig.songs.practice() == SongPractice{});
            rig.history_arrives();
        }
        CHECK(rig.songs.practice() == changed());
        rig.frames(1000);
        CHECK(rig.songs.practice() == changed());
        CHECK(rig.sync.saves() == 0);
    }
}

TEST_CASE("song practice sync: a session with no song or a missing one loads nothing and saves "
          "nothing") {
    TempDir dir;
    Rig rig(dir.path);
    SessionState none;
    const RestorePlan plan = rig.restore(none, false, false, 0xA1, "a.mid");
    CHECK(plan.outcome == RestoreOutcome::NoSong);
    CHECK_FALSE(rig.songs.has_song());
    rig.frames(1000);
    CHECK(rig.sync.saves() == 0);
    CHECK(rig.history.song_practice().empty());
}

TEST_CASE("song practice sync: --song takes all four from the session for the session's own song "
          "and from the history for another") {
    TempDir dir;
    store_changed_row(dir.path);
    {
        // The session's own song: the session's values, the history learns them.
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.practice() == changed());
        const SessionState left = session_of(0xA1);
        rig.songs.restore_song_settings(left, rig.now);
        rig.sync.session_applied();
        CHECK(rig.songs.practice() == session_practice());
        rig.frames(1000);
        CHECK(rig.songs.practice() == session_practice());
        CHECK(rig.history.song_practice().at(0xA1) == session_practice());
    }
    {
        // Another file: the session's hash does not match, so the controller keeps what the open
        // gave it (the history's row, here the defaults).
        Rig rig(dir.path);
        rig.open(0xB2, "b.mid");
        rig.songs.restore_song_settings(session_of(0xA1), rig.now);
        CHECK(rig.songs.practice() == SongPractice{});
    }
}

TEST_CASE("song practice sync: a change made before the history loaded is kept when the song is "
          "switched first") {
    TempDir dir;
    store_changed_row(dir.path);
    Rig late(dir.path, false);
    late.open(0xA1, "a.mid");
    late.songs.set_speed(120, late.now);
    late.open(0xB2, "b.mid"); // away before the history is in
    late.songs.set_transpose(4, late.now);
    late.open(0xA1, "a.mid"); // and back: its change is still there
    CHECK(late.songs.speed_percent() == 120);
    late.history_arrives();
    SongPractice want = changed();
    want.speed_percent = 120;
    // A1 takes its stored row under the player's speed; B2 had no row: its change alone is written.
    CHECK(late.songs.practice() == want);
    late.frames(1000);
    CHECK(late.history.song_practice().at(0xA1) == want);
    REQUIRE(late.history.song_practice().count(0xB2) == 1);
    SongPractice b;
    b.transpose_semitones = 4;
    CHECK(late.history.song_practice().at(0xB2) == b);
}

TEST_CASE("song practice sync: a change made before the history loaded is kept when the app "
          "quits first") {
    TempDir dir;
    store_changed_row(dir.path);
    {
        Rig late(dir.path, false);
        late.open(0xA1, "a.mid");
        late.songs.set_count_in_bars(0);
        late.sync.flush(); // quitting: waits for the history, lays the change over the stored row
        REQUIRE(late.history.flush());
        SongPractice want = changed();
        want.count_in_bars = 0;
        CHECK(late.history.song_practice().at(0xA1) == want);
    }
    Rig again(dir.path);
    again.open(0xA1, "a.mid");
    CHECK(again.songs.speed_percent() == 70);
    CHECK(again.songs.count_in_bars() == 0);
}

TEST_CASE("song practice sync: a song replaced without song_closing still keeps its last change") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.songs.set_speed(55, rig.now);
    rig.frames(100); // seen, not yet written
    CHECK(rig.history.song_practice().count(0xA1) == 0);
    rig.songs.load(song_of(0xB2, "b.mid"), rig.now); // behind the sync's back
    rig.frames(100);
    REQUIRE(rig.history.song_practice().count(0xA1) == 1);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 55);
}

TEST_CASE("song practice sync: the options of this run are not saved") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.songs.set_speed(66, rig.now);
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    Rig rig(dir.path);
    rig.open(0xA1, "a.mid");
    rig.songs.set_speed(30, rig.now); // --speed 30
    rig.sync.adopt();
    rig.frames(1000);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 66);
    rig.songs.set_speed(35, rig.now); // a later change is
    rig.frames(600);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 35);
}

TEST_CASE("song practice sync: a song without a hash is left alone") {
    TempDir dir;
    Rig rig(dir.path);
    rig.open(0, "nohash.mid");
    rig.songs.set_speed(77, rig.now);
    rig.frames(1000);
    CHECK(rig.history.song_practice().empty());
}

// ---- Accompaniment and the loop ------------------------------------------------------------------

TEST_CASE("song practice sync: a song's muted accompaniment parts are kept for that song") {
    TempDir dir;
    std::vector<std::uint32_t> keys;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid", false, 2);
        REQUIRE(rig.songs.accompaniment_parts().size() == 2);
        keys = all_keys(rig.songs.accompaniment_parts());
        CHECK(rig.songs.accompaniment_state() == AccompanimentState::On); // the default: sounding
        rig.songs.set_accompaniment_muted({keys[0]}, rig.now);
        CHECK(rig.songs.accompaniment_state() == AccompanimentState::Partial);
        rig.frames(600);
        rig.open(0xB2, "b.mid", false, 2); // another song with parts: its own, sounding
        CHECK(rig.songs.accompaniment_state() == AccompanimentState::On);
        rig.songs.set_accompaniment_muted(keys, rig.now);
        rig.frames(600);
        rig.open(0xA1, "a.mid", false, 2);
        CHECK(rig.songs.accompaniment_muted() == std::vector<std::uint32_t>{keys[0]});
        REQUIRE(rig.history.flush());
    }
    Rig again(dir.path);
    again.open(0xB2, "b.mid", false, 2);
    CHECK(again.songs.accompaniment_state() == AccompanimentState::Off);
    again.open(0xA1, "a.mid", false, 2);
    CHECK(again.songs.accompaniment_muted() == std::vector<std::uint32_t>{keys[0]});
    // Nothing was written by only looking: opening, and leaving B for A, are not changes.
    again.frames(1000);
    CHECK(again.sync.saves() == 0);
}

TEST_CASE("song practice sync: the global default decides a song the player never chose for") {
    TempDir dir;
    Rig rig(dir.path);
    rig.sync.set_accompaniment_default(false);
    rig.open(0xA1, "a.mid", false, 2);
    CHECK(rig.songs.accompaniment_state() == AccompanimentState::Off);
    rig.frames(1000);
    CHECK(rig.sync.saves() == 0); // the default is not a choice: nothing written
    // Another choice on the song leaves the parts to the default.
    rig.songs.set_speed(80, rig.now);
    rig.frames(600);
    REQUIRE(rig.history.song_practice().count(0xA1) == 1);
    CHECK_FALSE(rig.history.song_practice().at(0xA1).parts_set);
    CHECK(rig.history.song_practice().at(0xA1).speed_percent == 80);
    // The player lets one part sound: now it is their choice and it is kept.
    const std::vector<std::uint32_t> keys = all_keys(rig.songs.accompaniment_parts());
    rig.songs.set_accompaniment_muted({keys[1]}, rig.now);
    rig.frames(600);
    CHECK(rig.history.song_practice().at(0xA1).parts_set);
    CHECK(rig.history.song_practice().at(0xA1).muted_parts == std::vector<std::uint32_t>{keys[1]});
    // The default changes: that song keeps its choice, a new one follows the default.
    rig.sync.set_accompaniment_default(true);
    rig.open(0xB2, "b.mid", false, 2);
    CHECK(rig.songs.accompaniment_state() == AccompanimentState::On);
    rig.open(0xA1, "a.mid", false, 2);
    CHECK(rig.songs.accompaniment_muted() == std::vector<std::uint32_t>{keys[1]});
}

TEST_CASE("song practice sync: don't ask again for a song's octave shift comes back with it after "
          "a restart") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.songs.never_offer_shift();
        rig.frames(600);
        REQUIRE(rig.history.song_practice().count(0xA1) == 1);
        CHECK(rig.history.song_practice().at(0xA1).no_shift_offer);
        rig.open(0xB2, "b.mid");
        SongPractice b;
        rig.songs.practice_into(b);
        CHECK_FALSE(b.no_shift_offer); // the choice is the song's own
        REQUIRE(rig.history.flush());
    }
    Rig again(dir.path);
    again.open(0xB2, "b.mid");
    SongPractice b;
    again.songs.practice_into(b);
    CHECK_FALSE(b.no_shift_offer);
    again.open(0xA1, "a.mid");
    SongPractice a;
    again.songs.practice_into(a);
    CHECK(a.no_shift_offer);
    // Looking at it writes nothing.
    again.frames(1000);
    CHECK(again.sync.saves() == 0);
}

TEST_CASE("song practice sync: the loop comes back with its song, cut to the song") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        REQUIRE(rig.songs.set_loop(1.0, 3.0, rig.now));
        rig.frames(600);
        rig.open(0xB2, "b.mid");
        CHECK_FALSE(rig.songs.loop_enabled()); // the loop belongs to A
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.loop_enabled());
        CHECK(rig.songs.loop_start_s() == doctest::Approx(1.0));
        CHECK(rig.songs.loop_end_s() == doctest::Approx(3.0));
        CHECK(rig.songs.position_s() == doctest::Approx(1.0)); // it starts in the loop
        // Ending it is a change too.
        rig.songs.clear_loop(rig.now);
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    {
        Rig again(dir.path);
        again.open(0xA1, "a.mid");
        CHECK_FALSE(again.songs.loop_enabled());
        // A stored loop that runs past the song's end is cut to it, and a loop that cut leaves too
        // short is dropped.
        SongPractice p;
        p.loop_start_s = 2.0;
        p.loop_end_s = 100.0;
        p.loop_enabled = true;
        again.history.set_song_practice(0xC3, p);
        p.loop_start_s = 3.9;
        again.history.set_song_practice(0xD4, p);
        again.open(0xC3, "c.mid");
        CHECK(again.songs.loop_enabled());
        CHECK(again.songs.loop_start_s() == doctest::Approx(2.0));
        CHECK(again.songs.loop_end_s() == doctest::Approx(again.songs.duration_s()));
        again.open(0xD4, "d.mid");
        CHECK_FALSE(again.songs.loop_enabled());
    }
}

TEST_CASE("song practice sync: a late arrival brings the parts and the loop unless they were "
          "changed") {
    TempDir dir;
    std::vector<std::uint32_t> keys;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid", false, 2);
        keys = all_keys(rig.songs.accompaniment_parts());
        rig.songs.set_accompaniment_muted({keys[0]}, rig.now);
        REQUIRE(rig.songs.set_loop(1.0, 3.0, rig.now));
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    {
        Rig late(dir.path, false);
        late.open(0xA1, "a.mid", false, 2);
        CHECK(late.songs.accompaniment_state() == AccompanimentState::On);
        CHECK_FALSE(late.songs.loop_enabled());
        late.history_arrives();
        CHECK(late.songs.accompaniment_muted() == std::vector<std::uint32_t>{keys[0]});
        CHECK(late.songs.loop_enabled());
        late.frames(1000);
        CHECK(late.sync.saves() == 0);
    }
    {
        // The player muted the other part before it arrived: that wins, the loop still comes.
        Rig late(dir.path, false);
        late.open(0xA1, "a.mid", false, 2);
        late.songs.set_accompaniment_muted({keys[1]}, late.now);
        late.history_arrives();
        CHECK(late.songs.accompaniment_muted() == std::vector<std::uint32_t>{keys[1]});
        CHECK(late.songs.loop_enabled());
    }
}

// ---- The section and the practice list item ---------------------------------------------------

TEST_CASE("song practice sync: the section looped comes back through the sections, a missing id "
          "is dropped and a changed one follows the row") {
    TempDir dir;
    SectionLibrary library; // what the settings file would carry from one run to the next
    SectionId first = kNoSection;
    SectionId second = kNoSection;
    {
        Rig rig(dir.path);
        SectionsController sections(rig.songs, library);
        rig.sync.set_sections(&sections);
        rig.open(0xA1, "a.mid");
        sections.update(rig.now);
        REQUIRE(sections.split_at(2.0, rig.now)); // two sections: 0..2 s and 2..4 s
        sections.update(rig.now);
        REQUIRE(sections.view().sections.size() == 2);
        first = sections.view().sections[0].id;
        second = sections.view().sections[1].id;
        sections.select_section(second, rig.now);
        sections.update(rig.now);
        CHECK(sections.view().looping == second);
        rig.frames(600);
        CHECK(rig.history.song_practice().at(0xA1).section_id == static_cast<int>(second));
        REQUIRE(rig.history.flush());
    }
    {
        Rig again(dir.path);
        SectionsController sections(again.songs, library);
        again.sync.set_sections(&sections);
        // The same song: its section is looped again, through the sections.
        again.open(0xA1, "a.mid");
        sections.update(again.now);
        CHECK(sections.view().looping == second);
        CHECK(again.songs.loop_enabled());
        CHECK(again.songs.loop_start_s() == doctest::Approx(2.0));
        CHECK(again.songs.loop_end_s() == doctest::Approx(again.songs.duration_s()));
        again.frames(1000);
        CHECK(again.sync.saves() == 0); // restoring is not a change

        // A row whose section is gone: no section is looped (the plain loop it holds stays).
        SongPractice gone;
        gone.section_id = 999;
        gone.plan_item = 3;
        gone.loop_start_s = 1.0;
        gone.loop_end_s = 3.0;
        gone.loop_enabled = true;
        again.history.set_song_practice(0xC3, gone);
        again.open(0xC3, "c.mid");
        sections.update(again.now);
        CHECK(sections.view().looping == kNoSection);
        CHECK_FALSE(sections.view().plan_running);
        CHECK(again.songs.loop_enabled());
        CHECK(again.songs.loop_start_s() == doctest::Approx(1.0));

        // The row changed to the other section: that one is looped.
        SongPractice changed_row = again.history.song_practice().at(0xA1);
        changed_row.section_id = static_cast<int>(first);
        changed_row.loop_start_s = 0.0;
        changed_row.loop_end_s = 2.0;
        again.history.set_song_practice(0xA1, changed_row);
        again.open(0xB2, "b.mid");
        again.open(0xA1, "a.mid");
        sections.update(again.now);
        CHECK(sections.view().looping == first);
        CHECK(again.songs.loop_end_s() == doctest::Approx(2.0));
    }
}

TEST_CASE("song practice sync: a frame with accompaniment parts muted allocates nothing") {
    TempDir dir;
    Rig rig(dir.path);
    SongTransport port(rig.songs);
    rig.open(0xA1, "a.mid", false, 3);
    const std::vector<std::uint32_t> keys = all_keys(rig.songs.accompaniment_parts());
    rig.songs.set_accompaniment_muted({keys[0], keys[2]}, rig.now);
    rig.frames(600); // saved: nothing changes from here
    port.update(rig.now);
    port.update(rig.now); // the port's own caches are built
    rig.frames(200);
    etude::test::start_counting_allocations();
    for (int i = 0; i < 20; ++i) {
        rig.now += 16 * kMs;
        port.update(rig.now);
        rig.sync.update(rig.now);
        (void)rig.songs.accompaniment_state();
    }
    CHECK(etude::test::stop_counting_allocations() == 0);
    CHECK(port.view().accompaniment == ui::AccompState::Partial);
}

TEST_CASE("song practice sync: a song's hand split is kept per song and survives a restart") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.hand_split() == kAutoSplit); // a song never opened splits as before
        rig.songs.set_hand_split(67, rig.now);
        rig.frames(600);
        rig.open(0xB2, "b.mid");
        CHECK(rig.songs.hand_split() == kAutoSplit); // not what the last song was left on
        rig.songs.set_hand_split(55, rig.now);
        rig.frames(600);
        rig.open(0xA1, "a.mid");
        CHECK(rig.songs.hand_split() == 67);
        REQUIRE(rig.history.flush());
        CHECK(rig.history.song_practice().at(0xA1).split_point == 67);
        CHECK(rig.history.song_practice().at(0xB2).split_point == 55);
    }
    Rig again(dir.path);
    again.open(0xB2, "b.mid");
    CHECK(again.songs.hand_split() == 55);
    again.open(0xA1, "a.mid");
    CHECK(again.songs.hand_split() == 67);
    // Back to Auto is saved too, not left at the old point.
    again.songs.set_hand_split(kAutoSplit, again.now);
    again.frames(600);
    REQUIRE(again.history.flush());
    CHECK(again.history.song_practice().at(0xA1).split_point == kAutoSplit);
}

TEST_CASE("song practice sync: the saved session's song gets its hand split from the history, "
          "which the session does not keep") {
    for (const bool late : {false, true}) {
        CAPTURE(late);
        TempDir dir;
        {
            Rig rig(dir.path);
            rig.open(0xA1, "a.mid");
            rig.songs.set_hand_split(62, rig.now);
            rig.change_all();
            rig.frames(600);
            REQUIRE(rig.history.flush());
        }
        Rig rig(dir.path, !late);
        const RestorePlan plan = rig.restore(session_of(0xA1), true, true, 0xA1, "a.mid");
        REQUIRE(plan.outcome == RestoreOutcome::Restored);
        if (late) {
            rig.history_arrives();
        }
        rig.frames(1000);
        SongPractice want = session_practice(); // the session's four values stay
        want.split_point = 62;                  // and the history's split comes in
        CHECK(rig.songs.practice() == want);
        CHECK(rig.history.song_practice().at(0xA1) == want);
    }
}

TEST_CASE(
    "song practice sync: a hand split chosen before the history loaded wins over the stored one") {
    TempDir dir;
    {
        Rig rig(dir.path);
        rig.open(0xA1, "a.mid");
        rig.songs.set_hand_split(70, rig.now);
        rig.frames(600);
        REQUIRE(rig.history.flush());
    }
    Rig late(dir.path, false);
    late.open(0xA1, "a.mid");
    late.songs.set_hand_split(58, late.now);
    late.history_arrives();
    CHECK(late.songs.hand_split() == 58);
    late.frames(600);
    REQUIRE(late.history.flush());
    CHECK(late.history.song_practice().at(0xA1).split_point == 58);

    // Not changed meanwhile: the stored one arrives.
    Rig quiet_rig(dir.path, false);
    quiet_rig.open(0xA1, "a.mid");
    quiet_rig.history_arrives();
    CHECK(quiet_rig.songs.hand_split() == 58);
}
