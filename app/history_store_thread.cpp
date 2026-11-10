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

void HistoryStore::set_state(HistoryState next) {
    if (next != HistoryState::Waiting) {
        unwritable_.store(false);
    }
    state_.store(next);
    announce();
}

// The folder, or the file when there is one, cannot be written by this program (permissions, a
// read-only disk).
bool HistoryStore::not_writable_for_app() const {
    if (::access(directory_.c_str(), W_OK) != 0) {
        return true;
    }
    std::error_code ec;
    return fs::exists(path(), ec) && ::access(path().c_str(), W_OK) != 0;
}

void HistoryStore::say(std::string text) {
    std::lock_guard lock(mutex_);
    messages_.push_back(std::move(text));
}

bool HistoryStore::sleep_for_retry() {
    std::unique_lock lock(mutex_);
    work_.wait_for(lock, std::chrono::milliseconds(options_.retry_interval_ms),
                   [this] { return stop_; });
    return !stop_;
}

// Hands the runs read from the database to the frame thread and moves to `next`.
void HistoryStore::publish_loaded(HistoryState next) {
    std::lock_guard lock(mutex_);
    loaded_runs_ = std::move(thread_runs_);
    thread_runs_.clear();
    loaded_profile_rows_ = std::move(thread_profile_rows_);
    thread_profile_rows_.clear();
    loaded_profile_settings_ = std::move(thread_profile_settings_);
    thread_profile_settings_.clear();
    loaded_prefs_ = std::move(thread_prefs_);
    thread_prefs_.clear();
    loaded_groups_ = std::move(thread_groups_);
    thread_groups_.clear();
    loaded_sources_ = std::move(thread_sources_);
    thread_sources_.clear();
    loaded_versions_ = std::move(thread_versions_);
    thread_versions_.clear();
    loaded_practice_ = std::move(thread_practice_);
    thread_practice_.clear();
    loaded_members_ = std::move(thread_members_);
    thread_members_.clear();
    stats_.loaded_runs = static_cast<int>(loaded_runs_.size());
    // Jobs stamped for the profile this session started in, when the database lacks it, were the
    // default profile's all along: the load makes that the one in use (a run for a profile that is
    // gone is otherwise dropped, never given to another).
    const std::int64_t started = options_.profile_id > 0 ? options_.profile_id : kDefaultProfileId;
    if (!profile_known(started)) {
        for (Job& job : queue_) {
            if (job.profile_id == started) {
                job.profile_id = kDefaultProfileId;
            }
        }
    }
    load_ready_ = true;
    set_state(next);
}

void HistoryStore::thread_main() {
    std::error_code ec;
    fs::create_directories(directory_, ec);
    if (!take_instance_lock()) {
        read_only_phase();
        std::lock_guard lock(mutex_);
        answer_reads_unread();
        return;
    }
    if (open_loop(true)) {
        publish_loaded(HistoryState::Ready);
        write_phase();
    }
    std::lock_guard lock(mutex_);
    answer_reads_unread();
}

// A store that ends with reads still queued (the database never came back, or it is closing)
// answers each with nothing, so no request waits for ever.
void HistoryStore::answer_reads_unread() {
    for (const Job& job : queue_) {
        if (job.serial != 0) {
            answer_nothing(job);
        }
    }
}

void HistoryStore::answer_nothing(const Job& job) {
    if (job.extra == Job::Extra::ReadProgress) {
        progress_answers_.push_back({job.serial, nullptr});
    } else {
        event_reads_.push_back(read_answer(job, job.song_hash, std::nullopt));
    }
}

// The answer to a read or a pin job; `pin_took` only means something for a pin.
EventsRead HistoryStore::read_answer(const Job& job, std::uint64_t song_hash,
                                     std::optional<StoredRunEvents> found, bool pin_took) {
    const EventsReadKind kind = job.extra == Job::Extra::ReadGhost ? EventsReadKind::Ghost
                                : job.extra == Job::Extra::Pin     ? EventsReadKind::Pin
                                : job.extra == Job::Extra::ReadReplay ? EventsReadKind::Replay
                                                                   : EventsReadKind::Run;
    return {job.serial, kind, song_hash, std::move(found), pin_took};
}

