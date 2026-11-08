// SPDX-License-Identifier: GPL-3.0-or-later
// The practice history after a start that died or ran read-only: every state its files can be left
// in (a hot rollback journal, a -wal with committed or uncommitted frames, a missing -shm), and the
// next normal start always ends with a writable history, or says why it cannot save.
#include "history_store.hpp"
#include "history_store_test_support.hpp"

#include "sqlite_db.hpp"

#include <doctest/doctest.h>

#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

#if !defined(_WIN32)
#include <sys/wait.h>
#include <unistd.h>
#endif

using namespace etude;
using namespace etude::app;
using namespace history_test;
using namespace std::chrono_literals;
namespace fs = std::filesystem;

#if !defined(_WIN32)
namespace {

// What a start that died leaves behind is made by a child that dies the same way: its connection is
// never closed, so SQLite has no chance to roll back, checkpoint or delete anything.
enum class Left {
    HotJournal,       // died inside a transaction in rollback-journal mode (the first start's migration)
    WalCommitted,     // died after a commit, before the checkpoint: the -wal holds the work
    WalUncommitted,   // committed work, then died inside a transaction in WAL mode: frames in the -wal
};

const char* name_of(Left l) {
    return l == Left::HotJournal ? "a hot rollback journal"
           : l == Left::WalCommitted ? "a -wal with committed frames"
                                     : "a -wal with uncommitted frames";
}

// A transaction big enough that SQLite writes its journal (or -wal frames) before the end.
const char* kBigWork =
    "PRAGMA cache_size = 4; CREATE TABLE IF NOT EXISTS big (x BLOB); "
    "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 600) "
    "INSERT INTO big SELECT zeroblob(3000) FROM n";

void die_leaving(const fs::path& db_path, Left left) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        sql::Database db;
        bool ok = db.open(db_path) == SQLITE_OK;
        if (ok && left == Left::HotJournal) {
            std::string mode;
            ok = db.query_text("PRAGMA journal_mode = DELETE", mode) == SQLITE_OK &&
                 db.run_script(std::string("BEGIN; ") + kBigWork) == SQLITE_OK;
        } else if (ok && left == Left::WalCommitted) {
            ok = db.run_script("PRAGMA wal_autocheckpoint = 0; CREATE TABLE marker (x INTEGER); "
                               "INSERT INTO marker VALUES (42)") == SQLITE_OK;
        } else if (ok) {
            ok = db.run_script("PRAGMA wal_autocheckpoint = 0; CREATE TABLE marker (x INTEGER); "
                               "INSERT INTO marker VALUES (42)") == SQLITE_OK &&
                 db.run_script(std::string("BEGIN; ") + kBigWork) == SQLITE_OK;
        }
        ::_exit(ok ? 0 : 3); // no close, no checkpoint
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    REQUIRE(WIFEXITED(status));
    REQUIRE(WEXITSTATUS(status) == 0);
}

// A history with one run in it, closed cleanly (so it is a WAL database with one file).
void make_history(const fs::path& dir) {
    HistoryStore store(dir, quiet());
    REQUIRE(open_now(store).empty());
    store.add_run(sample_run(1));
    REQUIRE(store.flush());
}

// The normal start after: opens at once, nothing said, and a run saved.
void check_writable(const fs::path& dir, std::size_t runs_before) {
    Log log;
    HistoryStore store(dir, log.options());
    CHECK(open_now(store).empty());
    CHECK(store.state() == HistoryState::Ready);
    CHECK(store.runs().size() == runs_before);
    store.add_run(sample_run(100 + static_cast<int>(runs_before)));
    REQUIRE(store.flush(10s));
    CHECK(store.state() == HistoryState::Ready);
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == static_cast<std::int64_t>(runs_before + 1));
    CHECK(raw.text("PRAGMA quick_check(1)") == "ok");
    CHECK(raw.text("PRAGMA journal_mode") == "wal");
    CHECK(log.get().empty()); // nothing logged about a trouble
}

std::string slurp(const fs::path& p) {
    std::ifstream in(p, std::ios::binary);
    return std::string(std::istreambuf_iterator<char>(in), {});
}

} // namespace

TEST_CASE("history after a start that died: the next normal start has a writable history") {
    for (const Left left : {Left::HotJournal, Left::WalCommitted, Left::WalUncommitted}) {
        CAPTURE(name_of(left));
        TempDir dir;
        make_history(dir.path);
        die_leaving(dir.path / "history.db", left);
        if (left == Left::HotJournal) {
            REQUIRE(fs::exists(dir.path / "history.db-journal")); // the state this case is about
        } else {
            REQUIRE(fs::exists(dir.path / "history.db-wal"));
        }
        check_writable(dir.path, 1);
        Raw raw(dir.path / "history.db");
        if (left != Left::HotJournal) {
            CHECK(raw.num("SELECT x FROM marker") == 42); // the committed work was kept
        }
        if (left != Left::WalCommitted) {
            // What was not committed is gone, whichever journal held it.
            CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'big'") == 0);
        }
    }
}

