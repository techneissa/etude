// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database held locked by another etude, a read-only file, and a write that cannot be saved.
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

TEST_CASE("history: a database someone else holds locked is never moved; runs wait in memory and "
          "are saved when it frees") {
    TempDir dir;
    // A database in the default rollback mode with an exclusive lock held, as another program in
    // the middle of writing would have it.
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("CREATE TABLE theirs (x INTEGER); INSERT INTO theirs VALUES (1)") ==
            SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);

    Log log;
    HistoryOptions options = log.options();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    const std::string message = open_now(store);
    CHECK(message.find("busy") != std::string::npos);
    CHECK(store.state() == HistoryState::Waiting);
    CHECK(store.loading()); // nothing has been read yet
    store.add_run(sample_run(1));
    store.add_run(sample_run(2));
    CHECK(store.runs().size() == 2); // this session still has them
    CHECK_FALSE(flush_fails(store, 2)); // they cannot be saved yet, and a retry fails too
    // Several retries have failed; still one line, and the file is where it was.
    CHECK(log.get().size() == 1);
    CHECK(log.get()[0].find("busy") != std::string::npos);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(fs::exists(dir.path / "history.db"));

    REQUIRE(other.run_script("ROLLBACK") == SQLITE_OK); // the lock frees
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    const HistoryPoll polled = store.poll();
    CHECK(polled.loaded);
    CHECK_FALSE(store.loading());
    CHECK(store.runs().size() == 2);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);   // each written once
    CHECK(raw.num("SELECT COUNT(*) FROM theirs") == 1); // their table is untouched
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(store.take_errors().empty());
}

TEST_CASE("history: a write that finds the database locked is held and retried, once, in order") {
    TempDir dir;
    Log log;
    HistoryOptions options = log.options();
    options.write_busy_timeout_ms = 20;
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush(10s));

    Raw raw(store.path());
    REQUIRE(raw.db.run_script("BEGIN IMMEDIATE") == SQLITE_OK); // another connection is writing
    store.add_run(sample_run(2));
    store.add_run(sample_run(3));
    CHECK_FALSE(flush_fails(store, 2)); // the write failed, and so did a retry
    CHECK(store.state() == HistoryState::Waiting);
    CHECK(log.get().size() == 1); // one line however many retries
    CHECK(log.get()[0].find("busy") != std::string::npos);
    CHECK(store.take_errors().empty()); // held back is not a failure

    REQUIRE(raw.db.run_script("COMMIT") == SQLITE_OK);
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    CHECK(log.get().size() == 2);
    CHECK(log.get()[1].find("free again") != std::string::npos);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3);
    CHECK(raw.text("SELECT group_concat(start_utc_s, ',') FROM (SELECT start_utc_s FROM runs ORDER "
                   "BY id)") == std::to_string(sample_run(1).start_utc_s) + "," +
                                    std::to_string(sample_run(2).start_utc_s) + "," +
                                    std::to_string(sample_run(3).start_utc_s));
    CHECK(store.stats().failures == 0);
}

TEST_CASE(
    "history: closing while the database is still busy does not hang, and keeps what was held") {
    TempDir dir;
    sql::Database other;
    REQUIRE(other.open(dir.path / "history.db") == SQLITE_OK);
    REQUIRE(other.run_script("CREATE TABLE theirs (x INTEGER)") == SQLITE_OK);
    REQUIRE(other.run_script("BEGIN EXCLUSIVE") == SQLITE_OK);
    Log log;
    HistoryOptions options = log.options();
    options.open_busy_timeout_ms = 20;
    options.retry_interval_ms = 60'000; // would wait a minute
    {
        HistoryStore store(dir.path, options);
        open_now(store);
        store.add_run(sample_run(1));
    }
    const std::vector<std::string> lines = log.get();
    REQUIRE(lines.size() == 2);
    CHECK(lines[1].find("1 runs, 0 practice settings and 0 library changes not written") != std::string::npos);
    CHECK(lines[1].find("history.journal") != std::string::npos);
    CHECK(decode_journal(read_all(dir.path / "history.journal")).entries.size() == 1);
    REQUIRE(other.run_script("ROLLBACK") == SQLITE_OK);
}

TEST_CASE("history: a second etude on the same folder reads the history and saves nothing") {
    TempDir dir;
    Log log;
    HistoryStore first(dir.path, quiet());
    REQUIRE(open_now(first).empty());
    first.add_run(sample_run(1));
    first.add_run(sample_run(2));
    REQUIRE(first.flush());
    CHECK(fs::exists(dir.path / "history.lock"));

    {
        HistoryStore second(dir.path, log.options());
        const std::string message = open_now(second);
        CHECK(message.find("another Etude") != std::string::npos);
        CHECK(second.state() == HistoryState::ReadOnly);
        CHECK(second.runs().size() == 2); // it can read what the first one saved
        CHECK_FALSE(second.loading());
        second.add_run(sample_run(3));
        CHECK(second.runs().size() == 3); // in memory for this session
        CHECK(second.flush());
        CHECK(log.get().size() == 1);
    }
    first.add_run(sample_run(4));
    REQUIRE(first.flush());
    Raw raw(first.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 3); // 1, 2 and 4; never the second one's 3
    CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE start_utc_s = " +
                  std::to_string(sample_run(3).start_utc_s)) == 0);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
}

