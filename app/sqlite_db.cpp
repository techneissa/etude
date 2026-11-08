// SPDX-License-Identifier: GPL-3.0-or-later
#include "sqlite_db.hpp"

#include <utility>

namespace etude::app::sql {

Failure classify(int rc) {
    switch (rc & 0xFF) { // the primary code, whatever the extended code says
    case SQLITE_OK:
    case SQLITE_ROW:
    case SQLITE_DONE:
        return Failure::None;
    case SQLITE_BUSY:
    case SQLITE_LOCKED:
        return Failure::Locked;
    case SQLITE_CORRUPT:
    case SQLITE_NOTADB:
        return Failure::Damaged;
    case SQLITE_FULL:
    case SQLITE_IOERR:
    case SQLITE_READONLY:
    case SQLITE_CANTOPEN:
    case SQLITE_PROTOCOL:
    case SQLITE_NOMEM:
        return Failure::Environment;
    default:
        return Failure::Other;
    }
}

// ---- Statement

Statement::~Statement() { sqlite3_finalize(stmt_); }

Statement::Statement(Statement&& other) noexcept : stmt_(std::exchange(other.stmt_, nullptr)) {}

Statement& Statement::operator=(Statement&& other) noexcept {
    if (this != &other) {
        sqlite3_finalize(stmt_);
        stmt_ = std::exchange(other.stmt_, nullptr);
    }
    return *this;
}

int Statement::prepare(sqlite3* db, std::string_view sql) {
    sqlite3_finalize(stmt_);
    stmt_ = nullptr;
    return sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
}

void Statement::bind(int index, std::int64_t value) { sqlite3_bind_int64(stmt_, index, value); }

void Statement::bind(int index, std::string_view text) {
    // An empty view may carry a null pointer, which SQLite would bind as NULL.
    sqlite3_bind_text(stmt_, index, text.empty() ? "" : text.data(), static_cast<int>(text.size()),
                      SQLITE_TRANSIENT);
}

void Statement::bind_null(int index) { sqlite3_bind_null(stmt_, index); }

int Statement::step() { return sqlite3_step(stmt_); }

int Statement::reset() {
    const int rc = sqlite3_reset(stmt_);
    sqlite3_clear_bindings(stmt_);
    return rc;
}

std::int64_t Statement::column_int(int index) const { return sqlite3_column_int64(stmt_, index); }

std::string Statement::column_text(int index) const {
    const unsigned char* text = sqlite3_column_text(stmt_, index);
    if (text == nullptr) {
        return {};
    }
    return std::string(reinterpret_cast<const char*>(text),
                       static_cast<std::size_t>(sqlite3_column_bytes(stmt_, index)));
}

bool Statement::column_is_null(int index) const {
    return sqlite3_column_type(stmt_, index) == SQLITE_NULL;
}

// ---- Database

int Database::open(const std::filesystem::path& path, Mode mode) {
    close();
    const std::u8string utf8 = path.u8string();
    const std::string name(utf8.begin(), utf8.end());
    const int access =
        mode == Mode::ReadOnly ? SQLITE_OPEN_READONLY : SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE;
    const int rc = sqlite3_open_v2(name.c_str(), &db_, access | SQLITE_OPEN_FULLMUTEX, nullptr);
    if (rc != SQLITE_OK) {
        close();
    }
    return rc;
}

void Database::close() {
    if (db_ != nullptr) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

std::string Database::error_message() const {
    return db_ != nullptr ? sqlite3_errmsg(db_) : "database not open";
}

std::int64_t Database::last_insert_rowid() const { return sqlite3_last_insert_rowid(db_); }

int Database::changes() const { return sqlite3_changes(db_); }

void Database::set_busy_timeout(int ms) { sqlite3_busy_timeout(db_, ms); }

void Database::set_checkpoint_on_close(bool on) {
    sqlite3_db_config(db_, SQLITE_DBCONFIG_NO_CKPT_ON_CLOSE, on ? 0 : 1, nullptr);
}

int Database::run_script(std::string_view sql) {
    // sqlite3_prepare_v2 reports where the statement ended, which Statement::prepare hides, so
    // this loop calls it directly.
    const char* at = sql.data();
    const char* const end = sql.data() + sql.size();
    while (at < end) {
        sqlite3_stmt* stmt = nullptr;
        const char* tail = nullptr;
        int rc = sqlite3_prepare_v2(db_, at, static_cast<int>(end - at), &stmt, &tail);
        if (rc != SQLITE_OK) {
            return rc;
        }
        if (stmt != nullptr) { // null for a comment or blank text between statements
            do {
                rc = sqlite3_step(stmt);
            } while (rc == SQLITE_ROW);
            const int finalized = sqlite3_finalize(stmt);
            if (rc != SQLITE_DONE) {
                return rc;
            }
            if (finalized != SQLITE_OK) {
                return finalized;
            }
        }
        at = tail;
    }
    return SQLITE_OK;
}

int Database::query_int(std::string_view sql, std::int64_t& out) {
    Statement stmt;
    int rc = stmt.prepare(db_, sql);
    if (rc != SQLITE_OK) {
        return rc;
    }
    rc = stmt.step();
    if (rc == SQLITE_ROW) {
        out = stmt.column_int(0);
        return SQLITE_OK;
    }
    return rc == SQLITE_DONE ? SQLITE_ERROR : rc;
}

int Database::query_text(std::string_view sql, std::string& out) {
    Statement stmt;
    int rc = stmt.prepare(db_, sql);
    if (rc != SQLITE_OK) {
        return rc;
    }
    rc = stmt.step();
    if (rc == SQLITE_ROW) {
        out = stmt.column_text(0);
        return SQLITE_OK;
    }
    return rc == SQLITE_DONE ? SQLITE_ERROR : rc;
}

} // namespace etude::app::sql
