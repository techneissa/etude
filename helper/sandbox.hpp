// SPDX-License-Identifier: GPL-3.0-or-later
// `count`, `crashed` and `idle` run before every start of the app or of the crash screen, as root:
// `count` and `crashed` go back (they delete trees, prune versions, move links and fork the restore). So the app units
// do not run them in place: `etude-update-helper sandboxed MODE` asks systemd to run the same
// program as a transient service (`systemd-run --pipe --wait --collect`) under the sandbox of the
// update service, and waits. The sandbox is one list of properties, the drop-in
// deploy/pi/etude-update-sandbox.conf that setup.sh installs for the update service and the boot one
// and that this reads from the update service's copy, so the units and the transient run cannot
// drift. The transient service is a unit of its own that nothing waits for, so it cannot deadlock
// against the app unit's start job, which is waiting for this. Its exit status is the answer: 0, the
// restore's 75 (`count`), and for `idle` its `kSandboxedBusy`. Part of the root update helper
// (helper/main.cpp).
#pragma once

#include "context.hpp"

#include <sys/types.h>

#include <optional>
#include <string>
#include <vector>

namespace etude::helper {

// Where the properties are: the update service's drop-in, as setup.sh renders it.
inline constexpr const char* kDefaultSandboxFile = "/etc/systemd/system/etude-update.service.d/sandbox.conf";

// What `idle` ends in the transient run when an update is in flight: not 1, which is also what
// systemd-run itself ends with when it cannot start the unit.
inline constexpr int kSandboxedBusy = 10;

// The properties of the sandbox file: a drop-in's `[Service]` line, blank lines and `#` comments are
// skipped, every other line must be `Name=value`. The file is data, read the way the settings file is:
// `owner`'s (root), a plain file, nobody else may write it.
bool load_sandbox(const std::string& path, std::vector<std::string>& properties, std::string& error,
                  uid_t owner = 0);

struct SandboxRun {
    std::string systemd_run = "/usr/bin/systemd-run";
    std::string helper;  // this program, by absolute path
    std::string config;  // its --config, "" for the default
    std::string mode;    // count, crashed or idle
    std::string unit;    // the transient service's name
    int limit_s = 0;     // RuntimeMaxSec of the transient service
    int launch_margin_s = 20; // what systemd-run itself may add to that before it is ended
    std::vector<std::string> properties;
};

// The command line, one word each.
std::vector<std::string> sandbox_argv(const SandboxRun& run);

// How long the transient run may take: the helper's own worst case for the mode, and a margin.
int sandbox_limit_seconds(const Context& c, const std::string& mode);

// The first line of a sandboxed run's output, written before anything else the program does, so the
// launcher knows the run began. systemd-run ends 1 both when it could not start the unit and when the
// unit ran and then crashed, hit its time limit or ran out of memory; only the marker tells them apart.
inline constexpr const char* kSandboxStartMarker = "etude-update-helper: sandboxed run started";

// Runs the mode in the sandbox and returns the status the unit should end with. Empty when the
// sandbox did not start: `systemd-run` is not there, or it would not start the unit (the program never
// said it began). The caller then does what needs no sandbox in place, never the whole mode (main.cpp:
// `idle`, which reads only root's state, as it is; `count`, only its state-and-links part, see
// `count_in_place`): a count that is skipped is a trial that never rolls back, and what keeps the unit
// from starting may be the app's doing. A run that began and went wrong (crashed, hit its limit, ran
// out of memory), one cut short by a stop of the unit (stopped by name) and an unknown status end 0,
// logged, and are not run again. A RuntimeMaxSec in the properties can only shorten the run: the
// launcher stops it at the limit it computed (`limit_s` and the margin) whatever that line says.
std::optional<int> run_sandboxed(const Context& c, const SandboxRun& run);

} // namespace etude::helper
