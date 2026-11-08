// SPDX-License-Identifier: GPL-3.0-or-later
// Profiles in the history store (#79): the rows, one profile's settings text, switching the active
// profile, and the loading of every profile's data at start.
#include "history_store.hpp"

#include "history_store_internal.hpp"

#include <algorithm>
#include <array>
#include <chrono>

namespace etude::app {

namespace {

constexpr std::string_view kSelectProfiles =
    "SELECT id, name, colour, created_utc_s FROM profiles ORDER BY id";
constexpr std::string_view kSelectProfileSettings =
    "SELECT profile_id, settings FROM profile_settings ORDER BY profile_id";
constexpr std::string_view kUpsertProfile =
    "INSERT INTO profiles (id, name, is_default, created_utc_s, colour) "
    "VALUES (?1, ?2, ?3, ?4, ?5) ON CONFLICT (id) DO UPDATE SET name = excluded.name, "
    "colour = excluded.colour";
constexpr std::string_view kUpsertProfileSettings =
    "INSERT INTO profile_settings (profile_id, settings, updated_utc_s) VALUES (?1, ?2, ?3) "
    "ON CONFLICT (profile_id) DO UPDATE SET settings = excluded.settings, "
    "updated_utc_s = excluded.updated_utc_s";

std::int64_t now_utc_s() {
    return std::chrono::duration_cast<std::chrono::seconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

const std::string kNoSettings;

} // namespace

bool HistoryStore::prepare_profile_statements() {
    return upsert_profile_.prepare(db_.get(), kUpsertProfile) == SQLITE_OK &&
           upsert_profile_settings_.prepare(db_.get(), kUpsertProfileSettings) == SQLITE_OK;
}

// ---- Frame thread

bool HistoryStore::switch_profile(std::int64_t id) {
    if (!loaded_ || id == active_ || !profiles_.contains(id)) {
        return false;
    }
    left_utc_s_[active_] = now_utc_s();
    other_runs_[active_] = std::move(runs_);
    other_practice_[active_] = std::move(practice_);
    runs_ = std::move(other_runs_[id]);
    practice_ = std::move(other_practice_[id]);
    other_runs_.erase(id);
    other_practice_.erase(id);
    active_ = id;
    return true;
}

void HistoryStore::queue_profile_job(Job&& job) {
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return;
        }
        queue_.push_back(std::move(job));
    }
    work_.notify_one();
}

ProfileResult HistoryStore::add_profile(std::string_view name, Profile* made) {
    if (!loaded_) {
        return ProfileResult::NotReady;
    }
    Profile profile;
    const ProfileResult result = profiles_.add(name, now_utc_s(), &profile);
    if (result != ProfileResult::Ok) {
        return result;
    }
    if (made != nullptr) {
        *made = profile;
    }
    Job job;
    job.profiles.push_back(profile);
    queue_profile_job(std::move(job));
    return result;
}

ProfileResult HistoryStore::rename_profile(std::int64_t id, std::string_view name) {
    if (!loaded_) {
        return ProfileResult::NotReady;
    }
    const ProfileResult result = profiles_.rename(id, name);
    if (result == ProfileResult::Ok) {
        Job job;
        job.profiles.push_back(*profiles_.find(id));
        queue_profile_job(std::move(job));
    }
    return result;
}

ProfileResult HistoryStore::recolour_profile(std::int64_t id, std::uint32_t colour) {
    if (!loaded_) {
        return ProfileResult::NotReady;
    }
    const ProfileResult result = profiles_.recolour(id, colour);
    if (result == ProfileResult::Ok) {
        Job job;
        job.profiles.push_back(*profiles_.find(id));
        queue_profile_job(std::move(job));
    }
    return result;
}

ProfileResult HistoryStore::delete_profile(std::int64_t id) {
    if (!loaded_) {
        return ProfileResult::NotReady;
    }
    const ProfileResult can = profiles_.check_delete(id);
    if (can != ProfileResult::Ok || deleting_.contains(id)) {
        return can;
    }
    Job job;
    job.delete_profile = id;
    {
        std::lock_guard lock(mutex_);
        if (!accepting_) {
            return ProfileResult::NotSaved; // another etude holds the history: nothing would be written
        }
        queue_.push_back(std::move(job));
    }
    deleting_.insert(id);
    work_.notify_one();
    return ProfileResult::Ok;
}

