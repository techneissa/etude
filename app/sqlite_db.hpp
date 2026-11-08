// SPDX-License-Identifier: GPL-3.0-or-later
// The thinnest wrapper around the SQLite C API that the history store needs: a connection and a
// prepared statement that close themselves. Everything that touches SQLite goes through
// prepared statements (scripts and pragmas included: run_script() prepares each statement in
// turn), and values are always bound, never pasted into the SQL.
#pragma once

#include <sqlite3.h>

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>

namespace etude::app::sql {

// How a result code reads for the store: a locked database, a damaged one (or one that is not a
// SQLite file at all), a trouble of the machine that may pass (full disk, read-only file, I/O
// error, no memory), or any other failure (a statement that does not apply).
enum class Failure { None, Locked, Damaged, Environment, Other };
Failure classify(int rc);

class Statement {
  public:
    Statement() = default;
    ~Statement();
    Statement(const Statement&) = delete;
    Statement& operator=(const Statement&) = delete;
    Statement(Statement&& other) noexcept;
    Statement& operator=(Statement&& other) noexcept;

    // The first statement of `sql` (anything after its ';' is ignored). SQLITE_OK or an error.
    int prepare(sqlite3* db, std::string_view sql);
    bool prepared() const { return stmt_ != nullptr; }

    // 1-based, as in SQLite.
    void bind(int index, std::int64_t value);
    void bind(int index, std::string_view text);
    void bind_null(int index);

    // SQLITE_ROW, SQLITE_DONE or an error.
    int step();
    // Back to the start with the bindings cleared; returns the result of the last step.
    int reset();

    std::int64_t column_int(int index) const;
    std::string column_text(int index) const;
    bool column_is_null(int index) const;

  private:
    sqlite3_stmt* stmt_ = nullptr;
};

class Database {
  public:
    Database() = default;
    ~Database() { close(); }
    Database(const Database&) = delete;
    Database& operator=(const Database&) = delete;

    // Opens (and creates) the file for reading and writing, or only reads an existing one.
    // SQLITE_OK or an error; a failed open leaves the object closed. The connection serialises its
    // own calls.
    enum class Mode { ReadWrite, ReadOnly };
    int open(const std::filesystem::path& path, Mode mode = Mode::ReadWrite);
    void close();
    bool is_open() const { return db_ != nullptr; }
    sqlite3* get() const { return db_; }
    std::string error_message() const;
    std::int64_t last_insert_rowid() const;
    int changes() const;
    void set_busy_timeout(int ms);
    // Whether closing the last connection to a WAL database writes its -wal back into the file
    // (the default). Off, a close leaves both files as they were: for a file that is not ours to
    // write, whatever its journal mode.
    void set_checkpoint_on_close(bool on);

    // Runs every statement of `sql` (separated by ';'), ignoring any rows. The first failure's
    // result code, else SQLITE_OK.
    int run_script(std::string_view sql);
    // The first column of the first row of a single-value query (a pragma). SQLITE_OK with `out`
    // set, or an error.
    int query_int(std::string_view sql, std::int64_t& out);
    int query_text(std::string_view sql, std::string& out);

  private:
    sqlite3* db_ = nullptr;
};

} // namespace etude::app::sql
