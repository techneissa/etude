// SPDX-License-Identifier: GPL-3.0-or-later
#include "process.hpp"

#include "fd.hpp"
#include "interrupt.hpp"

#include <fcntl.h>
#include <grp.h>
#include <poll.h>
#include <pwd.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#if defined(__APPLE__)
#include <crt_externs.h>
#endif

#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstring>

namespace etude::helper {

namespace {

constexpr std::size_t kMaxOutput = 16 * 1024;

using Clock = std::chrono::steady_clock;

// Reads what comes on `fd` into `out` until it closes or `deadline` passes; false on the deadline or
// when the helper is told to stop (`stopped`).
bool drain(int fd, std::string& out, Clock::time_point deadline, bool interruptible, bool& stopped) {
    char buffer[4096];
    for (;;) {
        if (interruptible && interrupt_in_wait()) {
            stopped = true;
            return false;
        }
        const auto left =
            std::chrono::duration_cast<std::chrono::milliseconds>(deadline - Clock::now()).count();
        if (left <= 0) {
            return false;
        }
        pollfd p{fd, POLLIN, 0};
        const int ready = ::poll(&p, 1, static_cast<int>(std::min<long long>(left, 1000)));
        if (ready < 0 && errno != EINTR) {
            return true;
        }
        if (ready <= 0) {
            continue;
        }
        const ssize_t n = ::read(fd, buffer, sizeof buffer);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return true;
        }
        if (out.size() < kMaxOutput) {
            out.append(buffer, static_cast<std::size_t>(std::min<std::size_t>(
                                   static_cast<std::size_t>(n), kMaxOutput - out.size())));
        }
    }
}

int make_pipe(int pipes[2]) {
    if (::pipe(pipes) != 0) {
        return -1;
    }
    ::fcntl(pipes[0], F_SETFD, FD_CLOEXEC);
    ::fcntl(pipes[1], F_SETFD, FD_CLOEXEC);
    return 0;
}

int exit_code_of(int status) { return WIFEXITED(status) ? WEXITSTATUS(status) : -1; }

// Waits for `pid`; after `deadline`, or when the helper is told to stop, kills it. True when it ended
// on its own.
bool reap(pid_t pid, Clock::time_point deadline, int& status, bool interruptible, bool& stopped) {
    long nap_ns = 500'000;
    for (;;) {
        if (interruptible && interrupt_in_wait()) {
            stopped = true;
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            return false;
        }
        const pid_t done = ::waitpid(pid, &status, WNOHANG);
        if (done == pid) {
            return true;
        }
        if (done < 0 && errno != EINTR) {
            status = -1;
            return true;
        }
        if (Clock::now() >= deadline) {
            ::kill(pid, SIGKILL);
            ::waitpid(pid, &status, 0);
            return false;
        }
        struct timespec nap{0, nap_ns}; // a program that has just closed its output is gone in a moment
        ::nanosleep(&nap, nullptr);
        nap_ns = std::min<long>(nap_ns * 2, 20'000'000);
    }
}

bool dropped_for_good(const AppUser& user) {
    if (::getuid() != user.uid || ::geteuid() != user.uid || ::getgid() != user.gid ||
        ::getegid() != user.gid) {
        return false;
    }
    // Root again must be out of reach.
    return user.uid == 0 || (::setuid(0) != 0 && ::setgid(0) != 0);
}

// initgroups takes an int for the group on macOS and a gid_t on Linux.
#if defined(__APPLE__)
using GroupArg = int;
#else
using GroupArg = gid_t;
#endif

// Closes every descriptor but 0 to 2 and `keep`, and empties the environment: what the job runs with
// is its own, not what the helper's parent handed down.
void shed_inheritance(int keep) {
    long limit = ::sysconf(_SC_OPEN_MAX);
    if (limit < 0 || limit > 65536) {
        limit = 65536;
    }
    for (int fd = 3; fd < limit; ++fd) {
        if (fd != keep) {
            ::close(fd);
        }
    }
#if defined(__APPLE__)
    *_NSGetEnviron() = nullptr;
#else
    ::clearenv();
#endif
}

bool become(const AppUser& user) {
    if (::geteuid() != 0) {
        return ::geteuid() == user.uid; // not root: nothing to drop, and no other user to become
    }
    if (::initgroups(user.name.c_str(), static_cast<GroupArg>(user.gid)) != 0 || ::setgid(user.gid) != 0 ||
        ::setuid(user.uid) != 0) {
        return false;
    }
    return dropped_for_good(user);
}

} // namespace

