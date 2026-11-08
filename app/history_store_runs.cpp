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

// One run: its song's row, the run row and its bars. No transaction of its own.
int HistoryStore::store_run(const Job& job) {
    const RunRecord& run = job.record;
    const RunColumns columns = to_columns(run);
    const std::string key = song_key(run.song_hash, job.song_path);
    std::int64_t song_id = 0;
    if (!key.empty()) {
        upsert_song_.bind(1, key);
        upsert_song_.bind(2, hash_to_column(run.song_hash));
        upsert_song_.bind(3, job.song_path);
        upsert_song_.bind(4, run.song_title);
        upsert_song_.bind(5, run.end_utc_s);
        int rc = upsert_song_.step();
        if (rc == SQLITE_ROW) {
            song_id = upsert_song_.column_int(0);
            rc = upsert_song_.step(); // RETURNING: the row is out, finish the statement
        }
        const int reset = upsert_song_.reset();
        if (rc != SQLITE_DONE) {
            return rc;
        }
        if (reset != SQLITE_OK) {
            return reset;
        }
    }
    const std::int64_t profile = stored_profile(job.profile_id);
    insert_run_.bind(1, profile);
    if (song_id != 0) {
        insert_run_.bind(2, song_id);
    } else {
        insert_run_.bind_null(2);
    }
    insert_run_.bind(3, columns.song_title);
    for (std::size_t i = 0; i < kRunColumnCount; ++i) {
        insert_run_.bind(static_cast<int>(4 + i), columns.ints[i]);
    }
    int rc = insert_run_.step();
    insert_run_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    if (db_.changes() == 0) {
        return SQLITE_OK; // a repeat of a run already stored
    }
    const std::int64_t run_id = db_.last_insert_rowid();
    for (const RunBar& bar : columns.measures) {
        insert_measure_.bind(1, run_id);
        insert_measure_.bind(2, bar.measure);
        insert_measure_.bind(3, bar.graded);
        insert_measure_.bind(4, bar.missed);
        insert_measure_.bind(5, bar.wrong);
        rc = insert_measure_.step();
        insert_measure_.reset();
        if (rc != SQLITE_DONE) {
            return rc;
        }
    }
    if (has_events() && run.song_hash != 0 && !job.events.events.empty()) {
        return store_events(run_id, job);
    }
    return SQLITE_OK;
}

// The run's events as a set and its rows, then the song's older sets beyond the newest few. A
// song with no content hash keeps none (its runs cannot be told from another song's).
int HistoryStore::store_events(std::int64_t run_id, const Job& job) {
    const std::int64_t profile = stored_profile(job.profile_id);
    const int keep = events_kept_.load();
    int rc = SQLITE_OK;
    if (keep > 0) {
        const RunEvents& set = job.events;
        insert_event_set_.bind(1, run_id);
        insert_event_set_.bind(2, static_cast<std::int64_t>(set.events.size()));
        insert_event_set_.bind(3, static_cast<std::int64_t>(set.expected_notes));
        insert_event_set_.bind(4, static_cast<std::int64_t>(set.dropped));
        insert_event_set_.bind(5, static_cast<std::int64_t>(set.complete ? 1 : 0));
        rc = insert_event_set_.step();
        insert_event_set_.reset();
        if (rc != SQLITE_DONE) {
            return rc;
        }
        std::int64_t seq = 0;
        for (const RunEvent& e : set.events) {
            insert_event_.bind(1, run_id);
            insert_event_.bind(2, seq++);
            insert_event_.bind(3, static_cast<std::int64_t>(e.kind));
            insert_event_.bind(4, static_cast<std::int64_t>(e.song_ms));
            insert_event_.bind(5, static_cast<std::int64_t>(e.error_ms));
            insert_event_.bind(6, static_cast<std::int64_t>(e.key));
            insert_event_.bind(7, static_cast<std::int64_t>(e.velocity));
            if (e.kind == RunEventKind::Wrong) {
                insert_event_.bind_null(8);
                insert_event_.bind_null(9);
                insert_event_.bind_null(10);
            } else {
                insert_event_.bind(8, static_cast<std::int64_t>(e.note_index));
                insert_event_.bind(9, static_cast<std::int64_t>(e.hand));
                insert_event_.bind(10, static_cast<std::int64_t>(e.grade));
            }
            rc = insert_event_.step();
            insert_event_.reset();
            if (rc != SQLITE_DONE) {
                return rc;
            }
        }
    }
    prune_events_.bind(1, profile);
    prune_events_.bind(2, hash_to_column(job.record.song_hash));
    prune_events_.bind(3, static_cast<std::int64_t>(keep));
    prune_events_.bind(4, static_cast<std::int64_t>(keep > 0 ? 1 : 0)); // keeping none keeps none
    rc = prune_events_.step();
    prune_events_.reset();
    if (rc == SQLITE_DONE && db_.changes() > 0) {
        reclaim_pending_ = true; // sets were deleted: their pages go back after the commit
    }
    return rc == SQLITE_DONE ? SQLITE_OK : rc;
}

