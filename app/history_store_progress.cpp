// SPDX-License-Identifier: GPL-3.0-or-later
// The Progress page's request to the history thread, and the thread's answer.
#include "history_store_internal.hpp"

#include "etude/session_state.hpp"

#include <algorithm>

namespace etude::app {

std::uint64_t HistoryStore::request_progress(ProgressRequest request) {
    Job job;
    job.extra = Job::Extra::ReadProgress;
    job.profile_id = request.profile_id > 0 ? request.profile_id : active_;
    job.progress = std::move(request);
    for (const Profile& p : profiles_.all()) {
        const auto text = profile_settings_.find(p.id);
        const int goal = text == profile_settings_.end() ? -1 : goal_minutes_of_settings(text->second);
        job.progress.people.push_back({p.id, p.created_utc_s, goal >= 0 ? goal : AppSettings{}.daily_goal_minutes});
    }
    std::lock_guard lock(mutex_);
    if (!accepting_) {
        return 0;
    }
    job.serial = ++read_serial_;
    queue_.push_back(std::move(job));
    work_.notify_one();
    return read_serial_;
}

std::vector<ProgressAnswer> HistoryStore::take_progress() {
    std::lock_guard lock(mutex_);
    return std::exchange(progress_answers_, {});
}

// History thread. Every profile's runs are read from the open database in their own statements (no
// transaction held), grouped by profile in the order the profiles were made, and handed to core.
// A failed read is held or retried like any read job: the answer comes when it succeeds, or empty
// when the store ends first.
int HistoryStore::run_progress_job(const Job& job) {
    std::vector<ProfiledRun> stored;
    if (const int rc = read_all_runs(stored); rc != SQLITE_OK) {
        return rc;
    }
    std::vector<std::int64_t> ids = thread_profile_ids_;
    if (std::find(ids.begin(), ids.end(), kDefaultProfileId) == ids.end()) {
        ids.push_back(kDefaultProfileId);
    }
    std::sort(ids.begin(), ids.end(), [](std::int64_t a, std::int64_t b) { // default first
        return std::pair(a != kDefaultProfileId, a) < std::pair(b != kDefaultProfileId, b);
    });
    std::vector<std::vector<RunRecord>> runs(ids.size());
    for (ProfiledRun& one : stored) {
        // A run of a profile the table lacks is the default profile's, as everywhere else.
        const auto it = std::find(ids.begin(), ids.end(), one.profile_id);
        const std::size_t at = it == ids.end() ? 0 : static_cast<std::size_t>(it - ids.begin());
        runs[at].push_back(std::move(one.run));
    }
    std::vector<ProfileRuns> household;
    household.reserve(ids.size());
    for (std::size_t i = 0; i < ids.size(); ++i) {
        ProfileRuns one{ids[i], runs[i]};
        for (const ProgressRequest::Person& person : job.progress.people) {
            if (person.id == ids[i]) {
                one.created_utc_s = person.created_utc_s;
                one.goal_minutes = person.goal_minutes;
            }
        }
        household.push_back(one);
    }
    auto view = std::make_shared<const ProgressView>(build_progress_view(
        household, stored_profile(job.profile_id), job.progress.today, job.progress.goal_minutes,
        MedalOptions{job.progress.utc_offset_s, job.progress.pieces.get()}));
    std::lock_guard lock(mutex_);
    progress_answers_.push_back({job.serial, std::move(view)});
    return SQLITE_OK;
}

} // namespace etude::app
