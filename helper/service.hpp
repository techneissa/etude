// SPDX-License-Identifier: GPL-3.0-or-later
// The app's service, through systemctl: what state it is in, stop it (and make sure it stopped),
// start it, and the environment it has (for XDG_DATA_HOME). Every answer that is not clear counts
// against going on: a bus that does not answer, a call that times out and a state that is not
// exactly "inactive" or "failed" are "running", so the helper never installs or restores under an app
// that may still be alive. Part of the root update helper (helper/main.cpp).
#pragma once

#include "context.hpp"

#include "etude/data_folder.hpp"

#include <string>
#include <vector>

namespace etude::helper {

class Service {
  public:
    explicit Service(const Context& c) : c_(c) {}

    // What `systemctl is-active` prints for the unit ("active", "activating" (also an auto-restart
    // that is waiting), "deactivating", "inactive", "failed", ...), or "" when the answer did not come
    // (the program did not run, timed out or printed nothing).
    std::string state(const std::string& unit) const;
    // False only for exactly "inactive" or "failed": anything else, an unknown state included, may
    // still be running.
    bool running(const std::string& unit) const;
    // The first of the app's services that is running, or "".
    std::string active_unit() const;
    // `stop`, then `is-active`: false (with `error`) unless the unit is "inactive" or "failed" after.
    bool stop(const std::string& unit, std::string& error) const;
    // The unit failed the way a stop the helper made fails: its stop timed out (`Result=timeout`) or the
    // process was killed with SIGKILL (the helper's `systemctl kill`; the app never ends itself so). A crash
    // is a signal of its own, an exit status or the watchdog.
    bool ended_by_stop(const std::string& unit) const;
    // For going back, when the app must be down: `stop` again if it did not stop in time, then kill the
    // unit's main process and then everything in it, asking the state each time. `killed` says a kill
    // was needed. False (with `error`) only when the unit is still not "inactive" or "failed" after it.
    // None of these calls is cut short by SIGTERM: this is clean-up.
    bool stop_hard(const std::string& unit, bool& killed, std::string& error) const;
    bool start(const std::string& unit) const;
    // Queues the start and returns at once: the job goes on without the helper, which may be ended.
    bool start_queued(const std::string& unit) const;
    // Queues the start of the crash screen that goes with `app_unit` (core `crash_screen_unit`): the
    // app crashed and was gone back from, and the player chooses what starts next.
    bool start_crash_screen(const std::string& app_unit) const;
    bool restart(const std::string& unit) const;
    // XDG_DATA_HOME in the service's Environment.
    EnvLookup xdg_data_home(const std::string& unit) const;

  private:
    std::string state_of(const std::string& unit) const;
    void forgive(const std::string& unit) const;
    bool systemctl(const std::vector<std::string>& args, std::string* output = nullptr,
                   bool interruptible = false) const;
    const Context& c_;
};

// The data folder of the app's user: where SDL_GetPrefPath puts it for them, with XDG_DATA_HOME from
// the service's environment. "" when it cannot be worked out, an environment that cannot be read
// included (`unit` "": the main service).
std::string data_folder_for(const Context& c, const Service& service, const std::string& unit);

} // namespace etude::helper
