// SPDX-License-Identifier: GPL-3.0-or-later
#include "history_store.hpp"
#include "crash_thread.hpp"
#include "log_ring.hpp"

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

HistoryStore::HistoryStore(fs::path directory, HistoryOptions options)
    : directory_(std::move(directory)), options_(std::move(options)) {
    active_ = options_.profile_id > 0 ? options_.profile_id : kDefaultProfileId;
}

HistoryStore::~HistoryStore() { close(); }

void HistoryStore::close() {
    if (thread_.joinable()) {
        {
            std::lock_guard lock(mutex_);
            stop_ = true;
            if (compacting_) {
                compact_abort_ = true; // a VACUUM of a big file must not hold the quit
            }
        }
        work_.notify_all();
        thread_.join(); // saves what is queued first, journals what it cannot
    }
    close_database();
    release_instance_lock();
}

void HistoryStore::log(std::string_view line) const {
    // The settings folder is under the user's home: a log shows it as "<settings>", so the user's
    // name and folders stay out of text that gets pasted into a bug report.
    std::string text(line);
    const std::string folder = directory_.string();
    if (!folder.empty()) {
        for (std::size_t at = text.find(folder); at != std::string::npos;
             at = text.find(folder, at + 10)) {
            text.replace(at, folder.size(), "<settings>");
        }
    }
    if (options_.log) {
        options_.log(text);
    } else {
        log_printf( "%s\n", text.c_str());
    }
}

void HistoryStore::close_database() {
    // Statements first: a connection with a live statement does not close.
    begin_ = {};
    commit_ = {};
    rollback_ = {};
    upsert_song_ = {};
    insert_run_ = {};
    insert_measure_ = {};
    upsert_profile_ = {};
    upsert_profile_settings_ = {};
    upsert_prefs_ = {};
    insert_group_ = {};
    rename_group_ = {};
    delete_group_ = {};
    delete_group_songs_ = {};
    add_group_song_ = {};
    drop_group_song_ = {};
    move_group_ = {};
    upsert_source_ = {};
    insert_override_ = {};
    delete_override_ = {};
    upsert_practice_ = {};
    delete_parts_ = {};
    insert_part_ = {};
    insert_event_set_ = {};
    insert_event_ = {};
    prune_events_ = {};
    pin_events_ = {};
    select_event_set_ = {};
    select_events_ = {};
    select_candidates_ = {};
    select_run_id_ = {};
    incremental_ = false;
    reclaim_pending_ = false;
    db_.close();
}

void HistoryStore::open() {
    migrations_ = options_.migrations.empty() ? history_migrations() : options_.migrations;
    thread_ = spawn_thread([this] { thread_main(); });
}

HistoryPoll HistoryStore::poll() {
    HistoryPoll result;
    std::lock_guard lock(mutex_);
    if (load_ready_) {
        apply_loaded_profiles(result);
        apply_loaded_library();
        load_ready_ = false;
        loaded_ = true;
        result.loaded = true;
    }
    apply_deletions(result);
    for (const std::string& m : messages_) {
        result.message += result.message.empty() ? m : "; " + m;
    }
    messages_.clear();
    return result;
}

// The profiles, their settings, and what is each profile's: the runs and the practice choices. The
// active profile's are laid under this session's; the active one may be gone (a database
// replaced), and then the session's runs and choices were the default profile's all along.
void HistoryStore::apply_loaded_profiles(HistoryPoll& result) {
    profiles_ = ProfileSet(loaded_profile_rows_);
    loaded_profile_rows_.clear();
    if (!profiles_.contains(active_)) {
        active_ = kDefaultProfileId;
        result.profile_changed = true;
    }
    for (auto& [id, text] : loaded_profile_settings_) {
        profile_settings_.try_emplace(id, std::move(text)); // what this session set wins
    }
    loaded_profile_settings_.clear();
    // The active profile's runs from the database are older than any this session added; the
    // other profiles' are kept for the household boards and for a switch.
    std::vector<RunRecord> merged;
    for (ProfiledRun& stored : loaded_runs_) {
        (stored.profile_id == active_ ? merged : other_runs_[stored.profile_id])
            .push_back(std::move(stored.run));
    }
    loaded_runs_.clear();
    if (!runs_.empty()) { // none usually: a reserve would move every loaded run to a new buffer
        merged.reserve(merged.size() + runs_.size());
        for (RunRecord& run : runs_) {
            merged.push_back(std::move(run));
        }
    }
    runs_ = std::move(merged);
    for (ProfiledPractice& stored : loaded_practice_) {
        (stored.profile_id == active_ ? practice_ : other_practice_[stored.profile_id])
            .try_emplace(stored.hash, std::move(stored.practice)); // this session's choice wins
    }
    loaded_practice_.clear();
}

