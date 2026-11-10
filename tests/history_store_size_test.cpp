// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history database: its size, incremental auto_vacuum, and the one-time VACUUM.
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

#include <atomic>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <thread>

#include <vector>

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

namespace {

// Events of one run: `count` presses in song-time order (about 32 bytes a row once stored).
RunEvents bulk_events(int count) {
    RunEvents e;
    e.expected_notes = count;
    e.complete = true;
    for (int i = 0; i < count; ++i) {
        RunEvent ev;
        ev.kind = RunEventKind::Press;
        ev.note_index = i;
        ev.key = static_cast<std::uint8_t>(40 + i % 48);
        ev.velocity = 80;
        ev.hand = Hand::Right;
        ev.song_ms = i * 100;
        ev.error_ms = i % 7 - 3;
        ev.grade = NoteGrade::Perfect;
        e.events.push_back(ev);
    }
    return e;
}

std::uintmax_t size_of(const fs::path& p) { return fs::file_size(p); }

// Every column of every run row, in id order: what "every run intact" is compared against.
std::vector<std::string> dump_runs(Raw& raw) {
    const auto columns = raw.num("SELECT COUNT(*) FROM pragma_table_info('runs')");
    sql::Statement s;
    REQUIRE(s.prepare(raw.db.get(), "SELECT * FROM runs ORDER BY id") == SQLITE_OK);
    std::vector<std::string> rows;
    while (s.step() == SQLITE_ROW) {
        std::string row;
        for (int i = 0; i < columns; ++i) {
            row += s.column_is_null(i) ? "NULL" : s.column_text(i);
            row += '|';
        }
        rows.push_back(row);
    }
    return rows;
}

// Makes the closed database a file as every build before #257 made it: auto_vacuum NONE.
void make_mode_none(const fs::path& db) {
    Raw raw(db);
    REQUIRE(raw.db.run_script("PRAGMA auto_vacuum = NONE; VACUUM; PRAGMA wal_checkpoint(TRUNCATE)") ==
            SQLITE_OK);
    REQUIRE(raw.num("PRAGMA auto_vacuum") == 0);
}

// Deletes all but the newest `keep` event sets by hand, leaving their pages on the free list.
void free_event_pages(const fs::path& db, int keep) {
    Raw raw(db);
    REQUIRE(raw.db.run_script(
                "DELETE FROM run_event_sets WHERE run_id NOT IN "
                "(SELECT run_id FROM run_event_sets ORDER BY run_id DESC LIMIT " +
                std::to_string(keep) + "); DELETE FROM run_events WHERE run_id NOT IN "
                "(SELECT run_id FROM run_event_sets); PRAGMA wal_checkpoint(TRUNCATE)") == SQLITE_OK);
}

// A database of `runs` runs with `events` events each, closed, in mode NONE, and mostly free pages.
void mostly_free_file(const fs::path& dir, int runs, int events) {
    {
        HistoryStore store(dir, quiet());
        REQUIRE(open_now(store).empty());
        store.set_events_kept_per_song(kMaxRunEventsKept);
        for (int n = 1; n <= runs; ++n) {
            store.add_run(one_setup(sample_run(n)), "a.mid", bulk_events(events));
        }
        REQUIRE(store.flush());
        REQUIRE(store.take_errors().empty());
    }
    make_mode_none(dir / "history.db");
    free_event_pages(dir / "history.db", 2);
}

HistoryOptions small_threshold(Log& log) {
    HistoryOptions o = log.options();
    o.compact_min_free_bytes = 16 * 1024;
    return o;
}


} // namespace

TEST_CASE("history size: a new file is made in incremental auto_vacuum mode") {
    TempDir dir;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        REQUIRE(store.flush());
    }
    Raw raw(dir.path / "history.db");
    CHECK(raw.num("PRAGMA auto_vacuum") == 2);
    CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
    CHECK(raw.text("PRAGMA journal_mode") == "wal");
}

