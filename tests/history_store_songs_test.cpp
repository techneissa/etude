// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: a song's kept details and practice, and the upgrade of older files that gain them.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// ---- Song settings

TEST_CASE("history: a song's name, composer, genre, star and date added are kept by its hash") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPrefs p;
        p.title = "  My   Waltz ";
        p.composer = "Me";
        p.favourite = true;
        p.added_utc_s = 5000;
        store.set_song_prefs(0x1234, p);
        store.set_song_prefs(0, p); // no hash: ignored
        // Many at once: one transaction.
        std::vector<std::pair<std::uint64_t, SongPrefs>> many;
        for (std::uint64_t h = 1; h <= 50; ++h) {
            SongPrefs q;
            q.added_utc_s = 7000;
            many.emplace_back(h, q);
        }
        store.set_song_prefs(many);
        CHECK(store.song_prefs().size() == 51);
        CHECK(store.song_prefs().at(0x1234).title == "My Waltz"); // sanitized at once
        // A later date added never replaces an earlier one; an edit keeps it.
        SongPrefs later = store.song_prefs().at(0x1234);
        later.added_utc_s = 9000;
        later.favourite = false;
        later.genre = "Dances";
        store.set_song_prefs(0x1234, later);
        CHECK(store.song_prefs().at(0x1234).added_utc_s == 5000);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.song_prefs().size() == 51);
    const SongPrefs& p = again.song_prefs().at(0x1234);
    CHECK(p.title == "My Waltz");
    CHECK(p.composer == "Me");
    CHECK(p.genre == "Dances");
    CHECK_FALSE(p.favourite);
    CHECK(p.added_utc_s == 5000);
    CHECK(again.song_prefs().at(50).added_utc_s == 7000);
    // A hash with the top bit set round-trips.
    again.set_song_prefs(0xFEDCBA9876543210ULL, SongPrefs{"Big", "", "", true, 1});
    REQUIRE(again.flush());
    Raw raw(again.path());
    CHECK(raw.num("SELECT COUNT(*) FROM song_settings") == 52);
}

TEST_CASE("history: the version of a piece the player chose last is kept, and an old file gets the column") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPrefs p;
        p.favourite = true;
        p.chosen_utc_s = 4242;
        store.set_song_prefs(0x77, p);
        SongPrefs bad;
        bad.chosen_utc_s = -5; // a negative time is none
        store.set_song_prefs(0x78, bad);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.song_prefs().at(0x77).chosen_utc_s == 4242);
    CHECK(again.song_prefs().at(0x77).favourite);
    CHECK(again.song_prefs().at(0x78).chosen_utc_s == 0);
    Raw raw(again.path());
    CHECK(raw.num("SELECT chosen_utc_s FROM song_settings WHERE content_hash = 119") == 4242);
}

TEST_CASE("history: what this session set before the history loaded wins over the file") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.set_song_prefs(7, SongPrefs{"Old name", "", "", false, 100});
        REQUIRE(store.flush());
    }
    HistoryStore again(dir.path, quiet());
    again.set_song_prefs(7, SongPrefs{"New name", "", "", true, 0}); // before open: kept in memory
    again.open();
    REQUIRE(again.wait_until_settled(10s));
    CHECK(again.poll().loaded);
    CHECK(again.song_prefs().at(7).title == "New name");
    CHECK(again.song_prefs().at(7).favourite);
    REQUIRE(again.flush());
    Raw raw(again.path());
    // The file kept the earliest date added under the new name.
    CHECK(raw.num("SELECT added_utc_s FROM song_settings WHERE content_hash = 7") == 100);
    CHECK(raw.num("SELECT favourite FROM song_settings WHERE content_hash = 7") == 1);
}

TEST_CASE("history: a version 1 database moves to the latest version and keeps every run") {
    TempDir dir;
    {
        // A file as version 1 left it: version 2's table is not there yet.
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.add_run(sample_run(2));
        REQUIRE(store.flush());
    }
    {
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("DROP TABLE song_settings; DROP TABLE song_group_members; "
                                  "DROP TABLE song_groups; DROP TABLE song_sources; "
                                  "DROP TABLE version_overrides; DROP TABLE song_practice; "
                                  "DROP TABLE song_practice_parts; PRAGMA user_version = 1") == SQLITE_OK);
    }
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.runs().size() == 2);
    CHECK(store.runs()[1] == sample_run(2));
    CHECK(store.song_prefs().empty());
    store.set_song_prefs(3, SongPrefs{"", "", "", true, 0});
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM song_settings") == 1);
}

TEST_CASE("history: a version 5 database gains the version overrides and keeps what it had") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.set_song_prefs(3, SongPrefs{"", "", "", true, 0});
        REQUIRE(store.flush());
    }
    {
        // A file as version 5 left it: everything but the overrides' table (migration 6).
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("DROP TABLE version_overrides; DROP TABLE song_practice; "
                                  "DROP TABLE song_practice_parts; PRAGMA user_version = 5") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.runs().size() == 1);
        CHECK(store.song_prefs().size() == 1);
        CHECK(store.version_overrides().empty());
        store.edit_version_overrides({VersionEdit::Kind::Split, 0xAA, 0});
        store.edit_version_overrides({VersionEdit::Kind::Join, 0xB1, 0xB2});
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM version_overrides") == 2);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.version_overrides().split(0xAA));
    CHECK(again.version_overrides().joined(0xB2));
    CHECK(again.runs().size() == 1);
}

