// SPDX-License-Identifier: GPL-3.0-or-later
// The song controller: real songs, the library scan, the stuck-note property, seeks, and which runs are saved.
#include "song_controller_test_support.hpp"
#include "song_controller.hpp"
#include "song_library.hpp"

#include "etude/song_builder.hpp"
#include "etude/song_schedule.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <thread>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace song_test;

TEST_CASE("a real song: Bach's prelude loads, splits its hands and plays to the end") {
    const SongFileLoad load =
        load_song_file(std::string(ETUDE_TEST_DATA_DIR) + "/bach-bwv846-prelude.mid");
    REQUIRE(load.ok);
    CHECK(load.song.hash != 0);
    CHECK_FALSE(load.song.title.empty());
    for (const Note& n : load.song.song.notes) {
        if (!n.drum) {
            REQUIRE(n.hand != Hand::Unknown);
        }
    }
    SongController c;
    std::int64_t now = kSec;
    c.load(load.song, now);
    CHECK(c.bar_count() >= 35);
    std::vector<VisibleNote> notes;
    c.visible_notes(4.0, notes);
    CHECK_FALSE(notes.empty());
    REQUIRE(c.key_range().has_value());
    c.set_song_sound(SongSound::BothHands, now);
    c.set_speed(150, now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 5 * kSec);
    CHECK(piano.note_ons > 20);
    c.seek(c.duration_s() - 0.5, now);
    run(c, piano, now, now + 2 * kSec);
    CHECK(c.state() == TransportState::Ended);
    CHECK(piano.silent());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("the key range is every note as drawn: Satie's low A1 folds above its D2 on 61 keys") {
    // The left hand reaches A1 (33), below a 61-key keyboard (C2, 36): it folds up to A2 (45).
    // The song's D2 (38) and G2 (43) are drawn below that, so the range starts at D2. Folding only
    // the lowest pitch gave A2, and the key view widened and narrowed as D2 and G2 came by.
    const SongFileLoad load =
        load_song_file(std::string(ETUDE_TEST_DATA_DIR) + "/satie-gymnopedie-1.mid");
    REQUIRE(load.ok);
    SongController c;
    std::int64_t now = kSec;
    c.load(load.song, now);
    c.set_keyboard(kRange61, now);
    const std::optional<KeyRange> r = c.key_range();
    REQUIRE(r.has_value());
    CHECK(r->lowest == 38);
    CHECK(r->highest == 81);
    // Every note on screen lies inside it, whenever it is asked.
    std::vector<VisibleNote> notes;
    for (double at = 0.0; at < c.duration_s(); at += 7.0) {
        c.seek(at, now);
        c.visible_notes(6.0, notes);
        for (const VisibleNote& v : notes) {
            const Note& n = c.song().song.notes[v.index];
            if (v.playable && !n.drum && !n.accompaniment) {
                CHECK(v.pitch >= r->lowest);
                CHECK(v.pitch <= r->highest);
            }
        }
        CHECK(c.key_range()->lowest == r->lowest);
    }
    // A transposition is worked out again: two semitones up, the range as those notes are drawn.
    c.set_transpose(2, now);
    const std::optional<KeyRange> up =
        drawn_key_range(c.song().song.notes, Transposition(2, kRange61));
    REQUIRE(up.has_value());
    CHECK(c.key_range()->lowest == up->lowest);
    CHECK(c.key_range()->highest == up->highest);
    CHECK(up->highest == 83);
    c.set_transpose(0, now);
    // So is a new keyboard.
    c.set_keyboard(kRange88, now);
    CHECK(c.key_range()->lowest == 33);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a file that is not a song gives one line, and nothing loads") {
    const std::vector<std::uint8_t> junk{'h', 'e', 'l', 'l', 'o'};
    const SongFileLoad load = load_song_bytes(junk, "notes.mid");
    CHECK_FALSE(load.ok);
    CHECK_FALSE(load.error.empty());
    CHECK(load.error.find('\n') == std::string::npos);
    const SongFileLoad missing = load_song_file("/nonexistent/x.mid");
    CHECK_FALSE(missing.ok);
    CHECK(missing.error.find("x.mid") != std::string::npos);
}

TEST_CASE("the library lists the test songs by name before parsing any, then fills them in") {
    SongLibrary lib({std::string(ETUDE_TEST_DATA_DIR)});
    const std::size_t n = lib.scan();
    CHECK(n >= 4);
    // Listed and in name order, with nothing parsed yet.
    CHECK(lib.state().pending() == n);
    CHECK_FALSE(lib.state().settled());
    for (std::size_t i = 1; i < lib.size(); ++i) {
        // By file name; the tunes of one ABC file follow each other in tune order.
        CHECK(split_abc_location(lib.state().items[i - 1].relative).file <=
              split_abc_location(lib.state().items[i].relative).file);
    }
    lib.start_loading();
    lib.wait_until_settled();
    CHECK(lib.state().settled());
    // An ABC file of several tunes is replaced by them once read, so the list can be longer.
    CHECK(lib.state().loaded() + lib.state().failed() == lib.size());
    CHECK(lib.size() >= n);
    CHECK(lib.unreadable() == 0);
    for (std::size_t i = 0; i < lib.size(); ++i) {
        const LibraryItem& item = lib.state().items[i];
        CHECK(item.state == EntryState::Loaded);
        CHECK_FALSE(item.entry.title.empty());
        CHECK(item.entry.duration_s > 0.0);
        CHECK(lib.resolve(item.relative) == lib.path_at(i));
        CHECK(lib.relative_path(lib.path_at(i), lib.tune_at(i)) == item.relative);
        CHECK(lib.index_of(lib.path_at(i), lib.tune_at(i)) == i);
    }
    CHECK(lib.relative_path("/etc/hosts").empty());
    CHECK_FALSE(lib.resolve("../tests/data/bach-bwv846-prelude.mid").has_value());
    CHECK_FALSE(lib.resolve("/etc/hosts").has_value());
    CHECK(lib.step(std::nullopt, 1) == std::size_t{0});
    CHECK(lib.step(std::size_t{0}, -1) == lib.size() - 1);
    CHECK(lib.step(lib.size() - 1, 1) == std::size_t{0});
}

TEST_CASE(
    "the library parses on this thread too, keeps results across a rescan and caches by hash") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "etude_lib_test_cache";
    fs::remove_all(dir);
    fs::create_directories(dir / "a");
    fs::create_directories(dir / "b");
    const fs::path src = fs::path(ETUDE_TEST_DATA_DIR) / "satie-gymnopedie-1.mid";
    fs::copy_file(src, dir / "a" / "x.mid");
    fs::copy_file(src, dir / "b" / "x.mid"); // same bytes, same name: parsed once
    fs::copy_file(src, dir / "c.mid");       // same bytes, another name: its own title
    {
        std::ofstream junk(dir / "broken.mid", std::ios::binary);
        junk << "this is not a midi file";
    }
    SongLibrary lib({dir});
    CHECK(lib.scan() == 4);
    lib.load_all_now();
    CHECK(lib.state().loaded() == 3);
    CHECK(lib.state().failed() == 1);
    CHECK(lib.unreadable() == 1);
    CHECK(lib.cache_hits() == 1);
    // Stepping passes over the song that failed.
    std::optional<std::size_t> at = lib.step(std::nullopt, 1);
    for (std::size_t i = 0; i < 6; ++i) {
        REQUIRE(at.has_value());
        CHECK(lib.state().items[*at].state == EntryState::Loaded);
        at = lib.step(at, 1);
    }
    // A rescan with nothing changed keeps every result and has nothing to parse.
    CHECK(lib.scan() == 4);
    CHECK(lib.state().pending() == 0);
    CHECK(lib.state().loaded() == 3);
    // A changed file goes back to Pending.
    {
        std::ofstream more(dir / "c.mid", std::ios::binary | std::ios::app);
        more << "x";
    }
    CHECK(lib.scan() == 4);
    CHECK(lib.state().pending() == 1);
    fs::remove_all(dir);
}