TEST_CASE("history size: a file grown by runs with events shrinks after a prune by a lower keep-N") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    std::uintmax_t grown = 0;
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.set_events_kept_per_song(40);
        for (int n = 1; n <= 40; ++n) {
            store.add_run(one_setup(sample_run(n)), "a.mid", bulk_events(600));
        }
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    grown = size_of(db);
    CHECK(grown > 700 * 1024); // 40 runs of 600 events: about 1 MB
    Raw before(db);
    const auto rows = dump_runs(before);
    CHECK(rows.size() == 40);
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.set_events_kept_per_song(2); // the player lowers the setting
        store.add_run(one_setup(sample_run(41)), "a.mid", bulk_events(600));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    CHECK(size_of(db) < grown / 4);
    Raw raw(db);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 41); // every summary stays
    CHECK(raw.num("PRAGMA freelist_count") == 0);
    CHECK(raw.num("PRAGMA auto_vacuum") == 2);
    CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
    const auto after = dump_runs(raw);
    REQUIRE(after.size() == 41);
    CHECK(std::vector<std::string>(after.begin(), after.begin() + 40) == rows);
}

TEST_CASE("history size: one run hands back only a bounded number of pages; the rest follow") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.set_events_kept_per_song(100);
        store.add_run(one_setup(sample_run(1)), "a.mid", bulk_events(20));
        store.add_run(one_setup(sample_run(2)), "a.mid", bulk_events(20));
        REQUIRE(store.flush());
    }
    {
        // The first run's set made big by hand: about 900,000 rows, 15 MB.
        Raw raw(db);
        REQUIRE(raw.db.run_script(
                    "WITH RECURSIVE c(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 900000) "
                    "INSERT INTO run_events SELECT s.run_id, 100000 + c.i, 0, c.i, 0, 60, 70, NULL, "
                    "NULL, NULL FROM c, (SELECT MIN(run_id) AS run_id FROM run_event_sets) s") ==
                SQLITE_OK);
    }
    const std::uintmax_t grown = size_of(db);
    CHECK(grown > 12u * 1024 * 1024);
    HistoryStore store(dir.path, quiet());
    REQUIRE(open_now(store).empty());
    store.set_events_kept_per_song(1);
    store.add_run(one_setup(sample_run(3)), "a.mid", bulk_events(10));
    REQUIRE(store.flush());
    Raw raw(db);
    // The prune deleted two sets, far more than the 2048 pages one run gives back.
    const std::int64_t left = raw.num("PRAGMA freelist_count");
    CHECK(left > 1000);
    store.add_run(one_setup(sample_run(4)), "a.mid", bulk_events(10));
    REQUIRE(store.flush());
    const std::int64_t later = raw.num("PRAGMA freelist_count");
    CHECK(later < left);
    for (int n = 5; n < 9; ++n) {
        store.add_run(one_setup(sample_run(n)), "a.mid", bulk_events(10));
        REQUIRE(store.flush());
    }
    CHECK(raw.num("PRAGMA freelist_count") == 0);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 8);
}

TEST_CASE("history size: a file in mode NONE below the thresholds is not rewritten") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 6, 400); // about 25% free, but far under 8 MB
    const std::string bytes = read_all(db);
    Log log;
    {
        HistoryStore store(dir.path, log.options());
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 6);
    }
    Raw raw(db);
    CHECK(raw.num("PRAGMA auto_vacuum") == 0);
    CHECK(raw.num("PRAGMA freelist_count") > 0);
    CHECK_FALSE(logged(log, "compact"));
    CHECK(read_all(db) == bytes);
}

TEST_CASE("history size: a file in mode NONE over the thresholds is converted once, every run intact") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 30, 600);
    const std::uintmax_t big = size_of(db);
    std::vector<std::string> rows;
    {
        Raw raw(db);
        rows = dump_runs(raw);
        CHECK(raw.num("PRAGMA auto_vacuum") == 0);
        CHECK(raw.num("PRAGMA freelist_count") * raw.num("PRAGMA page_size") > 16 * 1024);
    }
    REQUIRE(rows.size() == 30);
    Log log;
    {
        HistoryStore store(dir.path, small_threshold(log));
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 30);
        CHECK(logged(log, "compacted"));
        // Still a working database: a new run is saved beside the old ones.
        store.add_run(one_setup(sample_run(31)), "a.mid", bulk_events(50));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    CHECK(size_of(db) < big / 4);
    {
        Raw raw(db);
        CHECK(raw.num("PRAGMA auto_vacuum") == 2);
        CHECK(raw.text("PRAGMA journal_mode") == "wal");
        CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
        CHECK(raw.num("PRAGMA user_version") == static_cast<std::int64_t>(kLatestVersion));
        const auto after = dump_runs(raw);
        REQUIRE(after.size() == 31);
        CHECK(std::vector<std::string>(after.begin(), after.begin() + 30) == rows);
    }
    // Once: the next start finds incremental mode and rewrites nothing.
    log.clear();
    {
        HistoryStore store(dir.path, small_threshold(log));
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 31);
    }
    CHECK_FALSE(logged(log, "compact"));
}