// The library's own rows: what this session set wins over what the database had.
void HistoryStore::apply_loaded_library() {
    for (auto& [hash, prefs] : loaded_prefs_) {
        prefs_.try_emplace(hash, std::move(prefs));
    }
    loaded_prefs_.clear();
    for (auto& [hash, source] : loaded_sources_) {
        sources_.try_emplace(hash, std::move(source));
    }
    loaded_sources_.clear();
    // The overrides as stored, with this session's edits laid over them.
    {
        VersionOverrides stored;
        for (const VersionRow& row : loaded_versions_) {
            if (row.b == 0) {
                stored.splits.push_back(row.a);
            } else {
                stored.joins.emplace_back(std::min(row.a, row.b), std::max(row.a, row.b));
            }
        }
        std::sort(stored.splits.begin(), stored.splits.end());
        stored.splits.erase(std::unique(stored.splits.begin(), stored.splits.end()),
                            stored.splits.end());
        std::sort(stored.joins.begin(), stored.joins.end());
        stored.joins.erase(std::unique(stored.joins.begin(), stored.joins.end()),
                           stored.joins.end());
        // The rows these edits change in what the database had are written too: a clear the
        // session's own state saw nothing to change in still removes a stored split.
        std::vector<VersionRow> rows;
        for (const VersionEdit& edit : override_edits_) {
            apply_version_edit(stored, edit, &rows);
        }
        overrides_ = std::move(stored);
        ++overrides_serial_;
        if (!rows.empty() && accepting_) {
            Job job;
            job.versions = std::move(rows);
            queue_.push_back(std::move(job));
            work_.notify_one();
        }
    }
    loaded_versions_.clear();
    override_edits_.clear();
    // The groups as stored, with this session's edits laid over them.
    groups_.replay(loaded_groups_, loaded_members_, group_edits_);
    loaded_groups_.clear();
    loaded_members_.clear();
    group_edits_.clear();
}

void HistoryStore::add_run(const RunRecord& record, std::string_view song_path) {
    add_run(record, song_path, RunEvents{});
}

void HistoryStore::add_run(const RunRecord& record, std::string_view song_path,
                           RunEvents&& events, std::int64_t profile_id) {
    RunRecord clean = sanitized(record);
    const std::int64_t owner = profile_id > 0 ? profile_id : active_;
    if (loaded_ && !profiles_.contains(owner)) {
        return; // the profile was deleted since the run began: nobody's run now
    }
    // A profile that is not the active one (played before a switch) has its list in other_runs_.
    (owner == active_ || !loaded_ ? runs_ : other_runs_[owner]).push_back(clean);
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        Job job;
        job.profile_id = owner;
        job.record = std::move(clean);
        job.song_path = std::string(song_path);
        job.events = std::move(events);
        queue_.push_back(std::move(job));
        if (compacting_) {
            compact_abort_ = true; // a finished run is saved first; the VACUUM waits for a start
        }
    }
    work_.notify_one();
}

void HistoryStore::set_events_kept_per_song(int count) {
    events_kept_.store(std::clamp(count, 0, kMaxRunEventsKept));
}

std::uint64_t HistoryStore::request_run_events(std::int64_t run_id) {
    Job job;
    job.extra = Job::Extra::ReadRun;
    job.profile_id = active_;
    job.run_id = run_id;
    std::lock_guard lock(mutex_);
    if (!accepting_) {
        return 0;
    }
    job.serial = ++read_serial_;
    queue_.push_back(std::move(job));
    work_.notify_one();
    return read_serial_;
}

std::uint64_t HistoryStore::request_replay(std::int64_t run_id, ReplayRequest request) {
    if (!request.notes || !request.measures) {
        return 0;
    }
    Job job;
    job.extra = Job::Extra::ReadReplay;
    job.profile_id = request.profile_id > 0 ? request.profile_id : active_;
    job.run_id = run_id;
    job.song_hash = request.record.song_hash;
    job.replay = std::make_shared<const ReplayRequest>(std::move(request));
    std::lock_guard lock(mutex_);
    if (!accepting_) {
        return 0;
    }
    job.serial = ++read_serial_;
    queue_.push_back(std::move(job));
    work_.notify_one();
    return read_serial_;
}