TEST_CASE("history: a second etude does not create or change a missing database") {
    TempDir dir;
    // A lock file held by a first instance that has not made the database yet.
    HistoryStore first(dir.path, quiet());
    first.open(); // may still be starting; the lock is what matters
    REQUIRE(first.wait_until_settled(10s));
    fs::remove(dir.path / "history.db");
    fs::remove(dir.path / "history.db-wal");
    fs::remove(dir.path / "history.db-shm");
    HistoryStore second(dir.path, quiet());
    open_now(second);
    CHECK(second.state() == HistoryState::ReadOnly);
    CHECK(second.runs().empty());
    CHECK_FALSE(fs::exists(dir.path / "history.db"));
}

TEST_CASE("history: the lock is free again once the first etude is gone") {
    TempDir dir;
    {
        HistoryStore first(dir.path, quiet());
        REQUIRE(open_now(first).empty());
        first.add_run(sample_run(1));
    }
    HistoryStore next(dir.path, quiet());
    CHECK(open_now(next).empty());
    CHECK(next.state() == HistoryState::Ready);
    CHECK(next.runs().size() == 1);
}

TEST_CASE("history: open() reads nothing; the runs arrive with poll(), before this session's own") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        for (int i = 0; i < 50; ++i) {
            store.add_run(sample_run(i));
        }
    }
    HistoryStore store(dir.path, quiet());
    store.open();
    CHECK(store.loading());
    CHECK(store.runs().empty());   // nothing on this thread until poll() says so
    store.add_run(sample_run(99)); // a run that ends while the history is still loading
    REQUIRE(store.wait_until_settled(10s));
    CHECK(store.runs().size() == 1); // still only this session's: the load waits for poll()
    CHECK(store.loading());
    const HistoryPoll polled = store.poll();
    CHECK(polled.loaded);
    CHECK_FALSE(store.loading());
    REQUIRE(store.runs().size() == 51);
    CHECK(store.runs()[0] == sample_run(0));
    CHECK(store.runs()[49] == sample_run(49));
    CHECK(store.runs()[50] == sample_run(99));
    CHECK_FALSE(store.poll().loaded); // once
    REQUIRE(store.flush());
    CHECK(store.stats().loaded_runs == 50);
    CHECK(store.stats().load_ms > 0.0);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 51);
}

TEST_CASE("history: a first migration that fails for any reason but damage never moves the file") {
    TempDir dir;
    {
        // Not our database: a table of its own with a name the first migration also wants. Its
        // tables are of ours by name and match no version, so the file is given up for this
        // session (Foreign), not retried: it will not change by waiting.
        sql::Database foreign;
        REQUIRE(foreign.open(dir.path / "history.db") == SQLITE_OK);
        REQUIRE(foreign.run_script("CREATE TABLE profiles (other_column TEXT)") == SQLITE_OK);
    }
    const std::string before = read_all(dir.path / "history.db");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    const std::string message = open_now(store);
    CHECK_FALSE(message.empty());
    CHECK(store.state() == HistoryState::Unavailable);
    store.add_run(sample_run(1));
    CHECK(store.flush(300ms)); // nothing is retried
    CHECK(log.get().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(read_all(dir.path / "history.db") == before);
    CHECK(store.runs().size() == 1);
}

#if !defined(_WIN32)
TEST_CASE("history: a write to a read-only file keeps the run in memory and retries") {
    if (::geteuid() == 0) {
        MESSAGE("skipped: root writes to read-only files");
        return;
    }
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1));
    }
    const fs::path db = dir.path / "history.db";
    const std::string before = read_all(db);
    fs::permissions(db, fs::perms::owner_read, fs::perm_options::replace);
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    open_now(store);
    CHECK(store.state() == HistoryState::Ready); // reading works
    store.add_run(sample_run(2));
    CHECK_FALSE(flush_fails(store)); // the write finds the file read-only
    CHECK(store.state() == HistoryState::Waiting);
    CHECK(store.take_errors().empty()); // held back, not failed
    CHECK(log.get().size() == 1);
    CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
    CHECK(read_all(db) == before);

    // The disk is writable again. (SQLite gave its -wal and -shm files the database's read-only
    // mode while it was, so they are restored too.)
    for (const char* name : {"history.db", "history.db-wal", "history.db-shm"}) {
        std::error_code ec;
        fs::permissions(dir.path / name, fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace, ec);
    }
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    store.poll();
    CHECK(store.runs().size() == 2); // the one from before and the one held
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
}
#endif

TEST_CASE("history: when no database can be had the runs are kept and saved once one can") {
    TempDir dir;
    fs::create_directory(dir.path / "history.db"); // a folder where the file should be
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    const std::string message = open_now(store);
    CHECK_FALSE(message.empty());
    CHECK(store.state() == HistoryState::Waiting);
    store.add_run(sample_run(1));
    CHECK(store.runs().size() == 1);
    CHECK_FALSE(flush_fails(store));
    CHECK(log.get().size() == 1);
    CHECK(store.take_errors().empty());

    fs::remove(dir.path / "history.db");
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

TEST_CASE("history: a run that cannot be saved is reported and leaves no half-written run") {
    TempDir dir;
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    store.flush();
    {
        Raw raw(store.path());
        REQUIRE(raw.db.run_script("DROP TABLE run_measures") == SQLITE_OK); // the schema broke
    }
    store.add_run(sample_run(2));
    store.flush();
    const std::vector<std::string> errors = store.take_errors();
    REQUIRE(errors.size() == 1);
    CHECK(errors[0].find("Could not save the run") != std::string::npos);
    CHECK(store.take_errors().empty());
    CHECK(store.stats().failures == 1);
    CHECK(store.stats().writes == 1);
    CHECK(store.runs().size() == 2); // this session still has it
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
}

// ---- Schema versions
