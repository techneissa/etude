// SPDX-License-Identifier: GPL-3.0-or-later
// Running another program from the app: with its output read and a time limit (the Mac update runs
// `ditto`, `codesign` and `plutil` this way), or started in a session of its own so it outlives the
// app (the relaunch script). No shell: the arguments are passed as they are. No SDL.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace etude::app {

struct ProgramResult {
    bool started = false;  // the program was found and began
    bool timed_out = false;
    int exit_code = -1;    // its exit status, -1 when it did not end normally
    std::string output;    // standard output and error together, at most 64 KiB
};

// What the update code runs programs through, so a test can stand in for them.
using ProgramRunner = std::function<ProgramResult(const std::vector<std::string>& argv, double timeout_s)>;

// Runs argv[0] (a full path) with the other arguments and waits at most `timeout_s` seconds, then
// ends it.
ProgramResult run_program(const std::vector<std::string>& argv, double timeout_s);

// Starts argv[0] in a new session with its output in `log_path`, no descriptor of this process
// open, and does not wait. False when it could not be started.
bool spawn_detached(const std::vector<std::string>& argv, const std::string& log_path);

} // namespace etude::app