// A read or a pin, outside any run's transaction (a read is one query; a pin one statement).
int HistoryStore::run_events_job(const Job& job) {
    if (job.extra == Job::Extra::ReadProgress) {
        return run_progress_job(job);
    }
    if (!has_events()) {
        pin_matched_ = job.extra != Job::Extra::Pin; // no notes here, so nothing to pin
        std::lock_guard lock(mutex_);
        if (job.serial != 0) {
            event_reads_.push_back(read_answer(job, job.song_hash, std::nullopt));
        }
        return SQLITE_OK;
    }
    if (job.extra == Job::Extra::Pin) {
        pin_matched_ = true;
        pin_events_.bind(1, job.run_id);
        pin_events_.bind(2, static_cast<std::int64_t>(job.pinned ? 1 : 0));
        pin_events_.bind(3, job.profile_id);
        const int rc = pin_events_.step();
        pin_events_.reset();
        if (rc == SQLITE_DONE && sqlite3_changes(db_.get()) == 0) {
            pin_matched_ = false;
            log("history: a pin of run " + std::to_string(job.run_id) +
                " matched no run with played notes; dropped");
        }
        if (rc == SQLITE_DONE && job.serial != 0) {
            std::lock_guard lock(mutex_);
            event_reads_.push_back(read_answer(job, 0, std::nullopt, pin_matched_));
        }
        return rc == SQLITE_DONE ? SQLITE_OK : rc;
    }
    std::optional<StoredRunEvents> found;
    std::int64_t run_id = job.run_id;
    const std::int64_t profile = stored_profile(job.profile_id);
    if (job.extra == Job::Extra::ReadReplay && run_id == 0 && job.replay) {
        // A run just finished: the frame knows its record, not the id the database gave it. It is
        // looked up under the profile the request was made for.
        if (const int found_rc = find_run_id(job.replay->record, profile, run_id);
            found_rc != SQLITE_OK) {
            return found_rc;
        }
    }
    const int rc = job.extra == Job::Extra::ReadGhost ? read_ghost(job, found)
                   : run_id == 0                      ? SQLITE_OK
                                                      : read_events(run_id, profile, found);
    if (rc != SQLITE_OK) {
        return rc; // the job is held or the file set aside and the read tried again
    }
    if (job.extra == Job::Extra::ReadReplay) {
        // Laid over the song and summed here, on this thread: the frame takes the finished answer.
        EventsRead answer = read_answer(job, job.song_hash, std::nullopt);
        answer.run_id = found ? run_id : 0;
        answer.pinned = found && found->pinned;
        // A stale run id must not put another song's notes against these tables. A run whose notes
        // are not there (pruned, never kept, or no such run) gets the summary of its record, built
        // here too: the frame does no work on the answer.
        if (job.replay) {
            const ReplaySong song{*job.replay->notes, *job.replay->measures};
            const bool played = found && found->song_hash == job.song_hash;
            answer.replay = std::make_shared<const RunReplay>(build_run_replay(
                job.replay->record, played ? found->events : RunEvents{}, song));
        }
        std::lock_guard lock(mutex_);
        event_reads_.push_back(std::move(answer));
        return SQLITE_OK;
    }
    const std::uint64_t song_hash = job.extra == Job::Extra::ReadGhost ? job.song_hash
                                    : found                           ? found->song_hash
                                                                      : 0;
    std::lock_guard lock(mutex_);
    event_reads_.push_back(read_answer(job, song_hash, std::move(found)));
    return SQLITE_OK;
}

// The id of the run `record` describes, 0 when the database has none (a write that has not
// landed, or one the database refused). The run is named by the fields of core's same_run().
int HistoryStore::find_run_id(const RunRecord& record, std::int64_t profile_id, std::int64_t& id) {
    id = 0;
    select_run_id_.bind(1, profile_id);
    select_run_id_.bind(2, hash_to_column(record.song_hash));
    select_run_id_.bind(3, record.start_utc_s);
    select_run_id_.bind(4, static_cast<std::int64_t>(record.mode == RunMode::Wait ? 1 : 0));
    select_run_id_.bind(5, static_cast<std::int64_t>(record.hands == HandFilter::Left    ? 1
                                                     : record.hands == HandFilter::Right ? 2
                                                                                         : 0));
    select_run_id_.bind(6, static_cast<std::int64_t>(record.speed_percent));
    select_run_id_.bind(7, static_cast<std::int64_t>(record.transpose_semitones));
    const int rc = select_run_id_.step();
    if (rc == SQLITE_ROW) {
        id = select_run_id_.column_int(0);
    }
    select_run_id_.reset();
    return rc == SQLITE_ROW || rc == SQLITE_DONE ? SQLITE_OK : rc;
}

