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

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace etude::app {

namespace fs = std::filesystem;

namespace {

const std::string& select_runs_sql() {
    static const std::string sql = [] {
        std::string s = "SELECT id, profile_id, song_title";
        for (std::string_view name : kRunColumnNames) {
            s += ", ";
            s += name;
        }
        s += " FROM runs ORDER BY id";
        return s;
    }();
    return sql;
}
constexpr std::string_view kSelectMeasures =
    "SELECT run_id, measure, graded, missed, wrong FROM run_measures ORDER BY run_id, measure";

} // namespace

bool HistoryStore::has_groups() const {
    constexpr std::int64_t kGroupsVersion = 3;
    return !migrations_.empty() && migrations_.back().version >= kGroupsVersion;
}

// The old store: runs.dat. Read once, every run INSERTed (a run already there is skipped, so
// a second pass changes nothing), then the file renamed runs.dat.bak so it is never read again.
HistoryStore::Attempt HistoryStore::import_legacy_runs(std::string& why) {
    const fs::path legacy = directory_ / kRunsFileName;
    if (!path_present(legacy)) {
        return Attempt::Ready;
    }
    std::ifstream in(legacy, std::ios::binary);
    if (!in) {
        log("history: " + legacy.string() + " cannot be read; left as it is");
        return Attempt::Ready;
    }
    const std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    in.close();
    const RunsDecode decoded = decode_runs(bytes);
    if (!decoded.runs.empty()) {
        int rc = begin_.step() == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
        begin_.reset();
        for (std::size_t i = 0; rc == SQLITE_OK && i < decoded.runs.size(); ++i) {
            Job job;
            job.record = decoded.runs[i];
            rc = store_run(job);
        }
        if (rc == SQLITE_OK) {
            rc = commit_.step() == SQLITE_DONE ? SQLITE_OK : SQLITE_ERROR;
            commit_.reset();
        }
        if (rc != SQLITE_OK) {
            const Attempt a = fail(rc, why);
            rollback_.step();
            rollback_.reset();
            return a;
        }
    }
    std::string line = "history: " + std::to_string(decoded.runs.size()) + " runs moved from " +
                       legacy.filename().string() + " into " + path().filename().string();
    if (decoded.bad_header) {
        line += " (not a runs file)";
    } else if (decoded.damaged > 0 || decoded.newer > 0 || decoded.truncated) {
        line += " (" + std::to_string(decoded.damaged + (decoded.truncated ? 1 : 0)) +
                " damaged and " + std::to_string(decoded.newer) + " from a newer version skipped)";
    }
    const fs::path backup = unused_name(legacy, ".bak");
    std::error_code ec;
    if (!backup.empty()) {
        fs::rename(legacy, backup, ec);
    }
    if (backup.empty() || ec) {
        line += "; the old file could not be renamed and stays";
    } else {
        line += "; the old file is kept as " + backup.filename().string();
    }
    log(line);
    return Attempt::Ready;
}

// The jobs the last quit could not write (history_journal.hpp), written in
// order, each in a transaction of its own, before the runs are read, so they are in the history this
// start loads. A run already in the database is skipped by the same rule a repeated run always is
// (the unique index on core's same_run fields: profile, song hash, start time, mode, hands, speed,
// transposition), and a song's practice choices replace its row whole, except a row the database
// changed after the journal was written, which is left as it is. The library's changes are as
// repeatable: settings and sources are upserts, a group made or a song added twice keeps the row
// there, a rename, a move, a pin, a drop and a delete set the same state again, an override is an
// insert that keeps its row or a delete. So a journal that is replayed twice (the file could not be
// deleted, or the open was tried again) adds nothing.
// The file is deleted once every record is in; if that fails it is renamed history.journal.done, and
// if that fails it is emptied; only when all three fail does the next start read it again (and then
// the song settings, groups, sources and overrides in it may replace later changes of the same
// rows). It is read once per session whatever happens to it. A record the database refuses for any other reason than the machine's (a busy, full or damaged file
// ends the attempt, which is tried again) is skipped and the file kept as history.journal.rejected,
// as is a file with a damaged length in it, whole, since the records after it cannot be read. A
// journal that is not ours, or from a newer etude, is left as it is and said once. Records that
// cannot be read are counted and said at the end. Nothing is replayed without history.lock
// (instance_locked_): another etude may be replaying the same file. The attempt made while quitting
// does not replay (one fsynced commit per record is not "no retries"): the jobs held join the file
// behind the older records, and the next start replays all of it in order.
// Replays a record that is neither a run nor a song's practice choices: the library's changes,
// profiles and their settings, and a pin. `pin_dropped` is set for a pin of a run the database lacks.
int HistoryStore::replay_other(const JournalEntry& entry, bool& pin_dropped) {
    Job job;
    if (const auto* prefs = std::get_if<JournalPrefs>(&entry)) {
        job.prefs = prefs->items;
        return write_prefs(job);
    }
    if (const auto* groups = std::get_if<JournalGroups>(&entry)) {
        job.groups = groups->edits;
        return write_groups(job);
    }
    if (const auto* origins = std::get_if<JournalOrigins>(&entry)) {
        job.sources = origins->items;
        return write_sources(job);
    }
    if (const auto* versions = std::get_if<JournalVersions>(&entry)) {
        job.versions = versions->rows;
        return write_versions(job);
    }
    if (const auto* profiles = std::get_if<JournalProfiles>(&entry)) {
        job.profiles = profiles->rows;
        return write_profile_job(job);
    }
    if (const auto* text = std::get_if<JournalProfileSettings>(&entry)) {
        job.settings_profile = text->profile_id;
        job.settings_text = text->text;
        job.at_utc_s = text->updated_utc_s;
        return write_profile_job(job);
    }
    if (const auto* gone = std::get_if<JournalProfileDelete>(&entry)) {
        job.delete_profile = gone->profile_id;
        return write_profile_job(job);
    }
    const JournalPin& pin = std::get<JournalPin>(entry);
    job.extra = Job::Extra::Pin;
    job.profile_id = pin.profile_id; // a pin of a profile the database lacks matches no run
    job.run_id = pin.run_id;
    job.pinned = pin.pinned;
    pin_matched_ = true;
    const int rc = run_events_job(job);
    pin_dropped = rc == SQLITE_OK && !pin_matched_;
    return rc;
}

HistoryStore::Attempt HistoryStore::replay_journal(std::string& why) {
    const fs::path file = journal_path();
    if (journal_replayed_ || !path_present(file)) {
        return Attempt::Ready;
    }
    const auto leave = [&](const std::string& reason) {
        if (!journal_reported_) {
            journal_reported_ = true;
            log("history: " + file.string() + " is " + reason + "; left as it is");
            say("Practice runs kept at the last quit are in " + std::string(kJournalFileName) +
                ", " + reason + "; left as they are");
        }
        return Attempt::Ready;
    };
    if (!instance_locked_) {
        // Without history.lock another etude may be replaying the same file.
        return leave("not replayed here (the history lock could not be taken)");
    }
    std::error_code ec;
    if (!fs::is_regular_file(file, ec)) {
        return leave("not a file");
    }
    std::string bytes;
    bool unreadable = false;
    {
        std::ifstream in(file, std::ios::binary); // closed again before the file is touched
        bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
        unreadable = in.bad() || (bytes.empty() && fs::file_size(file, ec) > 0);
    }
    if (unreadable) {
        return leave("not readable");
    }
    const JournalDecode found = decode_journal(bytes);
    if (found.newer_version != 0) {
        return leave("from a newer Etude (journal version " +
                     std::to_string(found.newer_version) + ")");
    }
    if (found.bad_header) {
        return leave("not a journal");
    }
    if (skip_replay_ && !found.entries.empty()) {
        // The attempt made while quitting does not replay (one fsynced commit per record is not "no
        // retries"), and the jobs held must not be written ahead of the older ones in the file, so
        // they join it: the next start replays all of it, in order.
        why = file.string() + " is not replayed while quitting";
        return Attempt::Locked;
    }
    // A song's row the database changed after the journal was written (the player kept playing, or
    // this journal came back) is left as it is.
    sql::Statement changed;
    const bool can_guard =
        has_practice() &&
        changed.prepare(db_.get(), "SELECT updated_utc_s FROM song_practice WHERE profile_id = ?1 "
                                   "AND content_hash = ?2") == SQLITE_OK;
    int runs = 0;
    int settings = 0;
    int library = 0;
    int rejected = 0;
    int superseded = 0;
    int dropped = 0;
    for (const JournalEntry& entry : found.entries) {
        Job job;
        int rc = SQLITE_OK;
        int* counted = &library;
        if (const auto* run = std::get_if<JournalRun>(&entry)) {
            job.profile_id = run->profile_id; // a run of a deleted profile is dropped by write_run
            job.record = run->record;
            job.song_path = run->song_path;
            job.events = run->events;
            rc = write_run(job);
            counted = &runs;
        } else if (const auto* practice = std::get_if<JournalPractice>(&entry)) {
            job.profile_id = practice->profile_id; // ... and its choices by write_practice
            job.practice_hash = practice->hash;
            job.practice = practice->practice;
            job.at_utc_s = practice->at_utc_s;
            if (can_guard && practice->at_utc_s > 0) {
                changed.bind(1, job.profile_id);
                changed.bind(2, hash_to_column(practice->hash));
                const bool newer_row =
                    changed.step() == SQLITE_ROW && changed.column_int(0) > practice->at_utc_s;
                changed.reset();
                if (newer_row) {
                    ++superseded;
                    continue;
                }
            }
            rc = write_practice(job);
            counted = &settings;
        } else {
            bool pin_dropped = false;
            rc = replay_other(entry, pin_dropped);
            if (pin_dropped) {
                ++dropped; // the run is not in this database
                continue;
            }
        }
        if (rc == SQLITE_OK) {
            ++*counted;
            continue;
        }
        const sql::Failure failure = sql::classify(rc);
        if (failure == sql::Failure::Other) {
            ++rejected;
            log("history: a record of " + file.string() + " was refused by the database (" +
                std::string(sqlite3_errstr(rc)) + "); skipped");
            continue;
        }
        why = db_.error_message();
        if (why.empty() || why == "not an error") {
            why = sqlite3_errstr(rc);
        }
        why = "replaying " + file.string() + ": " + why;
        return failure == sql::Failure::Locked    ? Attempt::Locked
               : failure == sql::Failure::Damaged ? Attempt::Damaged
                                                  : Attempt::Unusable;
    }
    changed = {};
    journal_replayed_ = true; // however the file ends up, it is not read again this session
    if (found.damaged > 0 || found.truncated) {
        log("history: " + file.string() + " had " + std::to_string(found.damaged) +
            " damaged records" + (found.truncated ? " and ended inside one" : "") +
            (found.framing_damaged ? "; the bytes after the damage were not read" : ""));
    }
    if (superseded > 0) {
        log("history: " + std::to_string(superseded) + " song settings of " + file.string() +
            " were left out: the database has newer ones");
    }
    // Done with the file: removed, or, when records were refused or the bytes after a damaged length
    // were never read, moved aside whole so nothing is lost with it. When that fails it is renamed
    // out of the way (.done); when that fails too it stays, and the next start reads it again, which
    // adds no run twice and leaves any song row changed since as it is.
    const auto remove_it = [&](const fs::path& p) {
        if (options_.remove_file) {
            return options_.remove_file(p);
        }
        std::error_code e;
        fs::remove(p, e);
        return !e;
    };
    const auto rename_it = [&](const fs::path& from, const fs::path& to) {
        if (options_.rename_file) {
            return options_.rename_file(from, to);
        }
        std::error_code e;
        fs::rename(from, to, e);
        return !e;
    };
    const auto truncate_it = [&](const fs::path& p) {
        if (options_.truncate_file) {
            return options_.truncate_file(p);
        }
        std::error_code e;
        fs::resize_file(p, 0, e);
        return !e;
    };
    const bool keep_it = rejected > 0 || found.framing_damaged;
    bool retired = false;
    if (keep_it) {
        const fs::path aside = unused_name(file, ".rejected");
        retired = !aside.empty() && rename_it(file, aside);
        if (retired) {
            log("history: " + file.string() + " is kept as " + aside.string() +
                (found.framing_damaged ? " with the bytes after its damaged record" : "") +
                (rejected > 0 ? " with the records the database refused" : ""));
        }
    } else {
        retired = remove_it(file);
    }
    if (!retired) {
        const fs::path done = unused_name(file, ".done");
        retired = !done.empty() && rename_it(file, done);
        if (retired) {
            log("history: " + file.string() + " could not be " + (keep_it ? "kept aside" : "removed") +
                "; renamed " + done.string() + " so it is not replayed again");
        }
    }
    if (!retired && truncate_it(file)) {
        retired = true;
        log("history: " + file.string() + " could not be " + (keep_it ? "kept aside" : "removed") +
            " or renamed; emptied so it is not replayed again");
    }
    if (!retired) {
        log("history: " + file.string() +
            " could not be removed, renamed or emptied; the next start reads it again: no run is "
            "added twice and a song's practice choices changed since are left as they are, but its "
            "song settings, groups, sources and overrides may replace later changes");
        say("Practice history: " + std::string(kJournalFileName) +
            " could not be removed after it was saved");
    }
    // Records that could not be read are gone; the player is told how many.
    const int unread = found.damaged + (found.truncated ? 1 : 0);
    if (unread > 0) {
        say("Practice history: " + std::to_string(unread) +
            (unread == 1 ? " record" : " records") + " kept at the last quit could not be read from " +
            std::string(kJournalFileName) + " and " + (unread == 1 ? "is" : "are") + " lost");
    }
    if (runs + settings + library + dropped > 0) {
        log("history: saved " + std::to_string(runs) + " runs, " + std::to_string(settings) +
            " song settings and " + std::to_string(library) +
            " library changes kept at the last quit" +
            (dropped > 0 ? "; " + std::to_string(dropped) + " dropped" : std::string()));
    }
    return Attempt::Ready;
}

HistoryStore::Attempt HistoryStore::load_runs(std::string& why) {
    const int rc = read_all_runs(thread_runs_);
    return rc == SQLITE_OK ? Attempt::Ready : fail(rc, why);
}

// One statement for the runs and one for their bars, each a plain read of the open database.
int HistoryStore::read_all_runs(std::vector<ProfiledRun>& out) {
    sql::Statement select;
    int rc = select.prepare(db_.get(), select_runs_sql());
    if (rc != SQLITE_OK) {
        return rc;
    }
    const std::size_t first = out.size();
    std::vector<std::int64_t> ids;
    while ((rc = select.step()) == SQLITE_ROW) {
        RunColumns columns;
        columns.song_title = select.column_text(2);
        for (std::size_t i = 0; i < kRunColumnCount; ++i) {
            columns.ints[i] = select.column_int(static_cast<int>(3 + i));
        }
        if (std::optional<RunRecord> run = from_columns(columns)) {
            ids.push_back(select.column_int(0));
            out.push_back({select.column_int(1), std::move(*run)});
        }
    }
    if (rc != SQLITE_DONE) {
        return rc;
    }
    // Both queries run in run id order, so the bars are matched to their run in one pass.
    sql::Statement bars;
    rc = bars.prepare(db_.get(), kSelectMeasures);
    if (rc != SQLITE_OK) {
        return rc;
    }
    std::size_t at = 0;
    while ((rc = bars.step()) == SQLITE_ROW) {
        const std::int64_t run_id = bars.column_int(0);
        while (at < ids.size() && ids[at] < run_id) {
            ++at;
        }
        if (at < ids.size() && ids[at] == run_id) {
            RunBar bar;
            bar.measure = static_cast<int>(bars.column_int(1));
            bar.graded = static_cast<int>(bars.column_int(2));
            bar.missed = static_cast<int>(bars.column_int(3));
            bar.wrong = static_cast<int>(bars.column_int(4));
            out[first + at].run.measures.push_back(bar);
        }
    }
    if (rc != SQLITE_DONE) {
        return rc;
    }
    for (std::size_t i = first; i < out.size(); ++i) {
        out[i].run = sanitized(out[i].run);
    }
    return SQLITE_OK;
}

} // namespace etude::app
