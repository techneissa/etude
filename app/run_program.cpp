// SPDX-License-Identifier: GPL-3.0-or-later
#include "run_program.hpp"

#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <spawn.h>
#include <sys/wait.h>
#include <unistd.h>

#include <cerrno>
#include <chrono>

extern char** environ;

namespace etude::app {

namespace {

constexpr std::size_t kMaxOutput = 64 * 1024;

std::vector<char*> c_args(const std::vector<std::string>& argv) {
    std::vector<char*> out;
    for (const std::string& a : argv) {
        out.push_back(const_cast<char*>(a.c_str()));
    }
    out.push_back(nullptr);
    return out;
}

// What a child must not inherit: every descriptor of this process but the ones it is given.
void close_inherited(posix_spawnattr_t& attr) {
#if defined(__APPLE__)
    short flags = 0;
    posix_spawnattr_getflags(&attr, &flags);
    posix_spawnattr_setflags(&attr, static_cast<short>(flags | POSIX_SPAWN_CLOEXEC_DEFAULT));
#else
    (void)attr;
#endif
}

} // namespace

ProgramResult run_program(const std::vector<std::string>& argv, int timeout_s) {
    ProgramResult result;
    int fds[2];
    if (argv.empty() || ::pipe(fds) != 0) {
        return result;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDOUT_FILENO);
    posix_spawn_file_actions_adddup2(&actions, fds[1], STDERR_FILENO);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    close_inherited(attr);
    std::vector<char*> args = c_args(argv);
    pid_t pid = 0;
    const int started = ::posix_spawn(&pid, argv[0].c_str(), &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    ::close(fds[1]);
    if (started != 0) {
        ::close(fds[0]);
        return result;
    }
    result.started = true;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(timeout_s);
    char buffer[4096];
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            result.timed_out = true;
            ::kill(pid, SIGKILL);
            break;
        }
        pollfd waiting{fds[0], POLLIN, 0};
        const int ready = ::poll(&waiting, 1, static_cast<int>(left.count()));
        if (ready < 0 && errno == EINTR) {
            continue;
        }
        if (ready <= 0) {
            continue; // the next turn sees the deadline
        }
        const ssize_t got = ::read(fds[0], buffer, sizeof buffer);
        if (got <= 0) {
            break; // the program closed its output
        }
        if (result.output.size() < kMaxOutput) {
            result.output.append(buffer, static_cast<std::size_t>(got));
        }
    }
    ::close(fds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    result.exit_code = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    return result;
}

bool spawn_detached(const std::vector<std::string>& argv, const std::string& log_path) {
    if (argv.empty()) {
        return false;
    }
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_addopen(&actions, STDIN_FILENO, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO, log_path.c_str(),
                                     O_WRONLY | O_CREAT | O_APPEND, 0600);
    posix_spawn_file_actions_adddup2(&actions, STDOUT_FILENO, STDERR_FILENO);
    posix_spawnattr_t attr;
    posix_spawnattr_init(&attr);
    short flags = 0;
#if defined(POSIX_SPAWN_SETSID)
    flags = static_cast<short>(flags | POSIX_SPAWN_SETSID);
#endif
    posix_spawnattr_setflags(&attr, flags);
    close_inherited(attr);
    std::vector<char*> args = c_args(argv);
    pid_t pid = 0;
    const int started = ::posix_spawn(&pid, argv[0].c_str(), &actions, &attr, args.data(), environ);
    posix_spawn_file_actions_destroy(&actions);
    posix_spawnattr_destroy(&attr);
    return started == 0;
}

} // namespace etude::app
