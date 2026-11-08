// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: a restored dump, a foreign or lookalike file, schema fingerprints, and a WAL database left byte for byte.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "progress_fixtures.hpp"
#include "runs_file.hpp"
#include "sqlite_db.hpp"
#include "etude/content_hash.hpp"
#include "etude/progress.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <cstring>
#include <doctest/doctest.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <csignal>
#include <filesystem>
#include <string>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

// 1. A file with the tables and no version.

TEST_CASE("history: a database restored from a dump (tables, user_version 0) is the version its "
          "tables are, and keeps its runs and notes") {
    TempDir dir;
    const fs::path source = dir.path / "source.db";
    {
        HistoryStore store(dir.path / "src", quiet());
        fs::create_directories(dir.path / "src");
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1), "a.mid", sample_events());
        store.add_run(sample_run(2), "a.mid", sample_events());
        SongPractice p;
        p.speed_percent = 70;
        store.set_song_practice(0xA1, p);
        REQUIRE(store.flush());
        store.close();
        fs::copy_file(store.path(), source);
    }
    dump_restore(source, dir.path / "history.db");
    REQUIRE_FALSE(fs::exists(dir.path / "history.db-wal"));
    Log log;
    HistoryStore store(dir.path, log.options());
    CHECK(open_now(store).empty());
    CHECK(store.state() == HistoryState::Ready);
    REQUIRE(store.runs().size() == 2);
    CHECK(store.runs()[1] == sample_run(2));
    REQUIRE(store.song_practice().count(0xA1) == 1);
    CHECK(store.song_practice().at(0xA1).speed_percent == 70);
    store.add_run(sample_run(3));
    REQUIRE(store.flush());
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1); // told once, then it is a database like any other
    CHECK(lines[0].find("version " + std::to_string(kLatestVersion)) != std::string::npos);
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 2);
}

TEST_CASE("history: tables with user_version 0 are adopted at whichever version their schema is, "
          "and migrated from there") {
    for (int version = 1; version <= static_cast<int>(kLatestVersion); ++version) {
        CAPTURE(version);
        TempDir dir;
        {
            TempDir built;
            build_at_version(built.path / "v.db", version);
            dump_restore(built.path / "v.db", dir.path / "history.db");
        }
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).empty());
        CHECK(store.state() == HistoryState::Ready);
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
        CHECK(raw.num("SELECT COUNT(*) FROM profiles") == 1);
    }
}

TEST_CASE("history: tables with user_version 0 that are not our tables are left as they are, said "
          "once, and not retried") {
    TempDir dir;
    {
        TempDir built;
        build_at_version(built.path / "v.db", static_cast<int>(kLatestVersion));
        dump_restore(built.path / "v.db", dir.path / "history.db");
        Raw raw(dir.path / "history.db");
        // A core table of ours that is someone else's: it lacks a column we write.
        REQUIRE(raw.db.run_script("ALTER TABLE profiles RENAME COLUMN name TO theirs") == SQLITE_OK);
    }
    const std::string before = read_all(dir.path / "history.db");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    const std::string message = open_now(store);
    CHECK(message.find("not one this version can use") != std::string::npos);
    CHECK(message.find("not saved") != std::string::npos);
    CHECK(message.find("saved when it opens") == std::string::npos); // it never will be
    CHECK(store.state() == HistoryState::Unavailable);
    store.add_run(sample_run(1));
    CHECK(store.flush(300ms)); // nothing to wait for: no retries are pending
    CHECK(store.runs().size() == 1); // this session still shows it
    store.close();
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(read_all(dir.path / "history.db") == before);
    CHECK_FALSE(fs::exists(dir.path / "history.db-wal"));
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1); // one line, however long the session
    CHECK(lines[0].find("user_version 0") != std::string::npos);
}

TEST_CASE("history: a foreign file that carries our latest version number is not switched to WAL "
          "or written") {
    TempDir dir;
    {
        sql::Database foreign;
        REQUIRE(foreign.open(dir.path / "history.db") == SQLITE_OK);
        REQUIRE(foreign.run_script("CREATE TABLE theirs (x INTEGER); INSERT INTO theirs VALUES (1); "
                                   "PRAGMA user_version = " +
                                   std::to_string(kLatestVersion)) == SQLITE_OK);
    }
    const std::string before = read_all(dir.path / "history.db");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    CHECK(open_now(store).find("not one this version can use") != std::string::npos);
    CHECK(store.state() == HistoryState::Unavailable);
    store.add_run(sample_run(1));
    store.flush();
    store.close();
    CHECK(read_all(dir.path / "history.db") == before); // the header still says rollback journal
    CHECK_FALSE(fs::exists(dir.path / "history.db-wal"));
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(log.get().size() == 1);
}

