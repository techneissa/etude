// SPDX-License-Identifier: GPL-3.0-or-later
#include "settle.hpp"

#include "backup.hpp"
#include "handoff_files.hpp"
#include "install.hpp"
#include "interrupt.hpp"
#include "layout.hpp"
#include "safe_io.hpp"
#include "state_store.hpp"

#include "etude/semver.hpp"

#include <sys/stat.h>

#include <algorithm>
#include <chrono>
#include <memory>
#include <thread>

namespace etude::helper {

namespace {

bool save_or_note(const Context& c, const PendingUpdate& state) {
    std::string error;
    if (!save_pending(c, state, error)) {
        note("the state could not be saved: " + error);
        return false;
    }
    return true;
}

bool is_folder(const std::string& path) {
    struct stat st{};
    return ::stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

std::string went_back_text(const PendingUpdate& s, const std::string& reason, const std::string& data) {
    std::string text = reason + "; went back to " + s.from;
    if (!data.empty()) {
        text += ". Couldn't put your data back: " + data;
    }
    return text;
}

// The data of the old version, put back as the app's user. "" when it is, or when there was none.
std::string restore_as_app(const Context& c, const Service& service, const PendingUpdate& s) {
    if (!s.backed_up) {
        return {};
    }
    const std::string folder = data_folder_for(c, service, s.unit);
    if (folder.empty()) {
        return "the data folder could not be found";
    }
    std::string error;
    return run_as_user(c.user, c.data_timeout_s, [&] { return restore_data(folder, s.from); }, error)
               ? std::string()
               : error;
}

// The newest version folder older than `from` that is neither `from` nor `to` and has a program in
// it: what `previous` was pointing at before the update (versions are kept until a trial is
// confirmed), or the newest version that ran before the one in use.
std::string earlier_version(const Context& c, const std::string& from, const std::string& to) {
    std::string best;
    for (const std::string& v : list_versions(c)) {
        struct stat program{};
        if (v == from || v == to || compare_versions(v, from) != VersionOrder::Older ||
            ::stat((c.version_dir(v) + "/bin/etude").c_str(), &program) != 0 || !S_ISREG(program.st_mode)) {
            continue;
        }
        if (best.empty() || compare_versions(v, best) == VersionOrder::Newer) {
            best = v;
        }
    }
    return best;
}

// The record of an update could not be read. What was going on is worked out from what is left: a
// `current` that has not said ready is a version on trial, so it is gone back from (a missing record
// is no reason to run a version that never proved itself), to `previous` or, when that link is gone or
// useless, to the newest older version in versions/; a version that has said ready is the one in use.
// With nothing to go back to the failure is reported and `current` keeps running.
PendingUpdate recover_damaged_state(const Context& c, const Service& service) {
    const std::string to = link_version(c, "current");
    std::string from = link_version(c, "previous");
    std::string error;
    const Fd update = open_update_dir(c, error);
    const bool proven = update.ok() && !to.empty() && ready_for(c, update.get(), to);
    if (to.empty() || proven) {
        note("the update record was damaged; nothing shows an update on trial, so nothing is undone");
        return {};
    }
    if (from.empty() || to == from || !is_folder(c.version_dir(from))) {
        from = earlier_version(c, to, to);
        if (from.empty()) {
            note("the update record was damaged, " + to + " has not said ready and there is no earlier version");
            write_status(c, "failed", to, "",
                         "the record of the update could not be read, and " + to +
                             " had not said ready; no earlier version is left to go back to, so it keeps running");
            return {};
        }
    }
    PendingUpdate s;
    s.phase = UpdatePhase::RollingBack;
    s.from = from;
    s.to = to;
    s.previous = earlier_version(c, from, to);
    s.unit = service.active_unit(); // the kiosk's own unit: a labwc install is stopped, and its data found, as itself
    if (s.unit.empty()) {
        s.unit = "etude.service";
    }
    s.reason = "the record of the update could not be read, and " + to + " had not said ready";
    const std::string folder = data_folder_for(c, service, s.unit);
    s.backed_up = !folder.empty() && is_folder(folder + "/update-backup/" + from);
    return s;
}

} // namespace

// An interrupted switch: `current` was pointed at the new version or it was not.
void reconcile_switch(const Context& c, PendingUpdate& s) {
    std::string error;
    if (link_version(c, "current") == s.to) {
        point_link(c, "previous", s.from, error);
        s.phase = UpdatePhase::Pending;
        save_or_note(c, s);
        return;
    }
    note("an update to " + s.to + " was cut short before it switched; it is dropped");
    discard_version(c, s.to);
    s = PendingUpdate{};
    save_or_note(c, s);
}

bool roll_back(const Context& c, const Service& service, PendingUpdate s, const std::string& reason,
               RollBackMode mode) {
    s.phase = UpdatePhase::RollingBack;
    s.reason = reason;
    save_or_note(c, s);
    std::string error;
    // A going back that cannot be done (the old version is gone, a link cannot be written) is a failed
    // attempt like a data restore that fails: the app does not start on it, and the crash screen shows
    // why (the status's `data`) and offers to try again.
    const auto cannot = [&](const std::string& why, const std::string& status_text) {
        note("cannot go back: " + why);
        s.restores = std::min(s.restores + 1, 100);
        s.data_error = why;
        save_or_note(c, s);
        write_status(c, "failed", s.to, s.from, status_text, why);
        return false;
    };
    if (!is_folder(c.version_dir(s.from))) {
        return cannot("versions/" + s.from + " is gone", reason + "; and the old version " + s.from + " is gone");
    }
    if (!point_link(c, "current", s.from, error) || !point_link(c, "previous", s.previous, error)) {
        return cannot("the links could not be put back: " + error, reason + "; and the links could not be put back");
    }
    if (mode == RollBackMode::LinksOnly) {
        // The data is put back by the next `count`, before the app starts; the record says so.
        write_status(c, "failed", s.to, s.from,
                     reason + "; " + s.from + " is back in place and your data is put back before it starts");
        note("links are back on " + s.from + "; the data is left for the next start");
        return false;
    }
    const std::string data = restore_as_app(c, service, s);
    if (!data.empty()) {
        note("the data of " + s.from + " could not be restored: " + data);
    }
    std::string ignore;
    Fd versions = open_directory(c.versions_dir(), ignore);
    if (versions.ok()) {
        remove_tree_at(versions.get(), s.to, ignore);
    }
    const Fd update = open_update_dir(c, ignore);
    if (update.ok()) {
        clear_ready(update.get());
    }
    prune_versions(c, s.from, s.previous);
    write_status(c, "rolled-back", s.to, s.from, went_back_text(s, reason, data), data);
    if (!data.empty()) {
        // Stays RollingBack: the next start's `count` restores the data again (the player's "Try again"),
        // and the app does not start until it has.
        s.restores = std::min(s.restores + 1, 100);
        s.data_error = data;
        save_or_note(c, s);
        return false;
    }
    note("went back to " + s.from);
    save_or_note(c, PendingUpdate{});
    return true;
}

// The update on trial is settled as good; the caller holds the state lock.
void settle_ok(const Context& c, const PendingUpdate& done) {
    save_or_note(c, PendingUpdate{});
    write_status(c, "ok", done.to, done.from, "");
    prune_versions(c, done.to, done.from);
    note("updated " + done.from + " to " + done.to);
}

namespace {

// The state under the lock. `locked` says whether the lock was had; without it the record is only
// peeked at (a stop that cut the wait short, or any trouble with the lock file), so the watch still
// knows what it is watching.
PendingUpdate locked_state(const Context& c, const Service& service, bool& locked) {
    std::string error;
    StateLock lock(c, "lock", true, error);
    locked = lock.held();
    if (!locked) {
        note(error);
        return peek_pending(c);
    }
    const PendingUpdate s = load_state_checked(c, service, error);
    if (!error.empty()) {
        note(error);
    }
    return s;
}

} // namespace

PendingUpdate load_state_checked(const Context& c, const Service& service, std::string& error,
                                 bool* recovered) {
    bool damaged = false;
    PendingUpdate s = load_pending(c, error, &damaged);
    if (recovered != nullptr) {
        *recovered = damaged;
    }
    if (damaged) {
        s = recover_damaged_state(c, service);
        if (s.phase != UpdatePhase::None) {
            save_or_note(c, s);
        }
    }
    return s;
}

int go_back(const Context& c, const Service& service, const PendingUpdate& trial, const std::string& why,
            bool hurry, bool crashed) {
    std::string error;
    bool killed = false;
    bool stopped = true;
    // The stop, the kill and the start below are the helper's own doing: the crash screen's unit does
    // not start for the failure they can leave behind (`idle`). A crash needs none of it, and the crash
    // screen is wanted.
    std::unique_ptr<ActingScope> acting;
    if (!crashed) {
        acting = std::make_unique<ActingScope>(c, kActingLockWaitS);
    }
    if (!trial.unit.empty() && !crashed) {
        stopped = service.stop_hard(trial.unit, killed, error);
        if (!stopped) {
            note("the app would not stop, not even killed: " + error);
        }
    }
    {
        StateLock lock(c, "lock", true, error);
        PendingUpdate s = lock.held() ? load_state_checked(c, service, error) : PendingUpdate{};
        if (s.phase == UpdatePhase::Pending || s.phase == UpdatePhase::RollingBack) {
            std::string reason = s.phase == UpdatePhase::RollingBack && !s.reason.empty() ? s.reason : why;
            if (!stopped) {
                reason += "; the app could not be stopped (" + error + ")";
            } else if (killed) {
                reason += "; the app had to be killed to stop it";
            }
            const bool all_here = stopped && !hurry;
            roll_back(c, service, s, reason, all_here ? RollBackMode::Full : RollBackMode::LinksOnly);
        } else if (!lock.held()) {
            note("the update record could not be locked: " + error);
        }
    }
    // Whatever happened above, something is started: now, or as a queued job the helper does not
    // have to outlive (the app's start runs `count` first, which finishes the data restore). After a
    // crash it is the crash screen, where the player chooses.
    bool started = true;
    bool screen = false;
    if (!trial.unit.empty()) {
        started = false;
        for (int attempt = 0; attempt < 2 && !started; ++attempt) {
            if (crashed) {
                started = service.start_crash_screen(trial.unit);
            } else {
                started = hurry || !stopped ? service.start_queued(trial.unit) : service.start(trial.unit);
            }
            if (!started) {
                // A restore that is still unfinished ends every start 75: another try would only count
                // another attempt, and the player's "Try again" is the next one.
                const bool again = attempt == 0 && peek_pending(c).phase != UpdatePhase::RollingBack;
                note(std::string(crashed ? "the crash screen" : "the app") + " could not be started" +
                     std::string(again ? ": trying once more" : ""));
                if (!again) {
                    break;
                }
            }
        }
        if (!started && !crashed) {
            // The app's start failed (`count` ends 75 while the data is not back). Its unit failed, and
            // the crash screen that OnFailure= starts was held back by `idle` for as long as this run held
            // the acting lock: so it is started here, once the lock is released.
            acting.reset();
            forget_acting(c);
            screen = service.start_crash_screen(trial.unit);
            note(screen ? "the app was not started: the crash screen is" : "the crash screen could not be started");
        }
        if (!started && !screen) {
            const UpdateStatus said = read_status(c);
            write_status(c, "failed", said.version, said.from,
                         (said.reason.empty() ? std::string("the update went back") : said.reason) +
                             "; and the app could not be started again",
                         said.data);
        }
    }
    return stopped && (started || screen) ? 0 : 1;
}

bool confirm_trial(const Context& c) {
    std::string error;
    StateLock lock(c, "lock", true, error);
    if (!lock.held()) {
        note(error);
        return false;
    }
    const Service service(c);
    const PendingUpdate s = load_state_checked(c, service, error);
    if (s.phase != UpdatePhase::Pending) {
        return false;
    }
    settle_ok(c, s);
    return true;
}

int update_idle(const Context& c) {
    // Only the helper acting on the app (a stop, a kill, a start) ends 1: the failure that can leave is
    // not a crash. While it acts, any failure is its own; for a while after (a stop it gave up waiting for
    // still ends in systemd's own time) one that looks like a stop's, a time out or a SIGKILL, is. The
    // helper's own trouble (a state folder that is not root's, a lock file that cannot be opened) ends 0
    // with a line in the journal, as `count` does: it must not keep the crash screen away.
    std::string error;
    {
        StateLock acting(c, "acting.lock", false, error);
        if (!acting.held()) {
            if (acting.contended()) {
                return 1;
            }
            note("idle: " + error + " (not an update in flight; not holding the crash screen back)");
            return 0;
        }
    }
    const long since = seconds_since_acting(c);
    if (since < 0 || since >= kActingWindowS) {
        return 0;
    }
    const Service service(c);
    for (const char* unit : {"etude.service", "etude-labwc.service"}) {
        if (service.ended_by_stop(unit)) {
            return 1;
        }
    }
    return 0;
}

// A going back that cannot even be tried (the layout is refused, the lock is not given) still counts as
// a failed attempt, and the status says so for the crash screen. Best effort.
static void count_failed_attempt(const Context& c, const std::string& why) {
    std::string error;
    StateLock lock(c, "lock", true, error, 5);
    if (!lock.held()) {
        return;
    }
    PendingUpdate s = peek_pending(c);
    if (s.phase != UpdatePhase::RollingBack) {
        return;
    }
    s.restores = std::min(s.restores + 1, 100);
    s.data_error = why;
    save_or_note(c, s);
    const UpdateStatus said = read_status(c);
    write_status(c, "failed", said.version.empty() ? s.to : said.version, said.from.empty() ? s.from : said.from,
                 said.reason.empty() ? s.reason : said.reason, why);
}

int count_start(const Context& c) {
    // The helper's own trouble never keeps the app from starting, except one thing: a going back is
    // recorded and cannot be finished here (a layout that is refused, a lock that is not given), so
    // the app would start on the new version's data. That ends 75; the unit fails and the crash screen
    // says why and offers to try again.
    const auto cannot_settle = [&](const std::string& why) {
        note(why);
        const PendingUpdate recorded = peek_pending(c);
        if (recorded.phase == UpdatePhase::RollingBack) {
            count_failed_attempt(c, why);
            note("a going back is recorded and could not be finished: the app does not start yet");
            return kRestoreUnfinished;
        }
        return 0;
    };
    if (const std::string layout = check_layout(c); !layout.empty()) {
        return cannot_settle("not settling an update in " + c.conf.prefix + ": " + layout);
    }
    std::string error;
    StateLock lock(c, "lock", true, error, kCountLockWaitS);
    if (!lock.held()) {
        return cannot_settle(error);
    }
    const Service service(c);
    PendingUpdate s = load_state_checked(c, service, error);
    if (!error.empty()) {
        note(error);
    }
    // A going back that did not finish is the one thing that stops the app from starting: its data
    // would be the new version's. The service is not running yet, so the data is safe to restore; a
    // failure here ends the start, and the player's "Try again" (or "Restart") makes the next one.
    if (s.phase == UpdatePhase::RollingBack) {
        return roll_back(c, service, s, s.reason) ? 0 : kRestoreUnfinished;
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
    const bool counted = save_or_note(c, s);
    note("start " + std::to_string(s.starts) + " of " + s.to + " without ready");
    if (!counted || s.starts > c.conf.max_starts) {
        const std::string why = counted ? s.to + " did not reach its first frame in " +
                                              std::to_string(s.starts - 1) + " starts"
                                        : "the start of " + s.to + " could not be counted";
        return roll_back(c, service, s, why) ? 0 : kRestoreUnfinished;
    }
    return 0;
}

int settle_after_crash(const Context& c) {
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
    const Service service(c);
    PendingUpdate s = load_state_checked(c, service, error);
    if (!error.empty()) {
        note(error);
    }
    if (s.phase == UpdatePhase::Switching) {
        reconcile_switch(c, s);
    }
    // A going back that is recorded is `count`'s: the next start puts the data back and says why when it
    // cannot. Only a version on trial that has not said ready is gone back from here.
    if (s.phase != UpdatePhase::Pending) {
        return 0;
    }
    const Fd update = open_update_dir(c, error);
    if (update.ok() && ready_for(c, update.get(), s.to)) {
        settle_ok(c, s);
        return 0;
    }
    roll_back(c, service, s, s.to + " closed unexpectedly before it said ready");
    return 0;
}

int watch_trial(const Context& c) {
    const Service service(c);
    int polls = 0;
    for (;;) {
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::seconds(c.conf.ready_timeout_s);
        PendingUpdate trial;
        for (;;) {
            bool locked = false;
            trial = locked_state(c, service, locked);
            if (!locked && trial.phase == UpdatePhase::Pending && consume_interrupt()) {
                // Told to stop while waiting for the lock: the trial is still unproven, so it is gone back
                // from the short way (the record read without the lock), never left unwatched.
                note("told to stop while " + trial.to + " is on trial: going back");
                return go_back(c, service, trial,
                               "the update was interrupted (the helper was told to stop) before " +
                                   trial.to + " said ready",
                               true);
            }
            if (trial.phase == UpdatePhase::RollingBack) {
                // A record that was damaged and worked out, or a going back that was left: it is not
                // watched, it is gone back from.
                return go_back(c, service, trial, trial.reason, false);
            }
            if (trial.phase != UpdatePhase::Pending) {
                return 0; // settled by someone else: a start that was out of starts, say
            }
            std::string error;
            const Fd update = open_update_dir(c, error);
            if (update.ok() && ready_for(c, update.get(), trial.to)) {
                confirm_trial(c);
                return 0;
            }
            if (consume_interrupt()) {
                // Told to stop while the new version is still unproven: go back the way a failed
                // trial does, so nothing is left running that nobody is watching.
                note("told to stop while " + trial.to + " is on trial: going back");
                return go_back(c, service, trial,
                               "the update was interrupted (the helper was told to stop) before " +
                                   trial.to + " said ready",
                               true); // short: systemd ends this run soon; `count` restores the data
            }
            // Nothing restarts the app by itself: a trial that crashed, or that the watchdog ended, left its
            // unit failed. It is gone back from now, and the crash screen shows (the app is not started
            // again on a question nobody asked).
            if (++polls % 4 == 0 && !trial.unit.empty() && service.state(trial.unit) == "failed" &&
                !service.ended_by_stop(trial.unit)) {
                note(trial.to + " closed unexpectedly before it said ready: going back");
                return go_back(c, service, trial, trial.to + " closed unexpectedly before it said ready", false,
                               true);
            }
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(250));
        }
        if (trial.starts >= c.conf.max_starts) {
            return go_back(c, service, trial,
                           trial.to + " did not reach its first frame in " +
                               std::to_string(trial.starts) + " starts of " +
                               std::to_string(c.conf.ready_timeout_s) + " s",
                           false);
        }
        note(trial.to + " did not reach its first frame in " + std::to_string(c.conf.ready_timeout_s) +
             " s: starting it again");
        const ActingScope acting(c, kActingLockWaitS);
        if (trial.unit.empty() || !service.restart(trial.unit)) {
            note("the service could not be restarted");
        }
    }
}

int resume_boot(const Context& c) {
    if (const std::string layout = check_layout(c); !layout.empty()) {
        note("not settling an update in " + c.conf.prefix + ": " + layout);
        return 0;
    }
    std::string error;
    StateLock run(c, "run.lock", true, error);
    if (!run.held()) {
        note(error);
        return 1;
    }
    {
        StateLock lock(c, "lock", true, error);
        const Service service(c);
        PendingUpdate s = lock.held() ? load_state_checked(c, service, error) : PendingUpdate{};
        if (s.phase == UpdatePhase::RollingBack) {
            // The data may only be put back while the app is not running; the start of the service
            // does it before the app starts (count), so here only when nothing runs.
            if (!service.running("etude.service") && !service.running("etude-labwc.service")) {
                roll_back(c, service, s, s.reason);
            }
            return 0;
        }
        if (s.phase == UpdatePhase::Switching) {
            reconcile_switch(c, s);
        }
        if (s.phase == UpdatePhase::None && read_status(c).state == UpdateState::Applying) {
            // An update was taken in and the power went (or the helper was killed) before it
            // switched: no state, but the status still says "applying". Say what happened.
            const UpdateStatus cut = read_status(c);
            write_status(c, "failed", cut.version, cut.from,
                         "the update was cut short before it switched (the device lost power or "
                         "was restarted); " + cut.from + " is still in use");
        }
        if (s.phase != UpdatePhase::Pending) {
            return 0;
        }
    }
    return watch_trial(c);
}

} // namespace etude::helper
