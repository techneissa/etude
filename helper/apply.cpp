// SPDX-License-Identifier: GPL-3.0-or-later
#include "apply.hpp"

#include "backup.hpp"
#include "handoff_files.hpp"
#include "install.hpp"
#include "interrupt.hpp"
#include "layout.hpp"
#include "service.hpp"
#include "settle.hpp"
#include "state_store.hpp"

#include <sys/stat.h>

#include <cstdint>
#include <memory>

namespace etude::helper {

namespace {

constexpr const char* kNoServiceToTry =
    "the app is not running from its own service (it is stopped, or the crash screen is up), so a new version could not "
    "be tried and rolled back: start it normally and update again";

int fail(const Context& c, const std::string& version, const std::string& from,
         const std::string& reason) {
    const std::string said = was_interrupted() && reason.find("interrupted") == std::string::npos
                                 ? reason + " (the helper was told to stop)"
                                 : reason;
    note("failed: " + said);
    write_status(c, "failed", version, from, said);
    return 1;
}

// Starts the service again when an update gives up after it was stopped, unless it was handed on.
class RestartGuard {
  public:
    RestartGuard(const Service& service, std::string unit) : service_(service), unit_(std::move(unit)) {}
    ~RestartGuard() {
        if (armed_ && !unit_.empty()) {
            note("starting " + unit_ + " again");
            service_.start(unit_);
        }
    }
    RestartGuard(const RestartGuard&) = delete;
    RestartGuard& operator=(const RestartGuard&) = delete;
    void arm() { armed_ = true; }
    void dismiss() { armed_ = false; }

