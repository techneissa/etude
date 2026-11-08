// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: pinning and pruning run events, and the upgrade from older versions and an interrupted migration.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <doctest/doctest.h>

#include <cstdlib>
#include <filesystem>
#include <string>

#if !defined(_WIN32)
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>
#endif
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("run events: a pin says whether it took; a pruned run, a run without notes and a run "
          "that is not there did not") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.set_events_kept_per_song(1);
    store.add_run(one_setup(sample_run(1)), "a.mid", sample_events());
    store.add_run(one_setup(sample_run(2)), "a.mid", sample_events()); // run 1's notes are pruned
    store.add_run(one_setup(sample_run(3, 0)), "b.mid", sample_events()); // no content hash: no notes kept
    REQUIRE(store.flush());
    Raw raw(store.path());
    const std::int64_t pruned = run_id_at(raw, 1);
    const std::int64_t live = run_id_at(raw, 2);
    const std::int64_t bare = run_id_at(raw, 3);
    REQUIRE(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE run_id = " + std::to_string(pruned)) ==
            0);

    CHECK_FALSE(pin_took(store, store.set_run_pinned(pruned, true)));
    CHECK_FALSE(pin_took(store, store.set_run_pinned(bare, true)));
    CHECK_FALSE(pin_took(store, store.set_run_pinned(424242, true)));
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE pinned = 1") == 0);

    CHECK(pin_took(store, store.set_run_pinned(live, true)));
    CHECK(raw.num("SELECT pinned FROM run_event_sets WHERE run_id = " + std::to_string(live)) == 1);
    CHECK(pin_took(store, store.set_run_pinned(live, true))); // already pinned: still took
    CHECK(pin_took(store, store.set_run_pinned(live, false)));
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets WHERE pinned = 1") == 0);
}

TEST_CASE("run events: a pin on a store that takes no more work returns no serial") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.close();
    CHECK(store.set_run_pinned(1, true) == 0);
}

TEST_CASE("run events: a prune keeps the pinned runs and the best run of each setup, so the ghost "
          "stays the best one") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.set_events_kept_per_song(1);
    const auto played = [&](int n, int speed, int hit, bool complete = true) {
        RunRecord r = sample_run(n);
        r.speed_percent = speed;
        r.transpose_semitones = 0;
        r.mode = RunMode::Flow;
        r.hands = HandFilter::Both;
        r.all = {100, hit};
        store.add_run(sanitized(r), "a.mid", sample_events(6, complete));
    };
    played(1, 100, 90);         // the best at 100%, the oldest
    played(2, 50, 70);          // the best at 50%
    played(3, 100, 40);         // pinned below
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(pin_took(store, store.set_run_pinned(run_id_at(raw, 3), true)));
    played(4, 100, 100, false); // incomplete: never the best, so it goes in the prune
    played(5, 100, 50);
    played(6, 100, 50);         // the newest
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    std::vector<std::int64_t> kept;
    for (const std::string& id : raw.column("SELECT run_id FROM run_event_sets ORDER BY run_id")) {
        kept.push_back(std::stoll(id));
    }
    const std::vector<std::int64_t> expected{run_id_at(raw, 1), run_id_at(raw, 2),
                                             run_id_at(raw, 3), run_id_at(raw, 6)};
    CHECK(kept == expected);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 6); // the summaries all stay

    GhostSetup want;
    want.speed_percent = 100;
    const auto ghost = answer(store, store.request_ghost(0xabc0, want));
    REQUIRE(ghost.has_value());
    CHECK(ghost->run_id == run_id_at(raw, 1)); // not the newer, weaker run
    want.speed_percent = 50;
    const auto slow = answer(store, store.request_ghost(0xabc0, want));
    REQUIRE(slow.has_value());
    CHECK(slow->run_id == run_id_at(raw, 2));

    // Keeping none keeps none, but for the pinned run.
    store.set_events_kept_per_song(0);
    played(7, 100, 60);
    REQUIRE(store.flush());
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 1);
    CHECK(raw.num("SELECT pinned FROM run_event_sets") == 1);
}

// 6. Upgrades from older shapes, and a migration cut short.