// Poll, mutex_ held: what the history thread did with the queued deletions. A done one changes the
// frame's lists now (the successor of a profile in use is the most recently used of the others: the
// later of its newest run and when it was last switched away from; the rest is core's rule); a failed
// one leaves everything as it was.
void HistoryStore::apply_deletions(HistoryPoll& result) {
    for (const auto& [id, done] : std::exchange(deletion_reports_, {})) {
        if (deleting_.erase(id) == 0) {
            continue; // a deletion replayed from the journal at start: nobody is waiting for it
        }
        ProfileDeleted out;
        out.id = id;
        out.active = active_;
        if (const Profile* gone = profiles_.find(id)) {
            out.name = gone->name;
        }
        if (done && profiles_.contains(id)) {
            if (id == active_) {
                std::vector<ProfileUse> uses;
                for (const Profile& p : profiles_.all()) {
                    std::int64_t at = left_utc_s_.contains(p.id) ? left_utc_s_[p.id] : 0;
                    const auto it = other_runs_.find(p.id);
                    if (it != other_runs_.end() && !it->second.empty()) {
                        at = std::max(at, it->second.back().start_utc_s);
                    }
                    uses.push_back({p.id, at});
                }
                switch_profile(profiles_.successor(id, uses));
                out.switched = true;
                out.active = active_;
            }
            other_runs_.erase(id);
            other_practice_.erase(id);
            profile_settings_.erase(id);
            left_utc_s_.erase(id);
            profiles_.remove(id);
            out.done = true;
        }
        result.deleted.push_back(std::move(out));
    }
}

const std::string& HistoryStore::profile_settings(std::int64_t id) const {
    const auto it = profile_settings_.find(id);
    return it == profile_settings_.end() ? kNoSettings : it->second;
}

void HistoryStore::set_profile_settings(std::int64_t id, std::string text) {
    if (id <= 0 || (loaded_ && !profiles_.contains(id))) {
        return;
    }
    std::string& held = profile_settings_[id];
    if (held == text) {
        return;
    }
    held = text;
    Job job;
    job.settings_profile = id;
    job.settings_text = std::move(text);
    job.at_utc_s = now_utc_s();
    queue_profile_job(std::move(job));
}

std::size_t HistoryStore::run_count_all() const {
    std::size_t total = runs_.size();
    for (const auto& [id, list] : other_runs_) {
        total += list.size();
    }
    return total;
}

std::vector<ProfileRuns> HistoryStore::household_runs() const {
    std::vector<ProfileRuns> out;
    for (const Profile& profile : profiles_.all()) {
        if (profile.id == active_) {
            out.push_back({profile.id, runs_});
            continue;
        }
        const auto it = other_runs_.find(profile.id);
        out.push_back({profile.id, it == other_runs_.end() ? std::span<const RunRecord>()
                                                           : std::span<const RunRecord>(it->second)});
    }
    return out;
}

// ---- History thread

bool HistoryStore::profile_known(std::int64_t id) const {
    return std::find(thread_profile_ids_.begin(), thread_profile_ids_.end(), id) !=
           thread_profile_ids_.end();
}

void HistoryStore::report_deletion(std::int64_t id, bool done) {
    std::lock_guard lock(mutex_);
    deletion_reports_.emplace_back(id, done);
}

std::int64_t HistoryStore::stored_profile(std::int64_t id) const {
    if (std::find(thread_profile_ids_.begin(), thread_profile_ids_.end(), id) !=
        thread_profile_ids_.end()) {
        return id;
    }
    return kDefaultProfileId;
}

HistoryStore::Attempt HistoryStore::load_profiles(std::string& why) {
    thread_profile_rows_.clear();
    thread_profile_settings_.clear();
    thread_profile_ids_.clear();
    sql::Statement select;
    int rc = select.prepare(db_.get(), kSelectProfiles);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    while ((rc = select.step()) == SQLITE_ROW) {
        Profile p;
        p.id = select.column_int(0);
        p.name = select.column_text(1);
        p.colour = static_cast<std::uint32_t>(select.column_int(2) & 0xFFFFFF);
        p.created_utc_s = select.column_int(3);
        thread_profile_ids_.push_back(p.id);
        thread_profile_rows_.push_back(std::move(p));
    }
    if (rc != SQLITE_DONE) {
        return fail(rc, why);
    }
    sql::Statement settings;
    rc = settings.prepare(db_.get(), kSelectProfileSettings);
    if (rc != SQLITE_OK) {
        return fail(rc, why);
    }
    while ((rc = settings.step()) == SQLITE_ROW) {
        thread_profile_settings_.emplace_back(settings.column_int(0), settings.column_text(1));
    }
    return rc == SQLITE_DONE ? Attempt::Ready : fail(rc, why);
}