TEST_CASE("the first scan opens no song file") {
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "etude_lib_test_lazy";
    fs::remove_all(dir);
    fs::create_directories(dir);
    for (int i = 0; i < 50; ++i) {
        fs::copy_file(fs::path(ETUDE_TEST_DATA_DIR) / "bach-bwv846-prelude.mid",
                      dir / ("song" + std::to_string(i) + ".mid"));
    }
    SongLibrary lib({dir});
    CHECK(lib.scan() == 50);
    CHECK(lib.state().pending() == 50);
    CHECK(lib.cache_hits() == 0);
    // Destroying the library with the worker running stops it cleanly.
    lib.start_loading();
    fs::remove_all(dir);
}

// The stuck-note rule, over random action sequences: whatever the player does, every note the
// song starts is stopped by a later note-off, and after a pause, a seek, a song change, a menu or
// a lost focus nothing of the song is left sounding.
TEST_CASE("no action sequence leaves a note sounding (property)") {
    std::mt19937 rng(20261012);
    const LoadedSong songs[2] = {two_hand_song(6), two_hand_song(3)};
    int total_note_ons = 0;
    int silent_checks = 0;
    std::size_t total_cancelled = 0;
    for (int trial = 0; trial < 300; ++trial) {
        SongController c;
        FakePiano piano;
        std::int64_t now = kSec;
        c.load(songs[0], now);
        c.set_song_sound(SongSound::BothHands, now);
        SongOptions o;
        o.resume_countdown_s = 1;
        o.metronome = rng() % 2 == 0;
        o.metronome_grid.subdivision = static_cast<ClickSubdivision>(rng() % 3);
        c.set_options(o, now);
        c.set_count_in_bars(static_cast<int>(rng() % 3));
        bool menu = false;
        for (int step = 0; step < 60; ++step) {
            const int action = static_cast<int>(rng() % 25);
            bool must_be_silent = false;
            const int ons_before = piano.note_ons;
            switch (action) {
            case 0:
            case 1:
            case 2:
                c.toggle_play(now);
                must_be_silent = !c.playing() && !c.counting_down();
                break;
            case 3:
                c.pause(now);
                must_be_silent = true;
                break;
            case 4:
                c.seek(std::uniform_real_distribution<double>(-1.0, 14.0)(rng), now);
                must_be_silent = true;
                break;
            case 5:
                c.skip_back(now);
                must_be_silent = true;
                break;
            case 6:
                c.skip_forward(now);
                must_be_silent = true;
                break;
            case 7:
                c.replay(now);
                break;
            case 8:
                c.change_speed(rng() % 2 == 0 ? 25 : -25, now);
                break;
            case 9:
                c.set_loop_start_here(now);
                break;
            case 10:
                c.set_loop_end_here(now);
                break;
            case 11:
                c.clear_loop(now);
                break;
            case 12:
                c.set_practice_hands(static_cast<HandFilter>(rng() % 3), now);
                break;
            case 13:
                c.cycle_song_sound(now);
                break;
            case 14:
                c.toggle_mode(now);
                break;
            case 15:
                c.set_transpose(static_cast<int>(rng() % 9) - 4, now);
                break;
            case 16:
                if (menu) {
                    c.menu_closed(now);
                } else {
                    c.menu_opened(now);
                    must_be_silent = true;
                }
                menu = !menu;
                break;
            case 17:
                c.lost_focus(now);
                must_be_silent = true;
                break;
            case 18:
                c.load(songs[rng() % 2], now);
                must_be_silent = true;
                break;
            case 19:
                c.set_keyboard(rng() % 2 == 0 ? KeyRange{36, 96} : KeyRange{21, 108}, now);
                break;
            case 20: {
                const std::array<MidiEvent, 2> presses{note_on(72, now), note_on(48, now)};
                run(c, piano, now, now + 50 * kMs, presses);
                break;
            }
            case 21:
                o.metronome = !o.metronome;
                c.set_options(o, now);
                break;
            case 22:
                c.set_count_in_bars(static_cast<int>(rng() % 3));
                break;
            case 23:
                o.metronome_grid.subdivision = static_cast<ClickSubdivision>(rng() % 3);
                o.click_volume = rng() % 5 == 0 ? 0 : kDefaultClickVolume;
                c.set_options(o, now);
                break;
            default:
                break;
            }
            drain(c, piano, now);
            if (must_be_silent) {
                // Messages may wait a few ms behind a tick's note-off; let them out.
                // Time runs on past the look-ahead: a note that was cancelled must not start in it.
                drain(c, piano, now + ClickSound{}.length_ns + 2 * kSongSoundLeadNs);
                INFO("trial " << trial << " step " << step << " action " << action);
                if (c.playing()) {
                    // The song goes on from the new place: what sounds started after the action.
                    CHECK(piano.count() <= static_cast<std::size_t>(piano.note_ons - ons_before));
                } else {
                    CHECK(piano.count() == 0);
                }
                ++silent_checks;
            }
            run(c, piano, now, now + static_cast<std::int64_t>(rng() % 900) * kMs);
        }
        shutdown_into(c, piano, now);
        INFO("trial " << trial);
        total_note_ons += piano.note_ons;
        total_cancelled += piano.cancelled;
        CHECK(piano.silent());
        CHECK(piano.queue.empty());
        CHECK_FALSE(c.needs_silence());
        CHECK(c.queued_events() == 0);
    }
    // The sequences did make the song sound, and did stop it often, with notes still scheduled.
    CHECK(total_note_ons > 1000);
    CHECK(silent_checks > 3000);
    CHECK(total_cancelled > 30);
}