TEST_CASE("history size: with the default thresholds, over 8 MB and a quarter free is converted") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    {
        HistoryStore store(dir.path, quiet());
        REQUIRE(open_now(store).empty());
        store.add_run(sample_run(1), "a.mid", bulk_events(20));
        store.add_run(sample_run(2), "a.mid", bulk_events(20));
        REQUIRE(store.flush());
    }
    make_mode_none(db);
    {
        Raw raw(db);
        REQUIRE(raw.db.run_script(
                    "WITH RECURSIVE c(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM c WHERE i < 480000) "
                    "INSERT INTO run_events SELECT s.run_id, 100000 + c.i, 0, c.i, 0, 60, 70, NULL, "
                    "NULL, NULL FROM c, (SELECT MIN(run_id) AS run_id FROM run_event_sets) s") ==
                SQLITE_OK);
        REQUIRE(raw.db.run_script("PRAGMA wal_checkpoint(TRUNCATE)") == SQLITE_OK);
    }
    CHECK(size_of(db) > 10u * 1024 * 1024);
    {
        // Used pages: the file is full, so nothing is converted.
        Log log;
        HistoryStore store(dir.path, log.options());
        REQUIRE(open_now(store).empty());
        CHECK_FALSE(logged(log, "compact"));
    }
    {
        Raw raw(db);
        REQUIRE(raw.db.run_script("DELETE FROM run_events WHERE seq >= 100000; "
                                  "PRAGMA wal_checkpoint(TRUNCATE)") == SQLITE_OK);
        CHECK(raw.num("PRAGMA freelist_count") * raw.num("PRAGMA page_size") > 8 * 1024 * 1024);
    }
    Log log;
    {
        HistoryStore store(dir.path, log.options());
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 2);
    }
    CHECK(logged(log, "compacted"));
    CHECK(size_of(db) < 1024 * 1024);
    Raw raw(db);
    CHECK(raw.num("PRAGMA auto_vacuum") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 2);
}

TEST_CASE("history size: no room for a copy leaves the file untouched and usable") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    const std::string bytes = read_all(db);
    std::vector<std::string> rows;
    {
        Raw raw(db);
        rows = dump_runs(raw);
    }
    Log log;
    HistoryOptions options = small_threshold(log);
    options.free_space = [](const fs::path&) -> std::optional<std::uintmax_t> { return 1024; };
    {
        HistoryStore store(dir.path, options);
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 20);
        CHECK(logged(log, "no room"));
        CHECK_FALSE(logged(log, "compacted"));
        CHECK(read_all(db) == bytes);
        store.add_run(one_setup(sample_run(21)), "a.mid", bulk_events(10));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    Raw raw(db);
    CHECK(raw.num("PRAGMA auto_vacuum") == 0);
    const auto after = dump_runs(raw);
    REQUIRE(after.size() == 21);
    CHECK(std::vector<std::string>(after.begin(), after.begin() + 20) == rows);
    // Room unknown is the same as no room.
    options.free_space = [](const fs::path&) -> std::optional<std::uintmax_t> { return std::nullopt; };
    {
        HistoryStore store(dir.path, options);
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 21);
    }
    CHECK(raw.num("PRAGMA auto_vacuum") == 0);
}

TEST_CASE("history size: a compaction that fails is logged and skipped, the file stays usable") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    // A second connection holding the write lock keeps VACUUM out (it is not an etude, so
    // history.lock is free, but the file is busy).
    Raw writer(db);
    REQUIRE(writer.db.run_script("BEGIN IMMEDIATE") == SQLITE_OK);
    Log log;
    HistoryOptions options = small_threshold(log);
    options.open_busy_timeout_ms = 50;
    HistoryStore store(dir.path, options);
    open_now(store);
    CHECK(logged(log, "compacting"));
    CHECK(logged(log, "left as it was"));
    CHECK_FALSE(logged(log, "compacted"));
    REQUIRE(writer.db.run_script("COMMIT") == SQLITE_OK);
    REQUIRE(store.wait_until_settled(10s));
    CHECK(store.state() == HistoryState::Ready);
    store.add_run(one_setup(sample_run(21)), "a.mid", bulk_events(10));
    REQUIRE(store.flush());
    CHECK(store.take_errors().empty());
    CHECK(writer.num("PRAGMA auto_vacuum") == 0);
    CHECK(writer.text("PRAGMA quick_check(1)") == "ok");
    CHECK(writer.num("SELECT COUNT(*) FROM runs") == 21);
}