// One run's set and events. `out` stays empty when the run has none.
int HistoryStore::read_events(std::int64_t run_id, std::int64_t profile_id,
                              std::optional<StoredRunEvents>& out) {
    select_event_set_.bind(1, run_id);
    select_event_set_.bind(2, profile_id);
    int rc = select_event_set_.step();
    if (rc == SQLITE_DONE) {
        select_event_set_.reset();
        return SQLITE_OK;
    }
    if (rc != SQLITE_ROW) {
        select_event_set_.reset();
        return rc;
    }
    StoredRunEvents stored;
    stored.run_id = run_id;
    const std::int64_t mode = select_event_set_.column_int(0);
    const std::int64_t hands = select_event_set_.column_int(1);
    stored.setup.mode = mode == 1 ? RunMode::Wait : RunMode::Flow;
    stored.setup.hands = hands == 1   ? HandFilter::Left
                         : hands == 2 ? HandFilter::Right
                                      : HandFilter::Both;
    stored.setup.speed_percent = static_cast<int>(select_event_set_.column_int(2));
    stored.setup.transpose_semitones = static_cast<int>(select_event_set_.column_int(3));
    stored.all.graded = static_cast<int>(select_event_set_.column_int(4));
    stored.all.hit = static_cast<int>(select_event_set_.column_int(5));
    stored.events.dropped = static_cast<std::int32_t>(select_event_set_.column_int(6));
    stored.events.expected_notes = static_cast<std::int32_t>(select_event_set_.column_int(7));
    stored.events.complete = select_event_set_.column_int(8) == 1;
    stored.pinned = select_event_set_.column_int(9) == 1;
    stored.song_hash = hash_from_column(select_event_set_.column_int(10));
    select_event_set_.reset();

    select_events_.bind(1, run_id);
    while ((rc = select_events_.step()) == SQLITE_ROW) {
        RunEvent e;
        const std::int64_t kind = select_events_.column_int(0);
        e.kind = kind == 0   ? RunEventKind::Press
                 : kind == 2 ? RunEventKind::Missed
                             : RunEventKind::Wrong;
        e.song_ms = static_cast<std::int32_t>(
            std::clamp<std::int64_t>(select_events_.column_int(1), INT32_MIN, INT32_MAX));
        e.error_ms = static_cast<std::int32_t>(
            std::clamp<std::int64_t>(select_events_.column_int(2), INT32_MIN, INT32_MAX));
        e.key = static_cast<std::uint8_t>(std::clamp<std::int64_t>(select_events_.column_int(3), 0, 255));
        e.velocity = static_cast<std::uint8_t>(
            std::clamp<std::int64_t>(select_events_.column_int(4), 0, 255));
        if (!select_events_.column_is_null(5)) {
            e.note_index = static_cast<std::int32_t>(
                std::clamp<std::int64_t>(select_events_.column_int(5), -1, INT32_MAX));
            e.hand = static_cast<Hand>(std::clamp<std::int64_t>(select_events_.column_int(6), 0, 3));
            e.grade = static_cast<NoteGrade>(
                std::clamp<std::int64_t>(select_events_.column_int(7), 0, 255));
        }
        stored.events.events.push_back(sanitized(e));
    }
    select_events_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    out = std::move(stored);
    return SQLITE_OK;
}

// The song's candidates go to core's rule; the one it picks has its events read.
int HistoryStore::read_ghost(const Job& job, std::optional<StoredRunEvents>& out) {
    const std::int64_t profile = stored_profile(job.profile_id);
    select_candidates_.bind(1, profile);
    select_candidates_.bind(2, hash_to_column(job.song_hash));
    std::vector<GhostCandidate> candidates;
    int rc = SQLITE_OK;
    while ((rc = select_candidates_.step()) == SQLITE_ROW) {
        GhostCandidate c;
        c.run_id = select_candidates_.column_int(0);
        const std::int64_t mode = select_candidates_.column_int(1);
        const std::int64_t hands = select_candidates_.column_int(2);
        c.setup.mode = mode == 1 ? RunMode::Wait : RunMode::Flow;
        c.setup.hands = hands == 1   ? HandFilter::Left
                        : hands == 2 ? HandFilter::Right
                                     : HandFilter::Both;
        c.setup.speed_percent = static_cast<int>(select_candidates_.column_int(3));
        c.setup.transpose_semitones = static_cast<int>(select_candidates_.column_int(4));
        c.complete = select_candidates_.column_int(5) == 1;
        c.all.graded = static_cast<int>(select_candidates_.column_int(6));
        c.all.hit = static_cast<int>(select_candidates_.column_int(7));
        candidates.push_back(c);
    }
    select_candidates_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    const std::optional<std::size_t> best = best_ghost_run(job.setup, candidates);
    if (!best) {
        return SQLITE_OK;
    }
    return read_events(candidates[*best].run_id, profile, out);
}

// One run in one transaction. A run of a profile the database does not hold (deleted since the run
// began, or a journal that outlived the profile) is dropped: it is never given to another profile.
int HistoryStore::write_run(const Job& job) {
    if (!profile_known(job.profile_id)) {
        log("history: a run of profile " + std::to_string(job.profile_id) + ", which is gone; dropped");
        return SQLITE_OK;
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = store_run(job);
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
    } else {
        reclaim_space();
    }
    return rc;
}

} // namespace etude::app
