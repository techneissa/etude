// SPDX-License-Identifier: GPL-3.0-or-later
// Settling an update after the switch: counting the starts of the new version, confirming it when it
// says ready, and going back to the old one when it does not. All of it works from the state file, so
// a reboot or a power cut in the middle of an update is finished by the next run, and every step can
// be repeated. Part of the root update helper (helper/main.cpp).
#pragma once

#include "context.hpp"
#include "service.hpp"

#include "etude/update_state.hpp"

#include <cstdint>
#include <string>

namespace etude::helper {

// What `count` returns when a going back did not finish (the data could not be put back): the app
// must not start on the new version's data, so the unit fails, no restart follows, and the crash
// screen says why (the status's `data`) and offers to try again.
inline constexpr int kRestoreUnfinished = 75;

// `etude-update-helper count`: run before every start of the app's service. Counts the start of a
// version that is still on trial; confirms it if it already said ready; goes back to the old one
// (the service is not running yet, so nothing is stopped) when it is out of starts, when the start
// could not be counted or when the record of the update is damaged and the version is unproven;
// finishes a going back that was cut short. 0 except `kRestoreUnfinished`: the helper's other
// troubles never keep the app from starting. Every start that ends 75 is one the player asked for
// ("Restart", "Try again"), so a restore is tried at each, with no limit.
int count_start(const Context& c);

// `etude-update-helper crashed`: run before the crash screen starts. A version on trial that has not
// said ready is gone back from at once, with no question asked (links, data, the new version removed),
// since the player did not choose the crash; one that has said ready is confirmed. A going back that
// is already recorded is left to the next `count`. Always 0: the screen shows either way, and says
// from the status why the data is not back when it is not.
int settle_after_crash(const Context& c);

// `etude-update-helper resume`: at boot. Finishes an interrupted switch or going back and, for a
// version still on trial, waits for it as `watch` does.
int resume_boot(const Context& c);

// `etude-update-helper idle`: 0 unless the helper is acting on the app right now (a stop, a kill, a
// start), which ends 1. The crash screen's unit asks it before it starts (ExecCondition=), so the failure
// a stop that had to be killed leaves behind is not taken for a crash. A crash while the helper only
// watches a version on trial is a crash: the screen shows. The helper's own trouble (a state folder
// that is not root's, a lock that cannot be opened) ends 0 too, logged.
int update_idle(const Context& c);

// Waits for the version on trial to say ready: `ready_timeout_s` at a time; after each, restarts the
// service until `max_starts` starts have been counted, then goes back to the old version (stop the
// service, put `current`, `previous` and the data back, start it). A unit that failed (a crash, a
// watchdog kill) is gone back from at once and the crash screen is started in place of the app. 0
// when settled, either way.
int watch_trial(const Context& c);

// Under the state lock, the update on trial is confirmed: state cleared, status ok, the other old
// versions removed. False when it was no longer on trial.
bool confirm_trial(const Context& c);

// The state, as `count` and `resume` read it, for every path that reads it (the caller holds the state
// lock): a record that is there but cannot be read, is not root's or has no phase is not "no update".
// What was going on is worked out from the layout (a `current` that has not said ready and is not
// `previous` is a version on trial) and comes back as a RollingBack record, saved; `*recovered` says
// it was needed. Nothing that can be told gives phase None.
PendingUpdate load_state_checked(const Context& c, const Service& service, std::string& error,
                                 bool* recovered = nullptr);

// Goes back from the version on trial: stops the app (a second stop, then a kill of the unit, if it
// will not stop), puts the old version back, and makes sure the app is started again, whatever
// happened. `hurry` (the helper was told to stop) and an app that could not be stopped leave the data
// restore to `count` before the app starts (a RollingBack record is kept, the links are back and the
// start is only queued); otherwise it is all done here. 0 when the app could be stopped, 1 not.
// `crashed`: the unit already failed on its own, so nothing is stopped and what is started is the
// crash screen, not the app.
int go_back(const Context& c, const Service& service, const PendingUpdate& trial, const std::string& why,
            bool hurry, bool crashed = false);

enum class RollBackMode : std::uint8_t {
    Full,      // links, data, the new version removed, status, record cleared
    LinksOnly, // the record kept as RollingBack and the links put back; the data is for the next `count`
};

// Puts the old version back: `current`, `previous`, the data (as the app's user), removes the new
// version and its ready file, clears the state and writes the status. Repeatable. True when all of it
// worked; false leaves the state in RollingBack (the data could not be restored) for the next run.
// The caller holds the state lock.
bool roll_back(const Context& c, const Service& service, PendingUpdate state,
               const std::string& reason, RollBackMode mode = RollBackMode::Full);

// An interrupted switch: `current` was pointed at the new version or it was not. The caller holds the
// state lock.
void reconcile_switch(const Context& c, PendingUpdate& state);

// The update on trial is settled as good: the record cleared, the status written, old versions removed.
// The caller holds the state lock.
void settle_ok(const Context& c, const PendingUpdate& done);

// `count` for when the sandbox it runs in cannot be started (sandbox.hpp): the part of it that needs
// no sandbox. It reads and writes only root's state file and root's links and versions under the
// prefix: it counts the start of a version on trial and, when the trial has failed, points `current`
// back. It never looks at the app user's home or its data folder (no `systemctl show`, no restore, no
// backup): the data is left for a sandboxed run, and every start that gets none counts as a failed
// attempt to put it back, so the app does not start on the new version's data. A record that cannot
// be read is left alone: working out what was going on needs the data folder. Ends as `count_start`
// does, with `kRestoreUnfinished` while the data is not back.
int count_in_place(const Context& c);

// `settle_after_crash` for when its sandbox cannot be started: the links go back, the data is left
// for the next `count`.
int settle_after_crash_in_place(const Context& c);

} // namespace etude::helper