TEST_CASE("history size: a file already in incremental or full mode is left alone") {
    for (const char* mode : {"INCREMENTAL", "FULL"}) {
        CAPTURE(mode);
        TempDir dir;
        const fs::path db = dir.path / "history.db";
        mostly_free_file(dir.path, 20, 600);
        {
            Raw raw(db);
            REQUIRE(raw.db.run_script(std::string("PRAGMA auto_vacuum = ") + mode + "; VACUUM") ==
                    SQLITE_OK);
            // Free pages again, as a mode that does not hand them back (or has not yet) leaves them.
            REQUIRE(raw.db.run_script("PRAGMA wal_checkpoint(TRUNCATE)") == SQLITE_OK);
        }
        const std::int64_t wanted = std::string(mode) == "FULL" ? 1 : 2;
        Log log;
        {
            HistoryStore store(dir.path, small_threshold(log));
            REQUIRE(open_now(store).empty());
            CHECK(store.runs().size() == 20);
        }
        CHECK_FALSE(logged(log, "compact"));
        Raw raw(db);
        CHECK(raw.num("PRAGMA auto_vacuum") == wanted);
        CHECK(raw.num("SELECT COUNT(*) FROM runs") == 20);
    }
}

TEST_CASE("history size: a second etude never compacts the file the first one holds") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    Log first_log;
    HistoryStore first(dir.path, quiet());
    // The first one holds the lock but sees a file under the thresholds (the defaults).
    REQUIRE(open_now(first).empty());
    const std::string bytes = read_all(db);
    Log log;
    {
        HistoryStore second(dir.path, small_threshold(log));
        CHECK(open_now(second).find("another Etude") != std::string::npos);
        CHECK(second.state() == HistoryState::ReadOnly);
        CHECK(second.runs().size() == 20);
    }
    CHECK_FALSE(logged(log, "compact"));
    CHECK(read_all(db) == bytes);
    Raw raw(db);
    CHECK(raw.num("PRAGMA auto_vacuum") == 0);
}

namespace {

// A hook for HistoryOptions::compacting that holds the VACUUM mid-way until the test lets go.
struct VacuumGate {
    std::atomic<bool> entered{false};
    std::atomic<bool> released{false};
    std::function<void()> hook() {
        return [this] {
            entered = true;
            while (!released) {
                std::this_thread::sleep_for(1ms);
            }
        };
    }
    void wait_entered() {
        for (int i = 0; i < 10000 && !entered; ++i) {
            std::this_thread::sleep_for(1ms);
        }
        REQUIRE(entered);
    }
};

// The file is still a usable mode-NONE database with its 20 runs, and a next start compacts it.
void check_untouched_then_compacts(const fs::path& dir, const std::vector<std::string>& rows,
                                   std::size_t runs) {
    {
        Raw raw(dir / "history.db");
        CHECK(raw.num("PRAGMA auto_vacuum") == 0);
        CHECK(raw.num("PRAGMA freelist_count") > 0);
        CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
        const auto now = dump_runs(raw);
        REQUIRE(now.size() == runs);
        CHECK(std::vector<std::string>(now.begin(), now.begin() + 20) == rows);
    }
    Log log;
    {
        HistoryStore store(dir, small_threshold(log));
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == runs);
    }
    CHECK(logged(log, "compacted"));
    Raw raw(dir / "history.db");
    CHECK(raw.num("PRAGMA auto_vacuum") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == static_cast<std::int64_t>(runs));
}

} // namespace

