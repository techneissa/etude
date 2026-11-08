// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: migrations in order, a newer file left alone, corruption during a write, and a failing migration step.
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

#include <chrono>
#include <filesystem>
#include <string>
#include <thread>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

TEST_CASE("history: a schema version bump migrates the existing database and keeps its runs") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
        store.add_run(sample_run(2));
    }
    const std::vector<Migration> v2 = with_next(kVersionNext);
    Log log;
    HistoryOptions options = log.options();
    options.migrations = v2;
    {
        HistoryStore store(dir.path, options);
        CHECK(open_now(store).empty());
        REQUIRE(store.runs().size() == 2);
        CHECK(store.runs()[0] == sample_run(1));
        store.add_run(sample_run(3));
        store.flush();
        Raw raw(store.path());
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion) + 1);
        CHECK(raw.num("SELECT color FROM profiles WHERE id = 1") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM badges") == 0);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
    }
    CHECK(log.get().empty());
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    // A start that already has version 2 changes nothing.
    HistoryStore again(dir.path, options);
    CHECK(open_now(again).empty());
    CHECK(again.runs().size() == 3);
}

TEST_CASE("history: a fresh database goes through every migration in order") {
    TempDir dir;
    const std::vector<Migration> v2 = with_next(kVersionNext);
    HistoryOptions options = quiet();
    options.migrations = v2;
    HistoryStore store(dir.path, options);
    REQUIRE(open_now(store).empty());
    Raw raw(store.path());
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion) + 1);
    CHECK(raw.num("SELECT COUNT(*) FROM badges") == 0);
}

TEST_CASE("history: a database from a newer etude is left alone and not used") {
    TempDir dir;
    const std::vector<Migration> v2 = with_next(kVersionNext);
    {
        HistoryOptions options = quiet();
        options.migrations = v2;
        HistoryStore store(dir.path, options);
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
    }
    const std::string before = read_all(dir.path / "history.db");
    Log log;
    HistoryStore old_build(dir.path, log.options()); // knows up to this build's version only
    const std::string message = open_now(old_build);
    CHECK(message.find("newer") != std::string::npos);
    CHECK(old_build.state() == HistoryState::Unavailable);
    CHECK(old_build.runs().empty());
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("newer") != std::string::npos);
    old_build.add_run(sample_run(2)); // kept for this session, not written
    old_build.flush();
    CHECK(old_build.runs().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(read_all(dir.path / "history.db") == before);
    Raw raw(dir.path / "history.db");
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion) + 1);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

TEST_CASE("history: wait_until_settled returns only once the state is published") {
    // The wait ends on the state change itself: whenever it returns true the store is out of
    // Loading and its loaded runs (here none: the newer database is not read) can be taken. Many
    // short stores, so a thread caught between "settled" and "state published" shows.
    TempDir dir;
    {
        sql::Database newer;
        REQUIRE(newer.open(dir.path / "history.db") == SQLITE_OK);
        REQUIRE(newer.run_script("CREATE TABLE future (x INTEGER); PRAGMA user_version = 99") ==
                SQLITE_OK);
    }
    int still_loading = 0;
    for (int i = 0; i < 400; ++i) {
        HistoryStore store(dir.path, quiet());
        store.open();
        REQUIRE(store.wait_until_settled(10s));
        if (store.state() == HistoryState::Loading) {
            ++still_loading;
        }
    }
    CHECK(still_loading == 0);
}

TEST_CASE("history: a newer database in rollback mode comes out byte for byte as it went in") {
    TempDir dir;
    {
        // A newer build's file that was never put in WAL mode: setting WAL would rewrite its
        // header.
        sql::Database newer;
        REQUIRE(newer.open(dir.path / "history.db") == SQLITE_OK);
        REQUIRE(newer.run_script("CREATE TABLE future (x INTEGER); INSERT INTO future VALUES (7); "
                                 "PRAGMA user_version = 99") == SQLITE_OK);
    }
    const std::string before = read_all(dir.path / "history.db");
    REQUIRE(before.size() > 100);
    CHECK(static_cast<unsigned char>(before[18]) == 1); // file format: rollback journal
    {
        Log log;
        HistoryStore store(dir.path, log.options());
        CHECK(open_now(store).find("newer") != std::string::npos);
        CHECK(store.state() == HistoryState::Unavailable);
        store.add_run(sample_run(1));
        store.flush();
        CHECK(log.get().size() == 1);
    }
    CHECK(read_all(dir.path / "history.db") == before);
    CHECK_FALSE(fs::exists(dir.path / "history.db-wal"));
    CHECK_FALSE(fs::exists(dir.path / "history.db-shm"));
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}

