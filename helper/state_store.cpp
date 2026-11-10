// SPDX-License-Identifier: GPL-3.0-or-later
#include "state_store.hpp"

#include "interrupt.hpp"
#include "safe_io.hpp"

#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#include <atomic>
#include <cerrno>
#include <chrono>
#include <cstdlib>
#include <string>

namespace etude::helper {

namespace {

constexpr const char* kStateFile = "state";

#if defined(ETUDE_HELPER_TEST_HOOKS)
int g_fail_save_in = 0;
std::atomic<int> g_lock_signals{0};
#endif

} // namespace

#if defined(ETUDE_HELPER_TEST_HOOKS)
void fail_save_in_test(int nth) { g_fail_save_in = nth; }
#endif

Fd open_state_dir(const Context& c, std::string& error) {
    if (::mkdir(c.conf.state_dir.c_str(), 0700) != 0 && errno != EEXIST) {
        error = c.conf.state_dir + ": " + errno_text(errno);
        return Fd();
    }
    Fd dir = open_directory(c.conf.state_dir, error);
    if (!dir.ok()) {
        return dir;
    }
    struct stat st{};
    if (::fstat(dir.get(), &st) != 0 || st.st_uid != ::geteuid() || (st.st_mode & (S_IRWXG | S_IRWXO)) != 0) {
        error = c.conf.state_dir + " is not root's alone (owner and mode 0700 are needed)";
        return Fd();
    }
    return dir;
}

StateLock::StateLock(const Context& c, const char* name, bool wait, std::string& error, double wait_s) {
    const Fd dir = open_state_dir(c, error);
    if (!dir.ok()) {
        return;
    }
    const int raw = ::openat(dir.get(), name, O_RDWR | O_CREAT | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (raw < 0) {
        error = std::string(name) + ": " + errno_text(errno);
        return;
    }
    Fd lock(raw);
    if (wait && wait_s > 0) {
        // A bounded wait: tried again every 50 ms until the time is up or the helper is told to stop.
        const auto deadline = std::chrono::steady_clock::now() +
                              std::chrono::milliseconds(static_cast<long long>(wait_s * 1000));
        while (::flock(lock.get(), LOCK_EX | LOCK_NB) != 0) {
            if (errno != EWOULDBLOCK && errno != EINTR) {
                error = std::string(name) + ": " + errno_text(errno);
                return;
            }
            if (interrupt_pending() || std::chrono::steady_clock::now() >= deadline) {
                contended_ = !interrupt_pending();
                error = interrupt_pending() ? std::string(name) + ": interrupted"
                                            : std::string(name) + " is held by another run";
                return;
            }
            struct timespec nap{0, 50'000'000};
            ::nanosleep(&nap, nullptr);
        }
        fd_ = std::move(lock);
        return;
    }
    // A signal cuts a blocking flock short (EINTR): try again, unless the helper was told to stop.
    while (::flock(lock.get(), wait ? LOCK_EX : (LOCK_EX | LOCK_NB)) != 0) {
        if (errno == EINTR && !interrupt_pending()) {
#if defined(ETUDE_HELPER_TEST_HOOKS)
            ++g_lock_signals;
#endif
            continue;
        }
        contended_ = errno == EWOULDBLOCK;
        error = errno == EINTR ? std::string(name) + ": interrupted"
                : contended_   ? std::string(name) + " is held by another run"
                               : std::string(name) + ": " + errno_text(errno);
        return;
    }
    fd_ = std::move(lock);
}

namespace {

void stamp_acting(const Context& c) {
    std::string error;
    const Fd dir = open_state_dir(c, error);
    if (!dir.ok()) {
        return;
    }
    const int raw = ::openat(dir.get(), "acting.stamp", O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (raw < 0) {
        return;
    }
    const Fd file(raw);
    const std::string text = std::to_string(static_cast<long long>(::time(nullptr))) + "\n";
    if (::write(file.get(), text.data(), text.size()) < 0) {
        // nothing to say it to: the stamp is a help, not a promise
    }
}

} // namespace

ActingScope::ActingScope(const Context& c, int wait_s) : c_(c), lock_(c, "acting.lock", true, error_, wait_s) {
    stamp_acting(c_);
}

ActingScope::~ActingScope() { stamp_acting(c_); }

void forget_acting(const Context& c) {
    std::string error;
    const Fd dir = open_state_dir(c, error);
    if (dir.ok()) {
        ::unlinkat(dir.get(), "acting.stamp", 0);
    }
}

long seconds_since_acting(const Context& c) {
    std::string error;
    const Fd dir = open_state_dir(c, error);
    if (!dir.ok()) {
        return -1;
    }
    const int raw = ::openat(dir.get(), "acting.stamp", O_RDONLY | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC);
    if (raw < 0) {
        return -1;
    }
    const Fd file(raw);
    struct stat st{};
    char buffer[64];
    if (::fstat(file.get(), &st) != 0 || !S_ISREG(st.st_mode) || st.st_uid != ::geteuid()) {
        return -1;
    }
    const ssize_t n = ::read(file.get(), buffer, sizeof buffer - 1);
    if (n <= 0) {
        return -1;
    }
    buffer[n] = '\0';
    char* end = nullptr;
    const long long then = std::strtoll(buffer, &end, 10);
    if (end == buffer) {
        return -1;
    }
    const long long now = static_cast<long long>(::time(nullptr));
    return now >= then ? static_cast<long>(now - then) : 0;
}

PendingUpdate load_pending(const Context& c, std::string& error, bool* damaged) {
    if (damaged != nullptr) {
        *damaged = false;
    }
    const Fd dir = open_state_dir(c, error);
    if (!dir.ok()) {
        return {};
    }
    FileLimits limits;
    limits.max_bytes = kMaxStateBytes;
    limits.check_owner = true;
    limits.owner = ::geteuid(); // root's, in the helper
    PlainFile file = open_plain_file(dir.get(), kStateFile, limits);
    if (file.status == OpenStatus::Missing) {
        return {};
    }
    std::string text;
    std::string why;
    if (file.status != OpenStatus::Ok || !read_all_text(file.fd.get(), kMaxStateBytes, text, why)) {
        error = std::string("the state file could not be read: ") +
                (file.status != OpenStatus::Ok ? open_status_text(file.status) : why.c_str());
        ::renameat(dir.get(), kStateFile, dir.get(), "state.unreadable");
        if (damaged != nullptr) {
            *damaged = true;
        }
        return {};
    }
    const PendingParse parsed = parse_pending_update(text);
    if (!parsed.ok) {
        error = "the state file is not valid: " + parsed.error;
        ::renameat(dir.get(), kStateFile, dir.get(), "state.unreadable");
        if (damaged != nullptr) {
            *damaged = true;
        }
        return {};
    }
    return parsed.state;
}

PendingUpdate peek_pending(const Context& c, bool* readable) {
    std::string error;
    if (readable != nullptr) {
        *readable = true;
    }
    const Fd dir = open_state_dir(c, error);
    if (!dir.ok()) {
        return {};
    }
    FileLimits limits;
    limits.max_bytes = kMaxStateBytes;
    limits.check_owner = true;
    limits.owner = ::geteuid();
    PlainFile file = open_plain_file(dir.get(), kStateFile, limits);
    if (file.status == OpenStatus::Missing) {
        return {};
    }
    std::string text;
    if (file.status != OpenStatus::Ok || !read_all_text(file.fd.get(), kMaxStateBytes, text, error)) {
        if (readable != nullptr) {
            *readable = false;
        }
        return {};
    }
    const PendingParse parsed = parse_pending_update(text);
    if (!parsed.ok && readable != nullptr) {
        *readable = false;
    }
    return parsed.ok ? parsed.state : PendingUpdate{};
}

bool save_pending(const Context& c, const PendingUpdate& state, std::string& error) {
#if defined(ETUDE_HELPER_TEST_HOOKS)
    if (g_fail_save_in > 0 && --g_fail_save_in == 0) {
        error = "a save that a test made fail";
        return false;
    }
#endif
    const Fd dir = open_state_dir(c, error);
    return dir.ok() && write_file_atomic(dir.get(), kStateFile, format_pending_update(state), 0600, error);
}

} // namespace etude::helper
