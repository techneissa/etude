// SPDX-License-Identifier: GPL-3.0-or-later
// What every part of the root update helper shares: its checked settings, the app's user and the
// places it works in. Part of the root update helper (helper/main.cpp).
#pragma once

#include "process.hpp"

#include "etude/update_conf.hpp"

#include <algorithm>
#include <cmath>
#include <string>

namespace etude::helper {

struct Context {
    UpdateConf conf;
    AppUser user;
    // The folder above which the prefix's ancestors are not looked at ("/" in service; a test sets
    // its own temporary folder, since /tmp is world-writable).
    std::string ancestor_floor = "/";
    // How long one systemctl call that does something may take (SERVICE_TIMEOUT; a test shortens it),
    // and how long one that only asks (the state, a kill) may.
    double service_timeout_s = 90;
    // A test that shortens the calls that do something sets `query_floor_s`, so the ones that ask
    // keep a safe limit.
    double query_floor_s = 0;
    double query_timeout_s() const {
        return std::max(service_timeout_s < 20 ? service_timeout_s : 20, query_floor_s);
    }
    // How long the helper waits for the new version to say ready, per start (READY_TIMEOUT; a test
    // shortens it).
    double ready_timeout_s = 30;
    // The nap between two looks at a unit it has killed, and the pause between two looks at a
    // trial's ready file (a test shortens both).
    int kill_nap_ms = 1000;
    int watch_poll_ms = 250;
    // How long the backup or restore of the app's data may take, as the app's user.
    int data_timeout_s = 240;

    std::string update_dir() const { return conf.prefix + "/update"; }
    std::string versions_dir() const { return conf.prefix + "/versions"; }
    std::string version_dir(const std::string& version) const {
        return versions_dir() + "/" + version;
    }
};

// How long `count` waits for the state lock before it gives up (and lets the app start, unless a going
// back is recorded: the lock is then the one thing in the way of finishing it).
inline constexpr int kCountLockWaitS = 30;
// How long the helper waits for the lock that says it is acting on the app (it is held only by a run of
// the helper itself, so a wait is a second run finishing).
inline constexpr int kActingLockWaitS = 10;

// The time budget, in one place: every figure is worked out from the helper's own timeouts
// (`service_timeout_s` for a systemctl call that does something, `query_timeout_s()` for one that only
// asks, `data_timeout_s` for the backup or restore), and the units' TimeoutStartSec and TimeoutStopSec
// (deploy/pi) must cover them, with margin, at the defaults and at the largest values the settings
// file may ask for (update_conf.hpp).
//
// One program run with a timeout of `t` seconds: `run_program` gives it `t`, then 5 more to be reaped.
inline int worst_case_call_seconds(double t) { return static_cast<int>(std::ceil(t)) + 5; }

// Stopping an app that will not stop: `stop` and a look at its state, twice; then a kill of the main
// process and a kill of the whole unit, each the call and three looks (a state and a 1 s wait); then
// a last look.
inline int worst_case_hard_stop_seconds(const Context& c) {
    const int s = worst_case_call_seconds(c.service_timeout_s);
    const int q = worst_case_call_seconds(c.query_timeout_s());
    return 2 * (s + q) + 2 * (q + 3 * (q + 1)) + q;
}

// Starting the app: `reset-failed`, then `start`.
inline int worst_case_start_seconds(const Context& c) {
    return 2 * worst_case_call_seconds(c.service_timeout_s);
}

// Reading the data folder (`systemctl show`), which a going back does once to find it.
inline int worst_case_data_folder_seconds(const Context& c) {
    return worst_case_call_seconds(c.service_timeout_s);
}

// Finding which of the two units is running (a look at each).
inline int worst_case_active_unit_seconds(const Context& c) {
    return 2 * worst_case_call_seconds(c.query_timeout_s());
}

// Recovering a damaged record: the running unit, then its data folder.
inline int worst_case_recover_seconds(const Context& c) {
    return worst_case_active_unit_seconds(c) + worst_case_data_folder_seconds(c);
}

// The longest an update can take if every wait runs out: the copy, check and unpack, finding the
// running unit, stopping the app, the backup, the switch, starting the new version, its trial (each
// start `ready_timeout_s` and a restart), and the going back (a hard stop, a damaged record recovered,
// the restore, a start tried twice). etude-update.service's TimeoutStartSec must cover it, so
// systemd never ends a run in the middle.
inline int worst_case_update_seconds(const Context& c) {
    const int stage = 120;
    const int stop = worst_case_call_seconds(c.service_timeout_s) + worst_case_call_seconds(c.query_timeout_s());
    const int start = worst_case_start_seconds(c);
    const int trial = c.conf.max_starts * (c.conf.ready_timeout_s + start);
    const int going_back = worst_case_hard_stop_seconds(c) + worst_case_recover_seconds(c) +
                           c.data_timeout_s + 2 * start;
    return stage + worst_case_active_unit_seconds(c) + stop + c.data_timeout_s + 5 + start + trial + going_back;
}

// What a stop of the update service (SIGTERM) may take to finish: a hard stop of the app, a damaged
// record recovered, the links back, and a start that is only queued, tried twice (the data restore is
// left to `count`). The units' TimeoutStopSec must cover it.
inline int worst_case_stop_seconds(const Context& c) {
    return worst_case_hard_stop_seconds(c) + worst_case_recover_seconds(c) + 2 * worst_case_start_seconds(c);
}

// What `count` may take before an app start: the lock, a damaged record recovered, then a going back
// (the data folder again, the restore). The app units' TimeoutStartSec must cover it.
inline int worst_case_count_seconds(const Context& c) {
    return kCountLockWaitS + worst_case_recover_seconds(c) + worst_case_data_folder_seconds(c) + c.data_timeout_s;
}

// One line to the journal, with the helper's name in front.
void note(const std::string& text);

} // namespace etude::helper