TEST_CASE("history: a version 2, 3 or 4 file, as that build left it, upgrades to the latest and "
          "keeps what it held") {
    for (const int version : {2, 3, 4}) {
        CAPTURE(version);
        TempDir dir;
        build_at_version(dir.path / "history.db", version);
        {
            Raw raw(dir.path / "history.db");
            REQUIRE(raw.db.run_script(
                        "INSERT INTO songs (song_key, content_hash, path, title, first_seen_utc_s, "
                        "last_seen_utc_s) VALUES ('k1', 77, '/a.mid', 'A', 1, 2); "
                        "INSERT INTO song_settings (profile_id, content_hash, title, favourite) "
                        "VALUES (1, 77, 'Mine', 1)") == SQLITE_OK);
            if (version >= 3) {
                REQUIRE(raw.db.run_script(
                            "INSERT INTO song_groups (profile_id, group_id, name) VALUES (1, 5, 'G'); "
                            "INSERT INTO song_group_members (profile_id, group_id, content_hash) "
                            "VALUES (1, 5, 77)") == SQLITE_OK);
            }
            if (version >= 4) {
                REQUIRE(raw.db.run_script(
                            "INSERT INTO song_sources (content_hash, catalogue, licence) "
                            "VALUES (77, 'cat', 'CC0')") == SQLITE_OK);
            }
        }
        {
            HistoryStore store(dir.path, quiet());
            CHECK(open_now(store).empty());
            store.add_run(sample_run(1));
            REQUIRE(store.flush());
            CHECK(store.take_errors().empty());
            REQUIRE(store.song_prefs().count(77) == 1);
            CHECK(store.song_prefs().at(77).title == "Mine");
            CHECK(store.song_prefs().at(77).favourite);
            if (version >= 3) {
                CHECK(store.groups().groups().size() == 1);
            }
            if (version >= 4) {
                CHECK(store.song_origins().size() == 1);
            }
            Raw raw(store.path());
            CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
            CHECK(raw.num("SELECT COUNT(*) FROM songs WHERE song_key = 'k1'") == 1);
            CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
        }
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    }
}

TEST_CASE("history: a version 9 file as the build of that day wrote it opens, with its notes and "
          "practice rows, and goes to the latest") {
    TempDir dir;
    build_at_version(dir.path / "history.db", 9);
    {
        Raw raw(dir.path / "history.db");
        REQUIRE(raw.db.run_script(
                    "INSERT INTO songs (id, song_key, content_hash, path, title, first_seen_utc_s, "
                    "last_seen_utc_s) VALUES (1, 'k', 4660, '/a.mid', 'Etude', 1, 2); "
                    "INSERT INTO runs (id, profile_id, song_id, song_title, song_hash, mode, hands, "
                    "speed_percent, transpose_semitones, start_utc_s, end_utc_s, local_day, "
                    "duration_ms, perfect, good, early, late, missed, played, wrong, unplayable, "
                    "longest_combo, clean_passes, left_graded, left_hit, right_graded, right_hit, "
                    "unknown_graded, unknown_hit, all_graded, all_hit) VALUES (1, 1, 1, 'Etude', "
                    "4660, 1, 2, 80, -2, 1000, 1060, 20000, 60000, 11, 12, 13, 14, 15, 16, 17, 18, "
                    "19, 20, 21, 22, 23, 24, 25, 26, 27, 28); "
                    "INSERT INTO run_measures (run_id, measure, graded, missed, wrong) "
                    "VALUES (1, 3, 8, 2, 1); "
                    "INSERT INTO run_event_sets (run_id, event_count, expected_notes, dropped, "
                    "complete, pinned) VALUES (1, 2, 2, 0, 1, 1); "
                    "INSERT INTO run_events (run_id, seq, kind, song_ms, error_ms, key, velocity, "
                    "note_index, hand, grade) VALUES (1, 0, 0, 500, -9, 60, 80, 0, 2, 1), "
                    "(1, 1, 2, 1000, 0, 62, 0, 1, 1, 4); "
                    "INSERT INTO song_practice (profile_id, content_hash, speed_percent, "
                    "count_in_bars, hands, transpose_semitones, updated_utc_s, loop_start_ms, "
                    "loop_end_ms, loop_enabled, section_id, plan_item, parts_set) "
                    "VALUES (1, 4660, 65, 2, 2, -2, 5, 1000, 4000, 1, 3, 2, 1); "
                    "INSERT INTO song_practice_parts (profile_id, content_hash, part_key) "
                    "VALUES (1, 4660, 513)") == SQLITE_OK);
        CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('song_practice') WHERE name IN "
                      "('no_shift_offer', 'split_point')") == 0);
    }
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    REQUIRE(store.runs().size() == 1);
    const RunRecord& run = store.runs()[0];
    CHECK(run.song_hash == 4660);
    CHECK(run.song_title == "Etude");
    CHECK(run.speed_percent == 80);
    CHECK(run.transpose_semitones == -2);
    CHECK(run.counts.perfect == 11);
    REQUIRE(store.song_practice().count(4660) == 1);
    const SongPractice& practice = store.song_practice().at(4660);
    CHECK(practice.speed_percent == 65);
    CHECK(practice.count_in_bars == 2);
    CHECK(practice.loop_end_s == doctest::Approx(4.0));
    CHECK(practice.loop_enabled);
    CHECK(practice.muted_parts.size() == 1);
    CHECK_FALSE(practice.no_shift_offer);
    CHECK(practice.split_point == 0);
    const auto stored = answer(store, store.request_run_events(1));
    REQUIRE(stored.has_value());
    CHECK(stored->pinned);
    REQUIRE(stored->events.events.size() == 2);
    CHECK(stored->events.events[0].error_ms == -9);
    CHECK(stored->events.events[1].kind == RunEventKind::Missed);
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM run_measures") == 1);
    CHECK(raw.num(kThreeIndexes) == 0);
}