std::vector<EventsRead> HistoryStore::take_event_reads() {
    std::lock_guard lock(mutex_);
    return std::exchange(event_reads_, {});
}

std::uint64_t HistoryStore::set_run_pinned(std::int64_t run_id, bool pinned) {
    Job job;
    job.extra = Job::Extra::Pin;
    job.profile_id = active_; // only the active profile's own runs can be pinned
    job.run_id = run_id;
    job.pinned = pinned;
    std::uint64_t serial = 0;
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return 0;
        }
        serial = job.serial = ++read_serial_;
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
    return serial;
}

void HistoryStore::set_song_practice(std::uint64_t hash, const SongPractice& practice) {
    if (hash == 0) {
        return;
    }
    Job job;
    job.profile_id = active_;
    job.practice_hash = hash;
    job.practice = sanitized(practice);
    job.at_utc_s = std::chrono::duration_cast<std::chrono::seconds>(
                       std::chrono::system_clock::now().time_since_epoch())
                       .count();
    practice_[hash] = job.practice;
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

void HistoryStore::set_song_origin(std::uint64_t hash, const SongOrigin& source) {
    if (hash == 0) {
        return;
    }
    sources_[hash] = source;
    Job job;
    job.sources.emplace_back(hash, source);
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

void HistoryStore::edit_version_overrides(const VersionEdit& edit) {
    // Before the load every edit is kept, even one that changes nothing in this session's own
    // state: it may change what the database had (poll() replays it over that and writes the rows).
    if (!loaded_) {
        override_edits_.push_back(edit);
    }
    std::vector<VersionRow> rows;
    apply_version_edit(overrides_, edit, &rows);
    if (rows.empty()) {
        return;
    }
    ++overrides_serial_;
    Job job;
    job.versions = std::move(rows);
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

void HistoryStore::set_song_prefs(std::uint64_t hash, const SongPrefs& prefs) {
    const std::pair<std::uint64_t, SongPrefs> one{hash, prefs};
    set_song_prefs(std::span<const std::pair<std::uint64_t, SongPrefs>>(&one, 1));
}

void HistoryStore::set_song_prefs(std::span<const std::pair<std::uint64_t, SongPrefs>> many) {
    Job job;
    for (const auto& [hash, prefs] : many) {
        if (hash == 0) {
            continue;
        }
        SongPrefs clean = sanitized(prefs);
        SongPrefs& held = prefs_[hash];
        // The date added stays the earliest one known.
        if (held.added_utc_s > 0 && (clean.added_utc_s == 0 || held.added_utc_s < clean.added_utc_s)) {
            clean.added_utc_s = held.added_utc_s;
        }
        held = clean;
        job.prefs.emplace_back(hash, std::move(clean));
    }
    if (job.prefs.empty()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

void HistoryStore::edit_groups(const GroupEdit& edit) { edit_groups(std::span(&edit, 1)); }

void HistoryStore::edit_groups(std::span<const GroupEdit> edits) {
    Job job;
    for (const GroupEdit& edit : edits) {
        if (!groups_.apply(edit)) {
            continue;
        }
        if (!loaded_) {
            group_edits_.push_back(edit); // laid over what the database has once it is read
        }
        job.groups.push_back(edit);
    }
    if (job.groups.empty()) {
        return;
    }
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

bool HistoryStore::wait_until_settled(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    // The state itself is what is waited on (set_state publishes it under mutex_): a separate
    // "settled" flag raised a step before the state was set let this return with it still Loading.
    return changed_.wait_for(lock, timeout,
                             [this] { return state_.load() != HistoryState::Loading; });
}

bool HistoryStore::flush(std::chrono::milliseconds timeout) {
    std::unique_lock lock(mutex_);
    return changed_.wait_for(lock, timeout,
                             [this] { return (queue_.empty() && !busy_) || !accepting_; });
}

std::vector<std::string> HistoryStore::take_errors() {
    std::lock_guard lock(mutex_);
    return std::exchange(errors_, {});
}

HistoryStats HistoryStore::stats() const {
    std::lock_guard lock(mutex_);
    return stats_;
}

} // namespace etude::app