TEST_CASE("history: a database of ours that differs a little (another spelling of a statement, an "
          "extra column or index, a missing index) opens and keeps its runs, at version 0 or at "
          "the latest") {
    for (const bool unversioned : {false, true}) {
        CAPTURE(unversioned);
        TempDir dir;
        const fs::path file = dir.path / "history.db";
        {
            HistoryStore store(dir.path / "src", quiet());
            fs::create_directories(dir.path / "src");
            REQUIRE(open_now(store).empty());
            store.add_run(sample_run(1), "a.mid", sample_events());
            store.add_run(sample_run(2));
            REQUIRE(store.flush());
            store.close();
            fs::copy_file(store.path(), file);
        }
        {
            // The statements as another writer or another SQLite would have spelled them, an
            // extra column, an extra index, and an index of ours gone.
            Raw raw(file);
            REQUIRE(raw.db.run_script(
                        "PRAGMA writable_schema = ON; "
                        "UPDATE sqlite_master SET sql = replace(sql, 'INTEGER NOT NULL', "
                        "'integer   not null') WHERE name IN ('runs', 'songs'); "
                        "PRAGMA writable_schema = OFF") == SQLITE_OK);
        }
        {
            Raw raw(file);
            REQUIRE(raw.db.run_script("PRAGMA journal_mode = DELETE; "
                                      "ALTER TABLE profiles ADD COLUMN theirs TEXT; "
                                      "CREATE INDEX theirs_idx ON runs (mode); "
                                      "DROP INDEX profiles_one_default") == SQLITE_OK);
            if (unversioned) {
                REQUIRE(raw.db.run_script("PRAGMA user_version = 0") == SQLITE_OK);
            }
        }
        Log log;
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).empty());
        CHECK(store.state() == HistoryState::Ready);
        CHECK(store.runs().size() == 2);
        store.add_run(sample_run(3));
        REQUIRE(store.flush());
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
        Raw raw(file);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    }
}

TEST_CASE("history: a file at the latest version whose core table lacks our columns is not ours") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
    }
    {
        Raw raw(dir.path / "history.db");
        REQUIRE(raw.db.run_script("PRAGMA journal_mode = DELETE; "
                                  "ALTER TABLE runs DROP COLUMN all_hit") == SQLITE_OK);
    }
    const std::string before = read_all(dir.path / "history.db");
    HistoryStore store(dir.path, quiet());
    CHECK(open_now(store).find("not one this version can use") != std::string::npos);
    CHECK(store.state() == HistoryState::Unavailable);
    store.close();
    CHECK(read_all(dir.path / "history.db") == before);
}

TEST_CASE("history: a file at the latest version with other tables beside ours is used") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
    }
    {
        Raw raw(dir.path / "history.db");
        REQUIRE(raw.db.run_script("CREATE TABLE theirs (x INTEGER); "
                                  "CREATE INDEX theirs_x ON theirs (x)") == SQLITE_OK);
    }
    HistoryStore store(dir.path, quiet());
    CHECK(open_now(store).empty());
    CHECK(store.runs().size() == 1);
}

TEST_CASE("history: no two schema versions have the same tables, columns and indexes, so a version "
          "0 file is never ambiguous") {
    // A migration that changes only data would leave the schema as it was, and the version a
    // dump restore is given (the last match) could be the wrong one. Every migration must change
    // the shape, or this fails and the adoption needs another signal.
    sql::Database db;
    REQUIRE(db.open(":memory:") == SQLITE_OK);
    std::vector<std::string> seen;
    for (const Migration& m : history_migrations()) {
        REQUIRE(db.run_script(m.sql) == SQLITE_OK);
        std::string print;
        sql::Statement tables;
        REQUIRE(tables.prepare(db.get(), "SELECT m.type, m.name, COALESCE(i.name, ''), "
                                         "COALESCE(i.type, ''), COALESCE(i.\"notnull\", ''), "
                                         "COALESCE(i.pk, '') FROM sqlite_master m "
                                         "LEFT JOIN pragma_table_info(m.name) i ON m.type = 'table' "
                                         "WHERE substr(m.name, 1, 7) <> 'sqlite_' "
                                         "ORDER BY m.type, m.name, i.name") == SQLITE_OK);
        while (tables.step() == SQLITE_ROW) {
            for (int i = 0; i < 6; ++i) {
                print += tables.column_text(i) + '\x1f';
            }
        }
        CAPTURE(m.version);
        CHECK(std::find(seen.begin(), seen.end(), print) == seen.end());
        seen.push_back(print);
    }
}