TEST_CASE("history after a start that died: a -wal without its -shm, or empty, is no trouble") {
    for (const bool empty_wal : {false, true}) {
        CAPTURE(empty_wal);
        TempDir dir;
        make_history(dir.path);
        die_leaving(dir.path / "history.db", Left::WalCommitted);
        fs::remove(dir.path / "history.db-shm");
        if (empty_wal) {
            std::ofstream(dir.path / "history.db-wal", std::ios::trunc).close();
        }
        check_writable(dir.path, 1);
    }
}

TEST_CASE("history after a start that died: the first start's own migration cut short") {
    TempDir dir;
    // The file as a first start leaves it when it dies inside the migration: a rollback journal over
    // a file with nothing of ours yet.
    {
        sql::Database db;
        REQUIRE(db.open(dir.path / "history.db") == SQLITE_OK);
        REQUIRE(db.run_script("CREATE TABLE seed (x INTEGER)") == SQLITE_OK);
    }
    die_leaving(dir.path / "history.db", Left::HotJournal);
    REQUIRE(fs::exists(dir.path / "history.db-journal"));
    Log log;
    HistoryStore store(dir.path, log.options());
    CHECK(open_now(store).empty());
    CHECK(store.state() == HistoryState::Ready);
    store.add_run(sample_run(1));
    REQUIRE(store.flush(10s));
    Raw raw(store.path());
    CHECK(raw.num("SELECT COUNT(*) FROM runs") == 1);
    CHECK(log.get().empty());
}

namespace {

// A database of a newer etude (version 99) with data in it, one page of which is damaged so its
// integrity check fails: the kind of file that would be set aside if it were taken for ours.
void make_newer_damaged(const fs::path& db_path) {
    {
        sql::Database db;
        REQUIRE(db.open(db_path) == SQLITE_OK);
        REQUIRE(db.run_script("CREATE TABLE future (x BLOB); "
                              "WITH RECURSIVE n(i) AS (SELECT 1 UNION ALL SELECT i + 1 FROM n WHERE i < 300) "
                              "INSERT INTO future SELECT zeroblob(3000) FROM n; "
                              "PRAGMA user_version = 99") == SQLITE_OK);
    }
    std::fstream f(db_path, std::ios::in | std::ios::out | std::ios::binary);
    f.seekp(4096 * 5);
    const std::string junk(4096, '\xFF');
    f.write(junk.data(), static_cast<std::streamsize>(junk.size()));
}

std::int64_t header_version_of(const fs::path& db_path) {
    const std::string head = slurp(db_path).substr(0, 64);
    return (static_cast<unsigned char>(head[60]) << 24) | (static_cast<unsigned char>(head[61]) << 16) |
           (static_cast<unsigned char>(head[62]) << 8) | static_cast<unsigned char>(head[63]);
}

} // namespace

TEST_CASE("history after a start that died: a newer database with a hot journal is told as newer and never set aside") {
    for (const bool header_says_newer : {true, false}) {
        CAPTURE(header_says_newer);
        TempDir dir;
        make_newer_damaged(dir.path / "history.db");
        // Either the file's own header says 99, or the interrupted write had already put another
        // version there, so only the rollback shows the 99 (the open reads the version again then).
        die_leaving(dir.path / "history.db", Left::HotJournal);
        if (!header_says_newer) {
            // The write that was cut short had got page 1 into the file with another version in it
            // (the journal holds the page as it was).
            std::fstream f(dir.path / "history.db", std::ios::in | std::ios::out | std::ios::binary);
            f.seekp(60);
            f.write("\0\0\0\5", 4);
        }
        REQUIRE(fs::exists(dir.path / "history.db-journal"));
        REQUIRE((header_version_of(dir.path / "history.db") == 99) == header_says_newer);
        const std::string journal_before = slurp(dir.path / "history.db-journal");
        const std::string db_before = slurp(dir.path / "history.db");

        Log log;
        HistoryOptions options = log.options();
        options.retry_interval_ms = 30;
        HistoryStore store(dir.path, options);
        CHECK(open_now(store).find("newer") != std::string::npos);
        CHECK(store.state() == HistoryState::Unavailable); // not "not usable" for ever
        store.add_run(sample_run(1));
        store.flush();
        store.close();

        // Not renamed, not migrated, not set aside; nothing else made beside it.
        CHECK(fs::exists(dir.path / "history.db"));
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged"));
        CHECK_FALSE(fs::exists(dir.path / "history.db.damaged.1"));
        if (header_says_newer) {
            // Not even opened: byte for byte, the journal too.
            CHECK(slurp(dir.path / "history.db") == db_before);
            CHECK(slurp(dir.path / "history.db-journal") == journal_before);
        }
        CHECK(header_version_of(dir.path / "history.db") == 99); // rolled back, if it had to be
        Raw raw(dir.path / "history.db");
        CHECK(raw.num("PRAGMA user_version") == 99);
        CHECK(raw.num("SELECT COUNT(*) FROM sqlite_master WHERE name = 'big'") == 0);
    }
}