// The lock file keeps two etudes from sharing one history. flock is released by the system when
// the process ends, however it ends.
bool HistoryStore::take_instance_lock() {
    const fs::path lock = directory_ / kHistoryLockName;
    lock_fd_ = ::open(lock.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (lock_fd_ < 0) {
        // A folder we cannot write to fails at the database anyway. Without the lock another
        // etude may be here too, so the history works as usual but replays no journal.
        return true;
    }
    if (::flock(lock_fd_, LOCK_EX | LOCK_NB) != 0) {
        ::close(lock_fd_);
        lock_fd_ = -1;
        return false;
    }
    instance_locked_ = true;
    return true;
}

void HistoryStore::release_instance_lock() {
    if (lock_fd_ >= 0) {
        ::close(lock_fd_);
        lock_fd_ = -1;
    }
}

// Another etude owns the history: read what is there, change nothing.
void HistoryStore::read_only_phase() {
    log("history: another Etude has " + path().string() + " open; this one reads it and does not save");
    say("Practice history is in use by another Etude; runs are not saved");
    const auto started = std::chrono::steady_clock::now();
    if (db_.open(path(), sql::Database::Mode::ReadOnly) == SQLITE_OK) {
        db_.set_busy_timeout(options_.open_busy_timeout_ms);
        std::int64_t version = 0;
        std::string why;
        // A database this build cannot read as it is (an older schema the other etude has not
        // migrated yet, or damage) counts as empty: nothing here may change it.
        const bool readable = db_.query_int("PRAGMA user_version", version) == SQLITE_OK &&
                              version == migrations_.back().version &&
                              load_profiles(why) == Attempt::Ready &&
                              load_runs(why) == Attempt::Ready &&
                              load_prefs(why) == Attempt::Ready &&
                              load_groups(why) == Attempt::Ready &&
                              load_sources(why) == Attempt::Ready &&
                              load_versions(why) == Attempt::Ready &&
                              load_practice(why) == Attempt::Ready;
        if (!readable) {
            // Also what an earlier start's unfinished write looks like to a reader: only a
            // read-write start can roll it back, and the next normal one does.
            log("history: " + path().string() + " cannot be read as it is" +
                (why.empty() ? std::string() : " (" + why + ")") + "; shown empty, nothing is changed");
            thread_profile_rows_.clear();
            thread_profile_settings_.clear();
            thread_versions_.clear();
            thread_practice_.clear();
            thread_runs_.clear();
            thread_prefs_.clear();
            thread_groups_.clear();
            thread_sources_.clear();
            thread_members_.clear();
        }
        close_database();
    }
    {
        std::lock_guard lock(mutex_);
        stats_.load_ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();
        accepting_ = false;
    }
    publish_loaded(HistoryState::ReadOnly);
}

// The first time the database cannot be had for now: one line in the log, one for the player, and the
// state Waiting (runs are kept in memory).
void HistoryStore::tell_waiting(Attempt attempt, const std::string& problem, const std::string& why) {
    log("history: " + path().string() + " is " + problem + " (" + why +
        "); runs are kept in memory and saved when it opens");
    if (attempt == Attempt::Unwritable) {
        say("Practice history can't be saved: Etude may not change its file or folder. "
            "Runs are saved when that is fixed");
    } else {
        say("Practice history is " + std::string(attempt == Attempt::Locked ? "busy" : "not usable") +
            " for now; runs are saved when it opens");
    }
    std::lock_guard lock(mutex_);
    unwritable_.store(attempt == Attempt::Unwritable);
    set_state(HistoryState::Waiting);
}

// Opens the database until it works. Only damage (corrupt, not a database, a failed integrity
// check) moves the file aside, and then only once, whole, with its WAL files. Everything else that
// goes wrong (busy, locked, a full or read-only disk, an I/O error, a file that is not ours) keeps
// the runs in memory, logs one line and tries again every retry interval. A database from a
// newer etude ends the attempts for this session. When the store is closing, one last attempt
// is made. True when the database is ready; false when this session cannot have one (the state
// says why).
bool HistoryStore::open_loop(bool initial) {
    const auto started = std::chrono::steady_clock::now();
    bool set_aside_done = false;
    bool waiting_logged = false;
    bool last_attempt = false;
    for (;;) {
        std::string why;
        skip_replay_ = last_attempt; // quitting: no fsynced commit per record, the next start replays
        Attempt attempt = attempt_open(why);
        if (attempt == Attempt::Ready) {
            if (initial) {
                std::lock_guard lock(mutex_);
                stats_.load_ms = std::chrono::duration<double, std::milli>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
            } else if (waiting_logged) {
                log("history: " + path().string() + " is open again; saving the runs held");
            }
            return true;
        }
        if (attempt == Attempt::Newer) {
            log("history: " + path().string() + " is from a newer Etude (" + why +
                "); left as it is, history is not saved this time");
            say("Practice history is from a newer version; not saved this time");
            give_up(true);
            return false;
        }
        if (attempt == Attempt::Foreign) {
            log("history: " + path().string() + " is not a practice history this version can use (" +
                why + "); left as it is, runs are not saved this time");
            say("Practice history file is not one this version can use; runs are not saved this "
                "time");
            give_up(true);
            return false;
        }
        // "May not change it" is said only when it is so: the folder or the file really is not
        // writable for this program. A full disk or the like also fails to create a file, and is
        // not a matter of permissions.
        if (attempt == Attempt::Unwritable && !not_writable_for_app()) {
            attempt = Attempt::Unusable;
        }
        std::string problem = "busy";
        if (attempt == Attempt::Damaged && !set_aside_done && !last_attempt) {
            std::string moved;
            if (set_aside(moved)) {
                set_aside_done = true;
                log("history: " + path().string() + " is damaged (" + why + "); kept as " + moved +
                    ", starting a new one");
                say("Practice history damaged; kept as " + moved + ", starting a new one");
                continue;
            }
            problem = "damaged and could not be moved aside (" + moved + ")";
        } else if (attempt == Attempt::Damaged) {
            problem = "damaged";
        } else if (attempt == Attempt::Unwritable) {
            problem = "not writable";
        } else if (attempt != Attempt::Locked) {
            problem = "not usable";
        }
        if (last_attempt) {
            give_up(false);
            return false;
        }
        if (!waiting_logged) {
            waiting_logged = true;
            tell_waiting(attempt, problem, why);
        }
        failed_attempts_.fetch_add(1);
        // Closing: one more attempt, then give up.
        last_attempt = !sleep_for_retry();
    }
}

// Ends the attempts. `permanent` is a database that will never do (the jobs are dropped); else the
// jobs held go to the journal for the next start.
void HistoryStore::give_up(bool permanent) {
    if (!permanent) {
        keep_held_jobs("could not be opened");
        return;
    }
    {
        std::lock_guard lock(mutex_);
        accepting_ = false;
        queue_.clear();
        announce();
    }
    publish_loaded(HistoryState::Unavailable);
}

namespace {

// "2 practice runs, 1 practice setting and 1 library change", empty when nothing was lost.
std::string loss_text(const JournalHeld& lost) {
    std::vector<std::string> parts;
    const auto count_of = [](std::size_t n, const char* one, const char* many) {
        return std::to_string(n) + " " + (n == 1 ? one : many);
    };
    if (lost.runs > 0) {
        parts.push_back(count_of(lost.runs, "practice run", "practice runs"));
    }
    if (lost.practice > 0) {
        parts.push_back(count_of(lost.practice, "practice setting", "practice settings"));
    }
    if (lost.library > 0) {
        parts.push_back(count_of(lost.library, "library change", "library changes"));
    }
    std::string text;
    for (std::size_t i = 0; i < parts.size(); ++i) {
        text += i == 0 ? "" : i + 1 == parts.size() ? " and " : ", ";
        text += parts[i];
    }
    return text;
}

} // namespace

// The queued jobs as journal records, in order, each with what it holds; the queue keeps only the
// reads, which are answered with nothing at the end.
void HistoryStore::collect_held_jobs(std::vector<JournalEntry>& entries, std::vector<JournalHeld>& held) {
    const std::int64_t now_utc_s = std::chrono::duration_cast<std::chrono::seconds>(
                                       std::chrono::system_clock::now().time_since_epoch())
                                       .count();
    // `items` in records of at most kMaxJournalItems; `make` fills one from a range of them.
    const auto in_chunks = [&](auto& items, auto make) {
        for (std::size_t at = 0; at < items.size(); at += kMaxJournalItems) {
            const std::size_t end = std::min(items.size(), at + kMaxJournalItems);
            entries.emplace_back(make(items.begin() + static_cast<std::ptrdiff_t>(at),
                                      items.begin() + static_cast<std::ptrdiff_t>(end)));
            held.push_back({0, 0, end - at});
        }
    };
    const auto one = [&](JournalEntry entry, JournalHeld count = {0, 0, 1}) { // a record of one item
        entries.push_back(std::move(entry));
        held.push_back(count);
    };
    std::lock_guard lock(mutex_);
    accepting_ = false;
    for (Job& job : queue_) {
        if (job.extra != Job::Extra::None) {
            if (job.extra == Job::Extra::Pin) {
                one(JournalPin{job.run_id, job.pinned, job.profile_id});
            }
            continue;
        }
        if (!job.profiles.empty()) {
            one(JournalProfiles{std::move(job.profiles)});
        } else if (job.delete_profile != 0) {
            one(JournalProfileDelete{job.delete_profile});
        } else if (job.settings_profile != 0) {
            one(JournalProfileSettings{job.settings_profile, std::move(job.settings_text),
                                       job.at_utc_s > 0 ? job.at_utc_s : now_utc_s});
        } else if (job.practice_hash != 0) {
            one(JournalPractice{job.practice_hash, std::move(job.practice),
                                job.at_utc_s > 0 ? job.at_utc_s : now_utc_s, job.profile_id},
                {0, 1, 0});
        } else if (!job.prefs.empty()) {
            in_chunks(job.prefs, [](auto first, auto last) {
                return JournalPrefs{{std::make_move_iterator(first), std::make_move_iterator(last)}};
            });
        } else if (!job.groups.empty()) {
            in_chunks(job.groups, [](auto first, auto last) {
                return JournalGroups{{std::make_move_iterator(first), std::make_move_iterator(last)}};
            });
        } else if (!job.sources.empty()) {
            in_chunks(job.sources, [](auto first, auto last) {
                return JournalOrigins{{std::make_move_iterator(first), std::make_move_iterator(last)}};
            });
        } else if (!job.versions.empty()) {
            in_chunks(job.versions, [](auto first, auto last) {
                return JournalVersions{{std::make_move_iterator(first), std::make_move_iterator(last)}};
            });
        } else {
            finish_ordering(job.events);
            one(JournalRun{std::move(job.record), std::move(job.song_path), std::move(job.events),
                           job.profile_id},
                {1, 0, 0});
        }
    }
    std::erase_if(queue_, [](const Job& job) { return job.extra == Job::Extra::None; });
}

// The jobs still queued when this session gives up on the database. Everything the history thread
// queues is written to the journal in order: the runs, the songs' practice choices and the library's
// own changes (stars and names, groups, sources, versions) and pins. A long job of library changes
// is cut into records of kMaxJournalItems each. What the journal could not keep either is reported
// by take_errors(), as one sentence the player can be told: only what neither the database nor the
// journal has, counted by item (a run, a song's practice choices, one library change each: a song's
// settings, a group edit, a source, an override row, a pin).
void HistoryStore::keep_held_jobs(std::string_view why) {
    std::vector<JournalEntry> entries;
    std::vector<JournalHeld> held;
    collect_held_jobs(entries, held);
    if (entries.empty()) {
        std::lock_guard lock(mutex_);
        announce();
        return;
    }
    const JournalAppend appended = append_journal(journal_path(), entries);
    JournalHeld all;
    JournalHeld lost;
    const auto add = [](JournalHeld& total, const JournalHeld& h) {
        total.runs += h.runs;
        total.practice += h.practice;
        total.library += h.library;
    };
    for (std::size_t i = 0; i < held.size(); ++i) {
        add(all, held[i]);
        if (i >= appended.kept.size() || !appended.kept[i]) {
            add(lost, held[i]);
        }
    }
    const auto counts = [](const JournalHeld& h) {
        return std::to_string(h.runs) + " runs, " + std::to_string(h.practice) +
               " practice settings and " + std::to_string(h.library) + " library changes";
    };
    if (appended.saved == entries.size()) {
        log("history: " + counts(all) + " not written to " + path().string() + " (" +
            std::string(why) + "); kept in " + journal_path().string() + " for the next start");
    } else {
        log("history: " + counts(lost) + " not saved: " + path().string() + " (" +
            std::string(why) + ") and " + journal_path().string() + " (" + appended.why + ")" +
            (appended.saved > 0 ? "; " + std::to_string(appended.saved) + " of " +
                                      std::to_string(entries.size()) + " records were kept"
                                : std::string()));
    }
    const std::string lost_text = loss_text(lost);
    std::lock_guard lock(mutex_);
    if (!lost_text.empty()) {
        errors_.push_back(lost_text + " could not be saved");
    }
    stats_.failures += static_cast<int>(lost.runs);
    announce();
}

// What a finished write adds to the stats and the errors; mutex_ held. `other` is a job that is not
// a run (library, practice, profile, read or pin).
void HistoryStore::note_write_result(const Job& job, bool other, int rc, const std::string& message,
                                     double ms) {
    if (other) {
        if (rc == SQLITE_OK) {
            return;
        }
        if (job.serial != 0) {
            // A read that failed for any other reason is still answered, with nothing.
            answer_nothing(job);
        }
        if (job.delete_profile != 0) {
            deletion_reports_.emplace_back(job.delete_profile, false); // it is still there
        }
        errors_.push_back(job.extra != Job::Extra::None
                              ? "Could not read or pin the run's notes: " + message
                          : is_profile_job(job) ? "Could not save the profile: " + message
                                                : "Could not save the song's details: " + message);
    } else if (rc == SQLITE_OK) {
        ++stats_.writes;
        stats_.last_ms = ms;
        stats_.max_ms = std::max(stats_.max_ms, ms);
        stats_.total_ms += ms;
    } else {
        ++stats_.failures;
        errors_.push_back("Could not save the run: " + message);
    }
}

// Saves each queued run. A run that finds the database busy waits, with the ones after it, and is
// tried again after the retry interval.
void HistoryStore::write_phase() {
    std::unique_lock lock(mutex_);
    for (;;) {
        work_.wait(lock, [this] { return stop_ || !queue_.empty(); });
        if (queue_.empty()) {
            break; // stopped, and everything is saved
        }
        Job job = std::move(queue_.front());
        queue_.pop_front();
        busy_ = true;
        lock.unlock();

        finish_ordering(job.events); // the sort of a run's events is paid here, not by the frame
        const auto started = std::chrono::steady_clock::now();
        const bool prefs = !job.prefs.empty() || !job.groups.empty() || !job.sources.empty() ||
                           !job.versions.empty() || job.practice_hash != 0 ||
                           job.extra != Job::Extra::None || is_profile_job(job);
        const int rc = job.extra != Job::Extra::None   ? run_events_job(job)
                       : is_profile_job(job)           ? write_profile_job(job)
                       : job.practice_hash != 0        ? write_practice(job)
                       : !job.groups.empty()   ? write_groups(job)
                       : !job.prefs.empty()    ? write_prefs(job)
                       : !job.sources.empty()  ? write_sources(job)
                       : !job.versions.empty() ? write_versions(job)
                                               : write_run(job);
        std::string message = rc == SQLITE_OK ? std::string() : db_.error_message();
        if (rc != SQLITE_OK && message == "not an error") {
            message = sqlite3_errstr(rc); // the statement's reset cleared the connection's text
        }
        const double ms =
            std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started)
                .count();

        lock.lock();
        busy_ = false;
        if (sql::classify(rc) == sql::Failure::Damaged) {
            // The file went bad under us. Set it aside as at the start, open a new one, and write
            // this run and the ones queued behind it there.
            queue_.push_front(std::move(job));
            lock.unlock();
            const bool reopened = recover_from_damage(message);
            lock.lock();
            if (!reopened) {
                accepting_ = false;
                announce();
                return;
            }
            set_state(HistoryState::Ready);
            continue;
        }
        if (sql::classify(rc) == sql::Failure::Environment) {
            // A full disk, a read-only file, an I/O error: it may pass. The run is held, the
            // connection (which may have opened read-only) is dropped, and the database is opened
            // again after the retry interval.
            queue_.push_front(std::move(job));
            const bool first = state_.load() != HistoryState::Waiting;
            set_state(HistoryState::Waiting);
            const std::size_t held = queue_.size();
            const bool stopping = stop_;
            lock.unlock();
            if (first) {
                log("history: could not write to " + path().string() + " (" + message + "); " +
                    std::to_string(held) + " runs held in memory, trying again every few seconds");
                say("Practice history cannot be written for now; runs are saved when it can");
            }
            if (stopping) { // the last attempt, made after the wake-up, failed too
                keep_held_jobs("could not be written");
                lock.lock();
                announce();
                return;
            }
            failed_attempts_.fetch_add(1);
            close_database();
            sleep_for_retry();
            const bool reopened = open_loop(false);
            lock.lock();
            if (!reopened) {
                accepting_ = false;
                announce();
                return;
            }
            continue;
        }
        if (sql::classify(rc) == sql::Failure::Locked) {
            queue_.push_front(std::move(job));
            const bool first = state_.load() != HistoryState::Waiting;
            set_state(HistoryState::Waiting);
            const std::size_t held = queue_.size();
            const bool stopping = stop_;
            lock.unlock();
            if (first) {
                log("history: " + path().string() + " is busy (" + message + "); " +
                    std::to_string(held) + " runs held in memory, trying again every few seconds");
            }
            if (stopping) {
                keep_held_jobs("stayed busy");
                lock.lock();
                announce();
                return;
            }
            failed_attempts_.fetch_add(1);
            sleep_for_retry();
            lock.lock();
            continue;
        }
        if (state_.load() == HistoryState::Waiting) {
            set_state(HistoryState::Ready);
            lock.unlock();
            log("history: " + path().string() + " is free again; saving the runs held");
            lock.lock();
        }
        note_write_result(job, prefs, rc, message, ms);
        announce();
    }
    accepting_ = false;
    announce();
}

} // namespace etude::app