TEST_CASE("history: a file missing any of the three indexes migration 12 drops still migrates") {
    for (const char* missing :
         {"songs_by_content_hash", "runs_by_song", "song_group_members_by_song"}) {
        CAPTURE(missing);
        TempDir dir;
        build_at_version(dir.path / "history.db", 11);
        {
            Raw raw(dir.path / "history.db");
            REQUIRE(raw.db.run_script(std::string("DROP INDEX ") + missing) == SQLITE_OK);
        }
        HistoryStore store(dir.path, quiet());
        CHECK(open_now(store).empty());
        store.add_run(sample_run(1));
        REQUIRE(store.flush());
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        CHECK(raw.num(kThreeIndexes) == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    }
}

// 2. A newer file is never written, its WAL included.

#if !defined(_WIN32)
TEST_CASE("history: a newer database in WAL mode with its -wal beside it comes out byte for byte") {
    TempDir dir;
    TempDir other;
    {
        // The newer build's connection stays open so its -wal still holds the work (version 99
        // and the rows live there, not in the main file); the files are copied out from under it.
        sql::Database newer;
        REQUIRE(newer.open(other.path / "history.db") == SQLITE_OK);
        std::string mode;
        REQUIRE(newer.query_text("PRAGMA journal_mode = WAL", mode) == SQLITE_OK);
        newer.set_checkpoint_on_close(false);
        REQUIRE(newer.run_script("PRAGMA wal_autocheckpoint = 0; CREATE TABLE future (x INTEGER); "
                                 "INSERT INTO future VALUES (7); PRAGMA user_version = 99") ==
                SQLITE_OK);
        REQUIRE(fs::file_size(other.path / "history.db-wal") > 0);
        fs::copy_file(other.path / "history.db", dir.path / "history.db");
        fs::copy_file(other.path / "history.db-wal", dir.path / "history.db-wal");
    }
    const std::string db_before = read_all(dir.path / "history.db");
    const std::string wal_before = read_all(dir.path / "history.db-wal");
    REQUIRE(wal_before.size() > 100);
    {
        Log log;
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).find("newer") != std::string::npos);
        CHECK(store.state() == HistoryState::Unavailable);
        store.add_run(sample_run(1));
        store.flush();
        store.close();
        CHECK(log.get().size() == 1);
    }
    CHECK(read_all(dir.path / "history.db") == db_before);
    CHECK(read_all(dir.path / "history.db-wal") == wal_before);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}

TEST_CASE("history: a connection told not to checkpoint on close leaves a WAL database as it was") {
    // The hazard the read-write open guards against until it knows the file is ours: closing the
    // last connection to a WAL database writes the -wal into the main file and removes it.
    for (const bool guarded : {false, true}) {
        CAPTURE(guarded);
        TempDir dir;
        TempDir held;
        const fs::path file = dir.path / "history.db";
        {
            sql::Database writer;
            REQUIRE(writer.open(held.path / "history.db") == SQLITE_OK);
            std::string mode;
            REQUIRE(writer.query_text("PRAGMA journal_mode = WAL", mode) == SQLITE_OK);
            writer.set_checkpoint_on_close(false);
            REQUIRE(writer.run_script("PRAGMA wal_autocheckpoint = 0; CREATE TABLE t (x INTEGER); "
                                      "INSERT INTO t VALUES (1); PRAGMA user_version = 99") ==
                    SQLITE_OK);
            fs::copy_file(held.path / "history.db", file);
            fs::copy_file(held.path / "history.db-wal", dir.path / "history.db-wal");
        }
        const std::string db_before = read_all(file);
        const std::string wal_before = read_all(dir.path / "history.db-wal");
        {
            sql::Database db;
            REQUIRE(db.open(file) == SQLITE_OK);
            if (guarded) {
                db.set_checkpoint_on_close(false);
            }
            std::int64_t version = 0;
            REQUIRE(db.query_int("PRAGMA user_version", version) == SQLITE_OK);
            CHECK(version == 99);
        }
        if (guarded) {
            CHECK(read_all(file) == db_before);
            CHECK(read_all(dir.path / "history.db-wal") == wal_before);
        } else {
            CHECK(read_all(file) != db_before); // the checkpoint wrote the newer file
            CHECK_FALSE(fs::exists(dir.path / "history.db-wal"));
        }
    }
}
#endif

// 3. Indexes nothing reads.

TEST_CASE("history: migration 12 drops the three indexes no query uses, and a new file never has "
          "them") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        Raw raw(store.path());
        CHECK(raw.num(kThreeIndexes) == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'runs_same_run'") == 1);
    }
    TempDir old_dir;
    build_at_version(old_dir.path / "history.db", 11);
    {
        Raw raw(old_dir.path / "history.db");
        CHECK(raw.num(kThreeIndexes) == 3);
    }
    HistoryStore store(old_dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num(kThreeIndexes) == 0);
    // The queries the store makes still use an index: the same-run index serves every runs lookup.
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

// 5. A pin says whether it took.