TEST_CASE("history: a version 6 database gains the song practice and keeps what it had") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.set_song_prefs(3, SongPrefs{"", "", "", true, 0});
        store.edit_version_overrides({VersionEdit::Kind::Split, 0xAA, 0});
        REQUIRE(store.flush());
    }
    {
        // A file as version 6 left it: everything but the practice table (migration 7).
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("DROP TABLE song_practice; DROP TABLE song_practice_parts; "
                                  "PRAGMA user_version = 6") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.runs().size() == 1);
        CHECK(store.song_prefs().size() == 1);
        CHECK(store.version_overrides().split(0xAA));
        CHECK(store.song_practice().empty());
        SongPractice p;
        p.speed_percent = 60;
        p.hands = HandFilter::Right;
        store.set_song_practice(0xC0FFEE, p);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice") == 1);
        CHECK(raw.num("SELECT hands FROM song_practice") == 2);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.song_practice().count(0xC0FFEE) == 1);
    CHECK(again.song_practice().at(0xC0FFEE).speed_percent == 60);
    CHECK(again.runs().size() == 1);
}

TEST_CASE("history: a version 7 database gains the loop and the muted parts and keeps its rows") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        SongPractice p;
        p.speed_percent = 60;
        p.hands = HandFilter::Left;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
    }
    {
        // A file as version 7 left it: song_practice without the added columns, no parts table.
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("DROP TABLE song_practice_parts; "
                                  "ALTER TABLE song_practice DROP COLUMN no_shift_offer; "
                                  "ALTER TABLE song_practice DROP COLUMN loop_start_ms; "
                                  "ALTER TABLE song_practice DROP COLUMN loop_end_ms; "
                                  "ALTER TABLE song_practice DROP COLUMN loop_enabled; "
                                  "ALTER TABLE song_practice DROP COLUMN section_id; "
                                  "ALTER TABLE song_practice DROP COLUMN plan_item; "
                                  "ALTER TABLE song_practice DROP COLUMN parts_set; "
                                  "ALTER TABLE song_practice DROP COLUMN split_point; "
                                  "PRAGMA user_version = 7") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.runs().size() == 1);
        REQUIRE(store.song_practice().count(0xA1) == 1);
        const SongPractice& kept = store.song_practice().at(0xA1);
        CHECK(kept.speed_percent == 60);
        CHECK(kept.hands == HandFilter::Left);
        CHECK_FALSE(kept.parts_set);
        CHECK(kept.muted_parts.empty());
        CHECK_FALSE(kept.has_loop());
        SongPractice p = kept;
        p.parts_set = true;
        p.muted_parts = {0x0102, 0x0304};
        p.loop_start_s = 12.5;
        p.loop_end_s = 20.25;
        p.loop_enabled = true;
        p.section_id = 3;
        p.plan_item = 2;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice_parts") == 2);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.song_practice().count(0xA1) == 1);
    const SongPractice& back = again.song_practice().at(0xA1);
    CHECK(back.parts_set);
    CHECK(back.muted_parts == std::vector<std::uint32_t>{0x0102, 0x0304});
    CHECK(back.loop_start_s == doctest::Approx(12.5));
    CHECK(back.loop_end_s == doctest::Approx(20.25));
    CHECK(back.loop_enabled);
    CHECK(back.section_id == 3);
    CHECK(back.plan_item == 2);
    CHECK(back.speed_percent == 60);
}

TEST_CASE("history: the octave-shift offer turned off is saved with the song, and a version 9 "
          "database gains the column and keeps its rows") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        SongPractice p;
        p.speed_percent = 60;
        p.no_shift_offer = true;
        store.set_song_practice(0xA1, p);
        SongPractice q; // another song that never turned it off
        q.speed_percent = 70;
        store.set_song_practice(0xB2, q);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    {
        // Saved and read back.
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK(store.song_practice().at(0xA1).no_shift_offer);
        CHECK(store.song_practice().at(0xA1).speed_percent == 60);
        CHECK_FALSE(store.song_practice().at(0xB2).no_shift_offer);
        // And taken back: the row says so.
        SongPractice p = store.song_practice().at(0xA1);
        p.no_shift_offer = false;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        CHECK_FALSE(store.song_practice().at(0xA1).no_shift_offer);
        SongPractice p = store.song_practice().at(0xA1);
        p.no_shift_offer = true;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
    }
    {
        // A file as version 9 left it: song_practice without the column.
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("ALTER TABLE song_practice DROP COLUMN no_shift_offer; "
                                  "ALTER TABLE song_practice DROP COLUMN split_point; "
                                  "PRAGMA user_version = 9") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.runs().size() == 1);
        REQUIRE(store.song_practice().count(0xA1) == 1);
        CHECK(store.song_practice().at(0xA1).speed_percent == 60);
        CHECK_FALSE(store.song_practice().at(0xA1).no_shift_offer); // nothing was turned off yet
        SongPractice p = store.song_practice().at(0xA1);
        p.no_shift_offer = true;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT no_shift_offer FROM song_practice WHERE speed_percent = 60") == 1);
        CHECK(raw.num("SELECT no_shift_offer FROM song_practice WHERE speed_percent = 70") == 0);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.song_practice().at(0xA1).no_shift_offer);
    CHECK_FALSE(again.song_practice().at(0xB2).no_shift_offer);
    CHECK(again.runs().size() == 1);
}

