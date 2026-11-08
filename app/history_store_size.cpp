// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.hpp"

#include "history_store_internal.hpp"

#include "runs_file.hpp"
#include "etude/run_columns.hpp"
#include "etude/song_key.hpp"

#include <algorithm>
#include <cmath>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <iterator>
#include <cctype>
#include <map>
#include <set>
#include <span>
#include <system_error>
#include <utility>
#include <variant>

#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace etude::app {

namespace fs = std::filesystem;

namespace {

constexpr int kReclaimPages = 256;       // pages handed back per transaction
constexpr int kReclaimChunksPerJob = 8;  // transactions after one run: 2048 pages, 8 MB of 4 KB
std::string megabytes(std::uint64_t bytes) {
    char buf[32];
    std::snprintf(buf, sizeof buf, "%.1f MB", static_cast<double>(bytes) / (1024.0 * 1024.0));
    return buf;
}

} // namespace

// An old database is in auto_vacuum NONE: its file never shrinks, so a heavy prune leaves free
// pages for good, and SQLite changes the mode only through one full VACUUM, which rewrites the
// file, needs about its size free (beside it, and in the temp folder where SQLite builds the copy)
// and holds the exclusive lock throughout. So it is done once, here on the history thread (the
// frame thread never touches the file), after the journal replay, and only when it pays: free
// pages over both thresholds, this process holds history.lock (no other instance has the file
// open), the file is ours and writable, and the disk has room. Anything that goes wrong is logged
// and skipped: a failed VACUUM changes nothing, so the file stays as it was and usable. A file in
// any other mode (INCREMENTAL, or FULL by someone's choice) is left alone.
void HistoryStore::compact_if_worthwhile() {
    incremental_ = false;
    std::int64_t mode = 0;
    if (db_.query_int("PRAGMA auto_vacuum", mode) != SQLITE_OK) {
        return;
    }
    std::int64_t pages = 0;
    std::int64_t page_size = 0;
    std::int64_t free_pages = 0;
    if (db_.query_int("PRAGMA page_count", pages) != SQLITE_OK ||
        db_.query_int("PRAGMA page_size", page_size) != SQLITE_OK ||
        db_.query_int("PRAGMA freelist_count", free_pages) != SQLITE_OK) {
        return;
    }
    if (mode == 2) {
        incremental_ = true;
        reclaim_pending_ = free_pages > 0; // left over from a session that ended mid-way
        return;
    }
    if (mode != 0 || pages <= 0 || skip_replay_ || !instance_locked_ ||
        sqlite3_db_readonly(db_.get(), "main") != 0) {
        return;
    }
    const auto unit = static_cast<std::uint64_t>(page_size);
    const std::uint64_t file_bytes = static_cast<std::uint64_t>(pages) * unit;
    const std::uint64_t free_bytes = static_cast<std::uint64_t>(free_pages) * unit;
    if (free_bytes <= options_.compact_min_free_bytes ||
        free_bytes * 100 <= file_bytes * static_cast<std::uint64_t>(options_.compact_min_free_percent)) {
        return;
    }
    std::error_code ec;
    const fs::path temp = fs::temp_directory_path(ec);
    for (const fs::path& where : {directory_, temp}) {
        std::optional<std::uintmax_t> room;
        if (options_.free_space) {
            room = options_.free_space(where);
        } else if (const fs::space_info info = fs::space(where, ec); !ec) {
            room = info.available;
        }
        if (!room || *room < file_bytes) {
            log("history: " + path().string() + " has " + megabytes(free_bytes) +
                " of free pages, but " + where.string() +
                " has no room for a copy; the file is left as it is");
            return;
        }
    }
    {
        // A quit, or a run that is waiting to be saved, comes before a VACUUM that may take
        // seconds on a big file (a run that finishes during it stops it, see add_run).
        std::lock_guard lock(mutex_);
        if (stop_ || std::any_of(queue_.begin(), queue_.end(), is_run_job)) {
            log("history: compacting " + path().string() +
                " waits for a start with nothing to save first");
            return;
        }
        compact_abort_ = false;
        compacting_ = true;
    }
    sqlite3_progress_handler(db_.get(), 1000, &HistoryStore::compact_progress, this);
    int rc = db_.run_script("PRAGMA auto_vacuum = INCREMENTAL; VACUUM");
    if (rc == SQLITE_OK) {
        rc = db_.run_script("PRAGMA wal_checkpoint(TRUNCATE)"); // the file itself shrinks now
    }
    const bool stopped = rc == SQLITE_INTERRUPT;
    const std::string problem = stopped ? std::string() : db_.error_message();
    sqlite3_progress_handler(db_.get(), 0, nullptr, nullptr);
    {
        std::lock_guard lock(mutex_);
        compacting_ = false;
    }
    if (stopped) {
        // SQLite rolled the VACUUM back: the file is as it was, and the next start tries again.
        log("history: compacting " + path().string() +
            " stopped (quitting, or a run to save); left as it was");
        return;
    }
    if (rc != SQLITE_OK) {
        log("history: compacting " + path().string() + " failed (" + problem +
            "); left as it was");
        return;
    }
    std::int64_t after = 0;
    incremental_ = db_.query_int("PRAGMA auto_vacuum", after) == SQLITE_OK && after == 2;
    std::int64_t after_pages = 0;
    (void)db_.query_int("PRAGMA page_count", after_pages);
    log("history: " + path().string() + " compacted from " + megabytes(file_bytes) + " to " +
        megabytes(static_cast<std::uint64_t>(after_pages) * unit) +
        "; the space of deleted notes now goes back as runs are saved");
}

int HistoryStore::compact_progress(void* self) {
    auto* store = static_cast<HistoryStore*>(self);
    if (store->options_.compacting) {
        store->options_.compacting();
    }
    return store->compact_abort_.load() ? 1 : 0;
}

// What write_phase() writes with write_run(): a finished run, not a read, a pin or a library edit.
bool HistoryStore::is_run_job(const Job& job) {
    return job.extra == Job::Extra::None && job.practice_hash == 0 && job.prefs.empty() &&
           job.groups.empty() && job.sources.empty() && job.versions.empty();
}

// After a run that deleted event sets: hands their pages back, a few hundred a transaction and a
// few transactions a run, so a write never holds the lock long; what is left waits for the next run
// (and for the next start, which finds it in the free list).
void HistoryStore::reclaim_space() {
    if (!reclaim_pending_ || !incremental_) {
        return;
    }
    bool freed = false;
    for (int i = 0; i < kReclaimChunksPerJob; ++i) {
        std::int64_t free_pages = 0;
        if (db_.query_int("PRAGMA freelist_count", free_pages) != SQLITE_OK || free_pages <= 0) {
            reclaim_pending_ = false;
            break;
        }
        if (db_.run_script("PRAGMA incremental_vacuum(" + std::to_string(kReclaimPages) + ")") !=
            SQLITE_OK) {
            log("history: giving pages back failed (" + db_.error_message() + "); not tried again");
            reclaim_pending_ = false;
            break;
        }
        freed = true;
    }
    if (freed) {
        (void)db_.run_script("PRAGMA wal_checkpoint(PASSIVE)"); // lets the file shrink
    }
}

} // namespace etude::app