  private:
    const Service& service_;
    std::string unit_;
    bool armed_ = false;
};

bool is_folder(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// Backs up the app's data as the app's user; `backed_up` says whether there was a data folder.
bool back_up(const Context& c, const Service& service, const std::string& unit, const std::string& running,
             bool& backed_up, std::string& error) {
    backed_up = false;
    const std::string folder = data_folder_for(c, service, unit);
    if (folder.empty()) {
        error = "the app's data folder could not be found";
        return false;
    }
    if (!run_as_user(c.user, c.data_timeout_s, [&] { return backup_data(folder, running); }, error)) {
        return false;
    }
    backed_up = is_folder(folder);
    return true;
}

// Puts `current` and `previous` back as they were before the switch. True when `current` is confirmed
// on the old version and the record is cleared. Otherwise the record is kept as RollingBack (the
// links, the data and the new version are then settled by the next `count`, before the app starts)
// and false is returned: the new version may be the one `current` still points at.
bool unswitch(const Context& c, const PendingUpdate& trial, const std::string& reason) {
    std::string ignore;
    for (int attempt = 0; attempt < 2 && link_version(c, "current") != trial.from; ++attempt) {
        point_link(c, "current", trial.from, ignore);
    }
    const bool back = link_version(c, "current") == trial.from;
    if (back) {
        point_link(c, "previous", trial.previous, ignore);
        save_pending(c, PendingUpdate{}, ignore);
        return true;
    }
    PendingUpdate keep = trial;
    keep.phase = UpdatePhase::RollingBack;
    keep.reason = reason;
    save_pending(c, keep, ignore);
    note("`current` could not be put back on " + trial.from + ": the going back is recorded for the next start");
    return false;
}

enum class SwitchResult : std::uint8_t {
    Ok,
    Undone, // the links are as they were: the new version may go
    Stuck,  // `current` could not be put back: a RollingBack record is kept, the new version stays
};

// Points the links at the new version; the state says Switching first, Pending after, so a power cut
// in the middle is told apart by `current`. When a step fails the links are put back, and only a
// confirmed `current` lets the caller remove the new version.
SwitchResult switch_links(const Context& c, PendingUpdate& trial, int update_fd, std::string& error) {
    if (!save_pending(c, trial, error)) {
        return SwitchResult::Undone;
    }
    clear_ready(update_fd);
    if (!point_link(c, "current", trial.to, error)) {
        return SwitchResult::Undone; // a rename that failed changed nothing
    }
    if (!point_link(c, "previous", trial.from, error)) {
        return unswitch(c, trial, error) ? SwitchResult::Undone : SwitchResult::Stuck;
    }
    trial.phase = UpdatePhase::Pending;
    if (!save_pending(c, trial, error)) {
        return unswitch(c, trial, error) ? SwitchResult::Undone : SwitchResult::Stuck;
    }
    return SwitchResult::Ok;
}

// Whether an earlier update is in the way: "" when none. A record that was damaged is worked out, and
// a going back that is recorded is carried out here (the new request is refused either way: ask again).
std::string earlier_update_in_the_way(const Context& c) {
    const Service service(c);
    PendingUpdate s;
    bool recovered = false;
    {
        std::string error;
        StateLock lock(c, "lock", true, error);
        if (!lock.held()) {
            return "the update record could not be locked";
        }
        s = load_state_checked(c, service, error, &recovered);
    }
    if (s.phase == UpdatePhase::None) {
        return {};
    }
    if (recovered || s.phase == UpdatePhase::RollingBack) {
        go_back(c, service, s, s.reason, false);
        return recovered ? "the record of an earlier update was damaged, and that update was gone back from"
                         : "an earlier update was being gone back from, and that is done";
    }
    return "an earlier update is still being settled";
}

} // namespace

// Stop the app, back its data up, switch the links, start it and wait for it: from here an error
// puts things back (the guard starts the old app again) and says so. The helper being told to stop
// (SIGTERM) is read between the steps and ends the same way as any other error.
int install_staged(const InstallJob& j) {
    const Context& c = j.c;
    const Service service(c);
    const std::string unit = service.active_unit();
    // The stop, the backup and the start are the helper's own doing: the crash screen does not start
    // for the failure a stop that had to be killed leaves behind. Declared before the guard, so the
    // guard's start still comes under it.
    std::unique_ptr<ActingScope> acting;
    if (!unit.empty()) {
        acting = std::make_unique<ActingScope>(c, kActingLockWaitS);
    }
    RestartGuard guard(service, unit);
    std::string error;
    const auto give_up = [&](const std::string& reason) {
        discard_version(c, j.version);
        return fail(c, j.version, j.running, reason);
    };
    if (unit.empty()) {
        return give_up(kNoServiceToTry); // nothing to try the new version under
    }
    if (!unit.empty()) {
        guard.arm(); // a stop that did not finish leaves the app to be started again
        if (!service.stop(unit, error)) {
            return give_up("the app could not be stopped: " + error);
        }
    }
    if (consume_interrupt()) {
        return give_up("the update was interrupted before the data was backed up");
    }
    bool backed_up = false;
    if (!back_up(c, service, unit.empty() ? "etude.service" : unit, j.running, backed_up, error)) {
        return give_up("your data could not be backed up: " + error);
    }
    if (consume_interrupt()) {
        return give_up("the update was interrupted after the data was backed up");
    }
    PendingUpdate trial;
    trial.phase = UpdatePhase::Switching;
    trial.from = j.running;
    trial.to = j.version;
    trial.previous = j.previous;
    trial.unit = unit;
    trial.backed_up = backed_up;
    switch (switch_links(c, trial, j.update_fd, error)) {
    case SwitchResult::Ok:
        break;
    case SwitchResult::Undone: {
        // The links are as they were, so the new version may go.
        std::string ignore;
        save_pending(c, PendingUpdate{}, ignore);
        return give_up("the new version could not be switched to: " + error);
    }
    case SwitchResult::Stuck:
        // `current` is not confirmed back: the new version stays, the record says RollingBack, and the
        // app's next start (the guard starts it) finishes the going back before it runs.
        return fail(c, j.version, j.running,
                    "the new version could not be switched to (" + error +
                        ") and the old one could not be put back: it is put back at the next start");
    }
    if (consume_interrupt()) {
        const std::string reason = "the update was interrupted before the new version started";
        if (!unswitch(c, trial, reason)) {
            return fail(c, j.version, j.running, reason + ", and the old version is put back at the next start");
        }
        return give_up(reason);
    }
    write_status(c, "applying", j.version, j.running, "");
    guard.dismiss();
    if (!service.start(unit)) {
        note(unit + " did not start; the trial goes on and counts it");
    }
    acting.reset(); // from here the helper only watches: a crash is a crash
    return watch_trial(c);
}

int apply_update(const Context& c) {
    std::string error;
    StateLock run(c, "run.lock", true, error);
    if (!run.held()) {
        note(error);
        return 1;
    }
    const Fd update = open_update_dir(c, error);
    if (!update.ok()) {
        note("no update folder: " + error);
        return 1;
    }
    // The request is taken (and so removed) first: whatever follows, it cannot start another run, so
    // the path unit does not loop on a request that is refused.
    const std::string version = take_request(c, update.get(), error);
    if (const std::string layout = check_layout(c); !layout.empty()) {
        note("refusing to work in " + c.conf.prefix + ": " + layout);
        return 1; // nothing is written under a prefix that cannot be trusted, the status included
    }
    clean_leftovers(c);
    const std::string running = link_version(c, "current");
    if (version.empty()) {
        if (error.empty()) {
            note("no request");
            return 0;
        }
        return fail(c, "", running, error);
    }
    note("request: update " + running + " to " + version);
    if (running.empty() || !is_folder(c.version_dir(running))) {
        return fail(c, version, running, "this install has no versions/ layout (run setup.sh)");
    }
    if (const std::string in_the_way = earlier_update_in_the_way(c); !in_the_way.empty()) {
        return fail(c, version, running, in_the_way);
    }
    const std::string previous = link_version(c, "previous");
    if (version == running || version == previous) {
        return fail(c, version, running, version + " is already installed");
    }
    // A new version is only ever tried under the app's own service, which counts its starts and goes back.
    // With none running (stopped, crashed, or the crash screen instead) there is no trial to run: the
    // switch would be final, so the update is refused.
    if (Service(c).active_unit().empty()) {
        return fail(c, version, running, kNoServiceToTry);
    }
    write_status(c, "applying", version, running, "");
    const StageResult staged = stage_release(c, update.get(), version, running);
    if (!staged.ok) {
        return fail(c, version, running, staged.error);
    }
    return install_staged(InstallJob{c, update.get(), version, running, previous});
}

} // namespace etude::helper