TEST_CASE("history: a migration cut short by a failed statement leaves the file at the old version, "
          "and the next start completes it") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
    }
    const int next = static_cast<int>(kLatestVersion) + 1;
    std::vector<Migration> chain(history_migrations().begin(), history_migrations().end());
    chain.push_back({next, "ALTER TABLE profiles ADD COLUMN step_one INTEGER NOT NULL DEFAULT 0"});
    chain.push_back({next + 1,
                     "ALTER TABLE profiles ADD COLUMN step_two INTEGER NOT NULL DEFAULT 0; "
                     "CREATE TABLE step_two_table (x INTEGER); "
                     "ALTER TABLE no_such_table ADD COLUMN x INTEGER"});
    {
        HistoryOptions options = quiet();
        options.migrations = chain;
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        CHECK_FALSE(open_now(store).empty());
        CHECK(store.state() == HistoryState::Waiting);
    }
    {
        // The first step is whole; the second left nothing: not its column, not its table.
        Raw raw(dir.path / "history.db");
        CHECK(raw.num("PRAGMA user_version") == next);
        CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'step_one'") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'step_two'") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'step_two_table'") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    }
    // The next start, with the step fixed, completes it.
    chain.back().sql = "ALTER TABLE profiles ADD COLUMN step_two INTEGER NOT NULL DEFAULT 0; "
                       "CREATE TABLE step_two_table (x INTEGER)";
    HistoryOptions fixed = quiet();
    fixed.migrations = chain;
    HistoryStore store(dir.path, fixed);
    CHECK(open_now(store).empty());
    REQUIRE(store.runs().size() == 1);
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == next + 1);
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'step_two'") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM step_two_table") == 0);
}

#if !defined(_WIN32)
TEST_CASE("history: a process killed in the middle of a migration leaves the file at the old "
          "version, and the next start completes it") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
    }
    const int next = static_cast<int>(kLatestVersion) + 1;
    const std::vector<Migration> chain = with_next(kVersionNext);
    const std::string file = (dir.path / "history.db").string();
    const pid_t child = fork();
    REQUIRE(child >= 0);
    if (child == 0) {
        // The migration's own statements, in its transaction, and the process dies before COMMIT
        // (no destructors, no close, as a kill or a power cut would).
        sql::Database db;
        if (db.open(file) != SQLITE_OK || db.run_script("BEGIN IMMEDIATE") != SQLITE_OK ||
            db.run_script(chain.back().sql) != SQLITE_OK ||
            db.run_script("PRAGMA user_version = " + std::to_string(next)) != SQLITE_OK) {
            _exit(2);
        }
        _exit(0);
    }
    int status = 0;
    REQUIRE(waitpid(child, &status, 0) == child);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
    {
        Raw raw(dir.path / "history.db");
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'color'") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'badges'") == 0);
    }
    HistoryOptions options = quiet();
    options.migrations = chain;
    HistoryStore store(dir.path, options);
    CHECK(open_now(store).empty());
    REQUIRE(store.runs().size() == 1);
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == next);
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'color'") == 1);
    CHECK(raw.num("SELECT COUNT(*) FROM badges") == 0);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}
#endif

// ---- Size: auto_vacuum and the compaction (#257)