// The rows that belong to one profile, children before parents; every other table is the device's.
// (The notes and measures of a run go with the run: ON DELETE CASCADE.)
constexpr std::array<std::string_view, 9> kDeleteProfileRows{
    "DELETE FROM runs WHERE profile_id = ?1",
    "DELETE FROM song_practice_parts WHERE profile_id = ?1",
    "DELETE FROM song_practice WHERE profile_id = ?1",
    "DELETE FROM song_group_members WHERE profile_id = ?1",
    "DELETE FROM song_groups WHERE profile_id = ?1",
    "DELETE FROM version_overrides WHERE profile_id = ?1",
    "DELETE FROM song_settings WHERE profile_id = ?1",
    "DELETE FROM profile_settings WHERE profile_id = ?1",
    "DELETE FROM profiles WHERE id = ?1"};

// All of the profile's rows in one transaction: any failure puts every one of them back. The rule of
// ProfileSet::check_delete holds here too, whatever asks (a journal replayed at start): the first
// profile, the only one and one the database lacks are never deleted, and the answer is "not done".
// The library's rows (song settings, groups, versions) are stored under the first profile today, so
// these statements find none of them for any other; they stay for the day the library is per profile.
int HistoryStore::delete_profile_rows(std::int64_t id) {
    if (id == kDefaultProfileId || !profile_known(id) || thread_profile_ids_.size() <= 1) {
        log("history: a deletion of profile " + std::to_string(id) + " was refused; skipped");
        report_deletion(id, false);
        return SQLITE_OK;
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = SQLITE_OK;
    for (const std::string_view sql : kDeleteProfileRows) {
        sql::Statement one;
        rc = one.prepare(db_.get(), sql);
        if (rc == SQLITE_OK) {
            one.bind(1, id);
            rc = one.step();
            rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        }
        if (rc != SQLITE_OK) {
            break;
        }
    }
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
        return rc;
    }
    thread_profile_ids_.erase(
        std::remove(thread_profile_ids_.begin(), thread_profile_ids_.end(), id),
        thread_profile_ids_.end());
    report_deletion(id, true);
    return rc;
}

// Profile rows, one profile's settings (one statement each), or a profile's deletion.
int HistoryStore::write_profile_job(const Job& job) {
    if (job.delete_profile != 0) {
        return delete_profile_rows(job.delete_profile);
    }
    int rc = begin_.step();
    begin_.reset();
    if (rc != SQLITE_DONE) {
        return rc;
    }
    rc = SQLITE_OK;
    for (const Profile& p : job.profiles) {
        upsert_profile_.bind(1, p.id);
        upsert_profile_.bind(2, p.name);
        upsert_profile_.bind(3, std::int64_t{p.is_default() ? 1 : 0});
        upsert_profile_.bind(4, p.created_utc_s);
        upsert_profile_.bind(5, std::int64_t{p.colour});
        rc = upsert_profile_.step();
        upsert_profile_.reset();
        if (rc != SQLITE_DONE) {
            break;
        }
        rc = SQLITE_OK;
    }
    // The text of a profile the database does not hold (one that was never saved) is dropped, not
    // a foreign-key failure that would hold every job behind it.
    const bool known = stored_profile(job.settings_profile) == job.settings_profile ||
                       std::any_of(job.profiles.begin(), job.profiles.end(), [&](const Profile& p) {
                           return p.id == job.settings_profile;
                       });
    if (rc == SQLITE_OK && job.settings_profile != 0 && known) {
        upsert_profile_settings_.bind(1, job.settings_profile);
        upsert_profile_settings_.bind(2, job.settings_text);
        upsert_profile_settings_.bind(3, job.at_utc_s);
        rc = upsert_profile_settings_.step();
        upsert_profile_settings_.reset();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
    }
    if (rc == SQLITE_OK) {
        rc = commit_.step();
        rc = rc == SQLITE_DONE ? SQLITE_OK : rc;
        commit_.reset();
    }
    if (rc != SQLITE_OK) {
        rollback_.step();
        rollback_.reset();
        return rc;
    }
    for (const Profile& p : job.profiles) {
        if (std::find(thread_profile_ids_.begin(), thread_profile_ids_.end(), p.id) ==
            thread_profile_ids_.end()) {
            thread_profile_ids_.push_back(p.id);
        }
    }
    return rc;
}

} // namespace etude::app
