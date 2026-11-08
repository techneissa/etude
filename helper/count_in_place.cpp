// SPDX-License-Identifier: GPL-3.0-or-later
#include "settle.hpp"

#include "handoff_files.hpp"
#include "layout.hpp"
#include "state_store.hpp"

#include <algorithm>

namespace etude::helper {

namespace {

bool save(const Context& c, const PendingUpdate& state) {
    std::string error;
    if (!save_pending(c, state, error)) {
        note("the state could not be saved: " + error);
        return false;
    }
    return true;
}

// The data of a going back is for a sandboxed run; this start gets none, so it is a failed attempt, and
// the app does not start on the new version's data.
int count_attempt(const Context& c, PendingUpdate s) {
    const std::string why = "the sandbox for putting the data back could not be started";
    s.restores = std::min(s.restores + 1, 100);
    s.data_error = why;
    save(c, s);
    const UpdateStatus said = read_status(c);
    write_status(c, "failed", said.version.empty() ? s.to : said.version, said.from.empty() ? s.from : said.from,
                 said.reason.empty() ? s.reason : said.reason, why);
    note("a going back is recorded and its data is not put back: the sandbox could not be started");
    return kRestoreUnfinished;
}

} // namespace

int count_in_place(const Context& c) {
    const auto cannot_settle = [&](const std::string& why) {
        note(why);
        const PendingUpdate recorded = peek_pending(c);
        return recorded.phase == UpdatePhase::RollingBack ? count_attempt(c, recorded) : 0;
    };
    if (const std::string layout = check_layout(c); !layout.empty()) {
        return cannot_settle("not settling an update in " + c.conf.prefix + ": " + layout);
    }
    std::string error;
    StateLock lock(c, "lock", true, error, kCountLockWaitS);
    if (!lock.held()) {
        return cannot_settle(error);
    }
    // Peeked at, not loaded: loading sets a record that cannot be read aside, and working out what it
    // meant needs the data folder, so it is left exactly as it is for a sandboxed run.
    bool readable = true;
    PendingUpdate s = peek_pending(c, &readable);
    if (!readable) {
        note("the update record could not be read; it is left for a sandboxed run");
        return 0;
    }
    if (s.phase == UpdatePhase::RollingBack) {
        return count_attempt(c, s);
    }
    if (s.phase == UpdatePhase::Switching) {
        reconcile_switch(c, s);
    }
    if (s.phase != UpdatePhase::Pending) {
        return 0;
    }
    const Fd update = open_update_dir(c, error);
    if (update.ok() && ready_for(c, update.get(), s.to)) {
        settle_ok(c, s);
        return 0;
    }
    ++s.starts;
    const bool counted = save(c, s);
    note("start " + std::to_string(s.starts) + " of " + s.to + " without ready");
    if (counted && s.starts <= c.conf.max_starts) {
        return 0;
    }
    // The trial has failed (or its start cannot be counted): the links go back, the data does not.
    const std::string why = counted ? s.to + " did not reach its first frame in " + std::to_string(s.starts - 1) +
                                          " starts"
                                    : "the start of " + s.to + " could not be counted";
    const Service service(c);
    roll_back(c, service, s, why, RollBackMode::LinksOnly);
    return count_attempt(c, peek_pending(c));
}

} // namespace etude::helper

namespace etude::helper {

int settle_after_crash_in_place(const Context& c) {
    if (const std::string layout = check_layout(c); !layout.empty()) {
        note("not settling an update in " + c.conf.prefix + ": " + layout);
        return 0;
    }
    std::string error;
    StateLock lock(c, "lock", true, error, kCountLockWaitS);
    if (!lock.held()) {
        note(error);
        return 0;
    }
    bool readable = true;
    PendingUpdate s = peek_pending(c, &readable);
    if (!readable) {
        return 0;
    }
    if (s.phase == UpdatePhase::Switching) {
        reconcile_switch(c, s);
    }
    if (s.phase != UpdatePhase::Pending) {
        return 0;
    }
    const Fd update = open_update_dir(c, error);
    if (update.ok() && ready_for(c, update.get(), s.to)) {
        settle_ok(c, s);
        return 0;
    }
    const Service service(c);
    roll_back(c, service, s, s.to + " closed unexpectedly before it said ready", RollBackMode::LinksOnly);
    return 0;
}

} // namespace etude::helper