namespace {

// What a store makes of a file the program cannot write, as a description of the first thing wrong
// ("" when all is as it should be). `can_fix`: the file can be made writable again here.
std::string unwritable_outcome(const fs::path& dir, bool can_fix) {
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir, options);
    const std::string said = open_now(store);
    if (said.find("can't be saved") == std::string::npos) {
        return "the player's line: " + said;
    }
    if (said.find("not usable") != std::string::npos) {
        return "the line says not usable";
    }
    if (store.state() != HistoryState::Waiting || !store.waiting_unwritable()) {
        return "not waiting as unwritable";
    }
    if (log.get().size() != 1 || log.get()[0].find("not writable") == std::string::npos) {
        return "the log line";
    }
    if (can_fix) {
        fs::permissions(dir / "history.db", fs::perms::owner_read | fs::perms::owner_write,
                        fs::perm_options::replace);
        store.add_run(sample_run(2));
        if (!store.flush(10s) || store.state() != HistoryState::Ready || store.waiting_unwritable()) {
            return "it did not open again once the file could be written";
        }
    }
    return "";
}

// The history of `dir`, as a file that holds one run in rollback mode (so that opening it needs to
// create a -journal or -wal beside it).
void make_rollback_history(const fs::path& dir) {
    make_history(dir);
    Raw raw(dir / "history.db");
    std::string mode;
    REQUIRE(raw.db.query_text("PRAGMA journal_mode = DELETE", mode) == SQLITE_OK);
}

} // namespace

TEST_CASE("history: a file the program may not write is told as that, once, with the reason") {
    TempDir dir;
    make_rollback_history(dir.path);
    if (::geteuid() != 0) {
        fs::permissions(dir.path / "history.db", fs::perms::owner_read, fs::perm_options::replace);
        CHECK(unwritable_outcome(dir.path, true).empty());
        return;
    }
    // Root writes through any permission, so the program runs as another user, in a child: the file
    // is root's and read-only to it, and so is the folder.
    const pid_t pid = ::fork();
    if (pid == 0) {
        if (::setgid(65534) != 0 || ::setuid(65534) != 0) {
            ::_exit(77); // no such user here
        }
        const std::string wrong = unwritable_outcome(dir.path, false);
        if (!wrong.empty()) {
            std::fprintf(stderr, "unwritable: %s\n", wrong.c_str());
        }
        ::_exit(wrong.empty() ? 0 : 1);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    REQUIRE(WIFEXITED(status));
    if (WEXITSTATUS(status) == 77) {
        MESSAGE("skipped: no unprivileged user to run as");
        return;
    }
    CHECK(WEXITSTATUS(status) == 0);
}

TEST_CASE("history: a file that cannot be opened for another reason is not called a matter of permissions") {
    TempDir dir;
    make_history(dir.path);
    // A folder where the -wal file must be made: the open fails with "cannot open", though the file
    // and its folder are writable (the same code as a full disk gives).
    fs::create_directory(dir.path / "history.db-wal");
    Log log;
    HistoryOptions options = log.options();
    options.retry_interval_ms = 30;
    HistoryStore store(dir.path, options);
    const std::string said = open_now(store);
    CHECK(said.find("not usable") != std::string::npos);
    CHECK(said.find("can't be saved") == std::string::npos);
    CHECK(store.state() == HistoryState::Waiting);
    CHECK_FALSE(store.waiting_unwritable());
    REQUIRE(log.get().size() == 1);
    CHECK(log.get()[0].find("not writable") == std::string::npos);
    fs::remove(dir.path / "history.db-wal");
    store.add_run(sample_run(2));
    CHECK(store.flush(10s)); // and it opens again once the way is clear
    CHECK(store.state() == HistoryState::Ready);
}
#endif