TEST_CASE("history: a database that goes corrupt during a write is set aside and the runs go to a "
          "new one") {
    TempDir dir;
    Log log;
    HistoryStore store(dir.path, log.options());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush());
    {
        // Break the schema under the open store: its next statement reads it and finds it corrupt.
        Raw raw(store.path());
        const std::int64_t cookie = raw.num("PRAGMA schema_version");
        REQUIRE(raw.db.run_script("PRAGMA writable_schema = ON") == SQLITE_OK);
        REQUIRE(raw.db.run_script("UPDATE sqlite_master SET rootpage = 99999 "
                                  "WHERE name = 'run_measures'") == SQLITE_OK);
        REQUIRE(raw.db.run_script("PRAGMA writable_schema = OFF") == SQLITE_OK);
        REQUIRE(raw.db.run_script("PRAGMA schema_version = " + std::to_string(cookie + 5)) ==
                SQLITE_OK);
    }
    store.add_run(sample_run(2));
    store.add_run(sample_run(3));
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    CHECK(store.take_errors().empty()); // nothing was dropped
    CHECK(store.stats().failures == 0);
    CHECK(fs::exists(dir.path / "history.db.damaged"));
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 1);
    CHECK(lines[0].find("damaged") != std::string::npos);
    const HistoryPoll polled = store.poll();
    CHECK(polled.message.find("damaged") != std::string::npos);
    {
        Raw raw(store.path());
        CHECK(raw.num("SELECT COUNT(*) FROM runs") ==
              2); // the held run and the one queued behind it
    }
    // Later runs go on being written.
    store.add_run(sample_run(4));
    REQUIRE(store.flush());
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
}

TEST_CASE("history: closing during the retry wait makes one last attempt and saves what it can") {
    TempDir dir;
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("CREATE TABLE theirs (x INTEGER)") == SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);
    Log log;
    HistoryOptions options = log.options();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 60'000; // asleep for a minute
    const auto started = std::chrono::steady_clock::now();
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        CHECK(store.state() == HistoryState::Waiting);
        store.add_run(sample_run(1));
        REQUIRE(other.run_script("ROLLBACK") == SQLITE_OK); // free now, but the thread sleeps
    } // closing wakes it for one more attempt
    CHECK(std::chrono::steady_clock::now() - started < 10s);
    CHECK(log.get().size() == 1); // "busy"; nothing was lost, so no "not saved" line
    Raw raw(dir.path / "history.db");
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

TEST_CASE("history: when moving a damaged database aside fails, nothing is deleted") {
    TempDir dir;
    const std::string junk = "junk that is not a database, but long enough to be read as one.....";
    write_all(dir.path / "history.db", junk);
    // The name the WAL file would get is taken by a folder that cannot be replaced.
    fs::create_directories(dir.path / "history.db.damaged-wal" / "keep");
    write_all(dir.path / "history.db-wal", "wal bytes");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        CHECK(store.state() == HistoryState::Waiting);
        store.add_run(sample_run(1));
        CHECK(store.runs().size() == 1);
        CHECK_FALSE(store.flush(200ms));
    }
    CHECK(read_all(dir.path / "history.db") == junk); // moved back, not lost
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(fs::exists(dir.path / "history.db.damaged-wal" / "keep"));
    // SQLite itself may tidy a WAL file it cannot use; ours never deletes one.
    if (fs::exists(dir.path / "history.db-wal")) {
        CHECK(read_all(dir.path / "history.db-wal") == "wal bytes");
    }
    const std::vector<std::string> lines = log.get();
    REQUIRE_FALSE(lines.empty());
    CHECK(lines[0].find("could not be moved aside") != std::string::npos);
}

TEST_CASE("history: with every .damaged name taken nothing is overwritten or deleted") {
    TempDir dir;
    const std::string junk = "junk that is not a database, but long enough to be read as one.....";
    write_all(dir.path / "history.db", junk);
    for (int i = 0; i < 8; ++i) {
        write_all(dir.path /
                      (i == 0 ? "history.db.damaged" : "history.db.damaged." + std::to_string(i)),
                  "old " + std::to_string(i));
    }
    HistoryOptions options = quiet();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    open_now(store);
    CHECK(store.state() == HistoryState::Waiting);
    CHECK(read_all(dir.path / "history.db") == junk);
    CHECK(read_all(dir.path / "history.db.damaged") == "old 0");
    CHECK(read_all(dir.path / "history.db.damaged.7") == "old 7");
}

TEST_CASE("history: a migration step that fails leaves the data at the version before it") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
    }
    const std::vector<Migration> broken =
        with_next("ALTER TABLE profiles ADD COLUMN ok INTEGER; "
                  "ALTER TABLE no_such_table ADD COLUMN x INTEGER");
    Log log;
    HistoryOptions options = log.options();
    options.migrations = broken;
    options.retry_interval_ms = 30;
    {
        HistoryStore store(dir.path, options);
        const std::string message = open_now(store);
        CHECK(message.find("not usable for now") != std::string::npos);
        CHECK(message.find("saved when it opens") != std::string::npos); // true: it is retried
        CHECK(store.state() == HistoryState::Waiting); // held in memory, retried
        store.add_run(sample_run(2));
        CHECK_FALSE(store.flush(300ms)); // several retries, still waiting
        CHECK(log.get().size() == 1);    // and told once
    }
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged")); // a bug in a step is not damage
    Raw raw(dir.path / "history.db");
    // Rolled back whole: not even the first ALTER stuck.
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.num("SELECT COUNT(*) FROM pragma_table_info('profiles') WHERE name = 'ok'") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

// ---- Timing

// ETUDE_HISTORY_BENCH=1 prints how long a run takes to save; the 2 ms rule is checked then (a
// loaded CI machine or a sanitizer build would make it flaky as an ordinary test).