ProgramResult run_program(const std::vector<std::string>& argv, double timeout_s, bool interruptible) {
    ProgramResult result;
    if (argv.empty() || argv[0].empty() || argv[0].front() != '/') {
        return result;
    }
    int pipes[2];
    if (make_pipe(pipes) != 0) {
        return result;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipes[0]);
        ::close(pipes[1]);
        return result;
    }
    if (pid == 0) {
        const int null = ::open("/dev/null", O_RDONLY);
        ::dup2(null, 0);
        ::dup2(pipes[1], 1);
        ::dup2(pipes[1], 2);
        std::vector<char*> args;
        for (const std::string& a : argv) {
            args.push_back(const_cast<char*>(a.c_str()));
        }
        args.push_back(nullptr);
        char path[] = "PATH=/usr/sbin:/usr/bin:/sbin:/bin";
        char lang[] = "LANG=C";
        char* env[] = {path, lang, nullptr};
        ::execve(argv[0].c_str(), args.data(), env);
        ::_exit(127);
    }
    ::close(pipes[1]);
    const Fd reader(pipes[0]);
    const auto deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(timeout_s * 1000));
    bool stopped = false;
    const bool in_time = drain(reader.get(), result.output, deadline, interruptible, stopped);
    int status = 0;
    const bool ended = in_time ? reap(pid, deadline + std::chrono::seconds(5), status, interruptible, stopped)
                               : (::kill(pid, SIGKILL), ::waitpid(pid, &status, 0), false);
    result.ran = true;
    result.interrupted = stopped;
    result.timed_out = !stopped && (!in_time || !ended);
    result.exit_code = result.timed_out || stopped ? -1 : exit_code_of(status);
    return result;
}

bool find_app_user(const std::string& name, AppUser& out, std::string& error) {
    const passwd* entry = ::getpwnam(name.c_str());
    if (entry == nullptr) {
        error = "there is no user " + name;
        return false;
    }
    if (entry->pw_uid == 0) {
        error = name + " is root: the app's data is never touched as root";
        return false;
    }
    if (entry->pw_dir == nullptr || entry->pw_dir[0] != '/') {
        error = name + " has no home folder";
        return false;
    }
    out.name = name;
    out.uid = entry->pw_uid;
    out.gid = entry->pw_gid;
    out.home = entry->pw_dir;
    return true;
}

bool run_as_user(const AppUser& user, double timeout_s, const std::function<std::string()>& job,
                 std::string& error) {
    int pipes[2];
    if (make_pipe(pipes) != 0) {
        error = std::string("pipe: ") + std::strerror(errno);
        return false;
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        error = std::string("fork: ") + std::strerror(errno);
        ::close(pipes[0]);
        ::close(pipes[1]);
        return false;
    }
    if (pid == 0) {
        shed_inheritance(pipes[1]);
        std::string message;
        if (!become(user)) {
            message = "could not become " + user.name;
        } else {
            ::umask(077);
            if (::chdir("/") != 0) {
                message = "could not leave the current folder";
            } else {
                ::alarm(static_cast<unsigned>(std::ceil(timeout_s)));
                try {
                    message = job();
                } catch (const std::exception& e) {
                    message = e.what();
                } catch (...) {
                    message = "it failed";
                }
            }
        }
        if (!message.empty()) {
            const ssize_t ignored = ::write(pipes[1], message.data(), std::min<std::size_t>(message.size(), 2000));
            (void)ignored;
        }
        ::_exit(message.empty() ? 0 : 1);
    }
    ::close(pipes[1]);
    const Fd reader(pipes[0]);
    const auto deadline = Clock::now() + std::chrono::milliseconds(static_cast<long long>(timeout_s * 1000));
    std::string said;
    bool stopped = false;
    const bool in_time = drain(reader.get(), said, deadline, true, stopped);
    int status = 0;
    const bool ended = in_time ? reap(pid, deadline + std::chrono::seconds(5), status, true, stopped)
                               : (::kill(pid, SIGKILL), ::waitpid(pid, &status, 0), false);
    if (stopped) {
        error = "interrupted";
        return false;
    }
    if (!in_time || !ended) {
        error = "it took too long";
        return false;
    }
    if (exit_code_of(status) != 0) {
        error = said.empty() ? "it failed" : said;
        return false;
    }
    return true;
}

} // namespace etude::helper