TEST_CASE("after a seek while playing, the song sounds again from the new place") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(4), now);
    c.set_song_sound(SongSound::BothHands, now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 1 * kSec);
    const int before = piano.note_ons;
    CHECK(before > 0);
    c.skip_forward(now);
    drain(c, piano, now);
    CHECK(piano.count() == 0);
    run(c, piano, now, now + 1 * kSec);
    drain(c, piano, now + kSongSoundLeadNs); // the last notes of the second start a lead later
    CHECK(c.playing());
    CHECK(piano.note_ons > before + 1);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}

TEST_CASE("a run where nothing was played is not saved") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    FakePiano piano;
    c.play(now);
    run(c, piano, now, now + 6 * kSec);
    CHECK(c.state() == TransportState::Ended);
    CHECK(c.take_finished_runs().empty());
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
    CHECK(c.take_finished_runs().empty());
}

TEST_CASE("a run of only wrong presses is still saved, with no hits") {
    SongController c;
    std::int64_t now = kSec;
    c.load(two_hand_song(), now);
    FakePiano piano;
    c.play(now);
    // C#4 is in no bar of the song: a press that matches no note.
    const std::int64_t at = now + 1 * kSec;
    run(c, piano, now, at);
    const std::array<MidiEvent, 1> wrong{note_on(61, now)};
    run(c, piano, now, now + 16 * kMs, wrong);
    run(c, piano, now, at + 6 * kSec);
    REQUIRE(c.state() == TransportState::Ended);
    const auto runs = c.take_finished_runs();
    REQUIRE(runs.size() == 1);
    CHECK(runs[0].record.wrong >= 1);
    CHECK(runs[0].record.all.hit == 0);
    CHECK(runs[0].record.all.graded > 0);
    std::vector<SongSoundEvent> out;
    c.shutdown(now, out);
}