TEST_CASE("history size: quitting during the one-time VACUUM stops it, and the next start compacts") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    std::vector<std::string> rows;
    {
        Raw raw(db);
        rows = dump_runs(raw);
    }
    VacuumGate gate;
    Log log;
    HistoryOptions options = small_threshold(log);
    options.compacting = gate.hook();
    HistoryStore store(dir.path, options);
    store.open(); // the VACUUM is under way: the store is not settled
    gate.wait_entered();
    std::atomic<bool> closing{false};
    std::atomic<bool> closed{false};
    std::thread closer([&] {
        closing = true;
        store.close();
        closed = true;
    });
    while (!closing) {
        std::this_thread::sleep_for(1ms);
    }
    std::this_thread::sleep_for(50ms); // close() has asked it to stop
    gate.released = true;
    closer.join();
    CHECK(closed);
    CHECK(logged(log, "stopped"));
    CHECK_FALSE(logged(log, "compacted"));
    check_untouched_then_compacts(dir.path, rows, 20);
}

TEST_CASE("history size: a run that finishes during the VACUUM stops it and is saved at once") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    std::vector<std::string> rows;
    {
        Raw raw(db);
        rows = dump_runs(raw);
    }
    VacuumGate gate;
    Log log;
    HistoryOptions options = small_threshold(log);
    options.compacting = gate.hook();
    {
        HistoryStore store(dir.path, options);
        store.open();
        gate.wait_entered();
        store.add_run(one_setup(sample_run(21)), "a.mid", bulk_events(10));
        gate.released = true;
        REQUIRE(store.wait_until_settled(10s));
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
        CHECK(store.state() == HistoryState::Ready);
    }
    CHECK(logged(log, "stopped"));
    CHECK_FALSE(logged(log, "compacted"));
    check_untouched_then_compacts(dir.path, rows, 21);
}

TEST_CASE("history size: a run already waiting to be saved puts the VACUUM off to the next start") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    std::vector<std::string> rows;
    {
        Raw raw(db);
        rows = dump_runs(raw);
    }
    Log log;
    {
        HistoryStore store(dir.path, small_threshold(log));
        store.add_run(one_setup(sample_run(21)), "a.mid", bulk_events(10)); // finished before open
        REQUIRE(open_now(store).empty());
        REQUIRE(store.flush());
        CHECK(store.take_errors().empty());
    }
    CHECK(logged(log, "waits for a start"));
    CHECK_FALSE(logged(log, "compacted"));
    check_untouched_then_compacts(dir.path, rows, 21);
}

TEST_CASE("history size: a journal is replayed into the database before the VACUUM, and all of it stays") {
    TempDir dir;
    const fs::path db = dir.path / "history.db";
    mostly_free_file(dir.path, 20, 600);
    std::string journal = encode_journal_header();
    for (int n = 101; n <= 103; ++n) {
        JournalRun run;
        run.record = one_setup(sample_run(n));
        run.song_path = "/songs/a.mid";
        run.events = bulk_events(30);
        journal += encode_journal_entry(run);
    }
    write_all(dir.path / "history.journal", journal);
    // Inside the VACUUM, a second connection reads what the database holds: the journal's runs
    // must be in it already (a VACUUM before the replay would show 20).
    std::atomic<std::int64_t> seen{-1};
    std::atomic<bool> looked{false};
    Log log;
    HistoryOptions options = small_threshold(log);
    options.compacting = [&] {
        if (looked.exchange(true)) {
            return;
        }
        sql::Database reader;
        if (reader.open(db, sql::Database::Mode::ReadOnly) == SQLITE_OK) {
            std::int64_t count = -1;
            if (reader.query_int("SELECT COUNT(*) FROM runs", count) == SQLITE_OK) {
                seen = count;
            }
        }
    };
    {
        HistoryStore store(dir.path, options);
        REQUIRE(open_now(store).empty());
        CHECK(store.runs().size() == 23);
    }
    CHECK(seen == 23);
    CHECK(logged(log, "compacted"));
    CHECK_FALSE(fs::exists(dir.path / "history.journal"));
    Raw raw(db);
    CHECK(raw.num("PRAGMA auto_vacuum") == 2);
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 23);
    CHECK(raw.num("SELECT COUNT(*) FROM runs WHERE start_utc_s >= " +
                  std::to_string(sample_run(101).start_utc_s)) == 3);
    CHECK(raw.num("SELECT COUNT(*) FROM run_event_sets") == 5); // the 2 kept and the 3 replayed
    CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
}