TEST_CASE("history: the muted parts of a song are replaced whole, and one song's never reach "
          "another's") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPractice p;
        p.parts_set = true;
        p.muted_parts = {1, 2, 3};
        store.set_song_practice(0xA1, p);
        SongPractice q;
        q.parts_set = true;
        q.muted_parts = {9};
        store.set_song_practice(0xB2, q);
        p.muted_parts = {2}; // the player let two parts sound again
        store.set_song_practice(0xA1, p);
        SongPractice none; // never chosen: no rows
        store.set_song_practice(0xC3, none);
        REQUIRE(store.flush());
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.song_practice().at(0xA1).muted_parts == std::vector<std::uint32_t>{2});
    CHECK(again.song_practice().at(0xB2).muted_parts == std::vector<std::uint32_t>{9});
    CHECK_FALSE(again.song_practice().at(0xC3).parts_set);
    CHECK(again.song_practice().at(0xC3).muted_parts.empty());
}

TEST_CASE("history: song practice is stored by content hash, sanitized, one row a song") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPractice p;
        p.speed_percent = 80;
        p.transpose_semitones = -2;
        store.set_song_practice(0xFFFFFFFFFFFFFFF0ULL, p); // the top bit set, as a hash can be
        p.speed_percent = 90;                              // the same song again: one row
        store.set_song_practice(0xFFFFFFFFFFFFFFF0ULL, p);
        p.speed_percent = 9999;
        store.set_song_practice(0x2, p);
        store.set_song_practice(0, p); // no hash: not kept
        REQUIRE(store.flush());
        CHECK(store.song_practice().size() == 2);
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice") == 2);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    REQUIRE(again.song_practice().size() == 2);
    CHECK(again.song_practice().at(0xFFFFFFFFFFFFFFF0ULL).speed_percent == 90);
    CHECK(again.song_practice().at(0xFFFFFFFFFFFFFFF0ULL).transpose_semitones == -2);
    CHECK(again.song_practice().at(0x2).speed_percent == kMaxSpeedPercent);
}

TEST_CASE("history: the hand split of a song is saved with its practice, Auto by default") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPractice p;
        p.split_point = 67;
        store.set_song_practice(0x51, p);
        SongPractice plain;
        plain.speed_percent = 70;
        store.set_song_practice(0x52, plain);
        SongPractice wild;
        wild.split_point = 900; // forced into range before it is stored
        store.set_song_practice(0x53, wild);
        REQUIRE(store.flush());
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice WHERE split_point = 67") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice WHERE split_point = 0") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM song_practice WHERE split_point = 127") == 1);
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.song_practice().at(0x51).split_point == 67);
    CHECK(again.song_practice().at(0x52).split_point == kAutoSplit);
    CHECK(again.song_practice().at(0x53).split_point == kMaxSplitPoint);
}

TEST_CASE("history: a version 10 database gains the hand split and keeps the practice it had") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        SongPractice p;
        p.speed_percent = 65;
        p.transpose_semitones = 2;
        p.split_point = 60;
        p.no_shift_offer = true;
        store.set_song_practice(0x61, p);
        REQUIRE(store.flush());
    }
    {
        // A file as version 10 left it: no split column, the octave-shift offer's kept.
        Raw raw(dir.path / "history.db");
        undo_version_13(raw);
        REQUIRE(raw.db.run_script("ALTER TABLE song_practice DROP COLUMN split_point; "
                                  "PRAGMA user_version = 10") == SQLITE_OK);
    }
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.song_practice().count(0x61) == 1);
        CHECK(store.song_practice().at(0x61).speed_percent == 65);
        CHECK(store.song_practice().at(0x61).transpose_semitones == 2);
        CHECK(store.song_practice().at(0x61).split_point == kAutoSplit);
        CHECK(store.song_practice().at(0x61).no_shift_offer); // the offer's column is kept
        SongPractice p = store.song_practice().at(0x61);
        p.split_point = 72;
        store.set_song_practice(0x61, p);
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    }
    HistoryStore again(dir.path, quiet());
    REQUIRE(open_now(again).empty());
    CHECK(again.song_practice().at(0x61).split_point == 72);
    CHECK(again.song_practice().at(0x61).no_shift_offer);
    CHECK(again.song_practice().at(0x61).speed_percent == 65);
}
