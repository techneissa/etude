// SPDX-License-Identifier: GPL-3.0-or-later
// The two ways the root update helper runs other code: a program (systemctl) with no shell and a
// clean environment, and a piece of its own code as the app's user, in a child that has dropped
// every privilege first. Part of the root update helper (helper/main.cpp).
#pragma once

#include <sys/types.h>

#include <functional>
#include <string>
#include <vector>

namespace etude::helper {

struct ProgramResult {
    bool ran = false;       // it started (fork and exec worked)
    int exit_code = -1;     // its exit status; -1 when killed
    bool timed_out = false;
    bool interrupted = false; // the helper was told to stop (SIGTERM) while it waited: killed
    std::string output;     // its output, at most 16 KiB
};

// Runs `argv` (argv[0] an absolute path) with no shell, no input and PATH only in its environment;
// kills it after `timeout_s` seconds. With `interruptible` a SIGTERM to the helper (interrupt.hpp)
// kills it early; a call that is part of the clean-up after one (start the app, ask its state) is not,
// so the clean-up is not cut short by the signal it answers.
ProgramResult run_program(const std::vector<std::string>& argv, double timeout_s, bool interruptible = true);

struct AppUser {
    std::string name;
    uid_t uid = 0;
    gid_t gid = 0;
    std::string home;
};

// The user's passwd entry. Refuses root (uid 0) and a user with no absolute home.
bool find_app_user(const std::string& name, AppUser& out, std::string& error);

// Runs `job` in a child process that has become `user` (group, supplementary groups, user, and
// checked: it must not be able to become root again), with every descriptor but its answer pipe
// closed, an empty environment, a clean umask and folder, limited to `timeout_s` seconds (and ended
// when the helper is told to stop). `job` returns "" when it worked or a sentence when it did not, which comes
// back in `error`. True when it worked. Where the helper is not root and `user` is who it is
// already, nothing is dropped (a unit test runs that way).
bool run_as_user(const AppUser& user, double timeout_s, const std::function<std::string()>& job,
                 std::string& error);

} // namespace etude::helper
