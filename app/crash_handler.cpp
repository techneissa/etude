// SPDX-License-Identifier: GPL-3.0-or-later
#include "crash_handler.hpp"

#include "log_ring.hpp"

#include "etude/crash_report.hpp"

#include <cxxabi.h>
#include <execinfo.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>
#include <sys/utsname.h>
#include <unistd.h>

#include <atomic>
#include <cstdlib>
#include <ctime>
#include <cstring>
#include <exception>
#include <stdexcept>
#include <string>
#include <typeinfo>

namespace etude::app {

namespace {

// One line of text the app keeps current and the handler reads. Two slots and an index, so the
// handler never reads a line that is half written.
template <std::size_t N> class FixedLine {
  public:
    void set(std::string_view text) {
        const std::size_t next = 1 - current_.load(std::memory_order_acquire);
        std::size_t n = 0;
        for (const char c : text) {
            if (n + 1 >= N) {
                break;
            }
            slots_[next][n++] = (c == '\n' || c == '\r') ? ' ' : c;
        }
        slots_[next][n] = '\0';
        current_.store(next, std::memory_order_release);
    }
    const char* get() const { return slots_[current_.load(std::memory_order_acquire)]; }

  private:
    char slots_[2][N] = {};
    std::atomic<std::size_t> current_{0};
};

FixedLine<48> g_version;
FixedLine<96> g_os;
FixedLine<96> g_screen;
FixedLine<160> g_song;

char g_dir[1024];                // `<data>/crashes`, set at the start
std::atomic<bool> g_hang_for_test{false}; // the test of the alarm: the trace never comes back
std::atomic<bool> g_writing{false};

// Output without stdio: each piece is one `write`.
void put(int fd, const char* text) {
    std::size_t n = 0;
    while (text[n] != '\0') {
        ++n;
    }
    while (n > 0) {
        const ssize_t done = ::write(fd, text, n);
        if (done <= 0) {
            return;
        }
        text += done;
        n -= static_cast<std::size_t>(done);
    }
}

void put_int(int fd, long value) {
    char digits[24];
    int at = 23;
    digits[at] = '\0';
    const bool negative = value < 0;
    unsigned long v = negative ? 0UL - static_cast<unsigned long>(value) : static_cast<unsigned long>(value);
    do {
        digits[--at] = static_cast<char>('0' + v % 10);
        v /= 10;
    } while (v != 0 && at > 1);
    if (negative) {
        digits[--at] = '-';
    }
    put(fd, digits + at);
}

void put_field(int fd, const char* label, const char* value) {
    put(fd, label);
    put(fd, value);
    put(fd, "\n");
}

// The trace asks the loader and the unwinder for their locks, which another thread may hold when
// it crashes: then nothing would ever end the process and nothing would restart it. The alarm ends
// it after kCrashAlarmSeconds (SIGALRM kept at its default action, so it kills), report or not.
void arm_alarm() {
    ::signal(SIGALRM, SIG_DFL);
    ::alarm(kCrashAlarmSeconds);
}

const char* const kFatal[] = {"SIGSEGV", "SIGBUS", "SIGILL", "SIGFPE", "SIGABRT"};
const int kFatalNumbers[] = {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT};

} // namespace

const char* crash_signal_name(int signo) {
    for (std::size_t i = 0; i < 5; ++i) {
        if (kFatalNumbers[i] == signo) {
            return kFatal[i];
        }
    }
    return "signal";
}

void set_crash_build(std::string_view version, std::string_view os) {
    g_version.set(version);
    g_os.set(os);
}
void set_crash_screen(std::string_view screen) { g_screen.set(screen); }
void set_crash_song(std::string_view title) { g_song.set(title); }

std::string os_description() {
    utsname u{};
    if (::uname(&u) != 0) {
        return "unknown";
    }
    const std::string sys = u.sysname;
    if (sys == "Darwin") {
        return "macOS (Darwin " + std::string(u.release) + ") " + u.machine;
    }
    return sys + " " + u.release + " " + u.machine;
}

bool write_crash_report(const char* dir, int signo, const char* detail) {
    char name[etude::kCrashNameChars];
    etude::crash_report_name(static_cast<std::int64_t>(::time(nullptr)), name);
    char path[1100];
    std::size_t n = 0;
    for (std::size_t i = 0; dir[i] != '\0' && n < sizeof path - 32; ++i) {
        path[n++] = dir[i];
    }
    path[n++] = '/';
    for (std::size_t i = 0; name[i] != '\0'; ++i) {
        path[n++] = name[i];
    }
    path[n] = '\0';
    const int fd = ::open(path, O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd < 0) {
        return false;
    }
    put(fd, "etude crash report\n");
    put_field(fd, "Version: ", g_version.get());
    put_field(fd, "OS: ", g_os.get());
    put_field(fd, "Screen: ", g_screen.get());
    put_field(fd, "Song: ", g_song.get());
    put(fd, "Signal: ");
    if (signo == 0) {
        put(fd, "uncaught C++ exception");
        if (detail != nullptr && detail[0] != '\0') {
            put(fd, " (");
            put(fd, detail);
            put(fd, ")");
        }
    } else {
        put(fd, crash_signal_name(signo));
        put(fd, " (");
        put_int(fd, signo);
        put(fd, ")");
    }
    put(fd, "\nTime (UTC): ");
    name[15] = '\0'; // the name without ".txt"
    put(fd, name);
    put(fd, "\n\nStack trace:\n");
    while (g_hang_for_test.load(std::memory_order_relaxed)) {
        ::pause(); // a test of the alarm: the header is written, the trace never is
    }
    void* frames[64];
    const int count = ::backtrace(frames, 64);
    ::backtrace_symbols_fd(frames, count, fd);
    put(fd, "\nLog (the last lines, the newest last):\n");
    log_ring_dump(fd);
    ::close(fd);
    return true;
}

extern "C" void etude_crash_signal(int signo, siginfo_t* /*info*/, void* /*context*/) {
    arm_alarm();
    if (!g_writing.exchange(true)) {
        write_crash_report(g_dir, signo, nullptr);
    }
    // The default action again: the process dies of the same signal, so systemd sees it.
    ::signal(signo, SIG_DFL);
    ::raise(signo);
}

namespace {

[[noreturn]] void on_terminate() {
    arm_alarm();
    char detail[160] = "";
    if (const std::type_info* type = abi::__cxa_current_exception_type()) {
        int status = 0;
        char* readable = abi::__cxa_demangle(type->name(), nullptr, nullptr, &status);
        std::strncpy(detail, readable != nullptr && status == 0 ? readable : type->name(),
                     sizeof detail - 1);
        std::free(readable);
    }
    if (!g_writing.exchange(true)) {
        write_crash_report(g_dir, 0, detail);
    }
    ::signal(SIGABRT, SIG_DFL);
    std::abort();
}

} // namespace

namespace {

constexpr std::size_t kAltStackBytes = 64 * 1024;

// The thread's alternate stack: made by the thread, given back when it ends.
struct AltStack {
    void* memory = nullptr;
    ~AltStack() {
        if (memory != nullptr) {
            stack_t off{};
            off.ss_flags = SS_DISABLE;
            ::sigaltstack(&off, nullptr);
            ::munmap(memory, kAltStackBytes);
        }
    }
};

} // namespace

void crash_thread_init() {
    thread_local AltStack mine;
    if (mine.memory != nullptr) {
        return;
    }
    void* memory = ::mmap(nullptr, kAltStackBytes, PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANON, -1, 0);
    if (memory == MAP_FAILED) {
        return; // no report for a stack overflow of this thread; every other crash still has one
    }
    stack_t alt{};
    alt.ss_sp = memory;
    alt.ss_size = kAltStackBytes;
    if (::sigaltstack(&alt, nullptr) == 0) {
        mine.memory = memory;
    } else {
        ::munmap(memory, kAltStackBytes);
    }
}

void crash_test_hang_in_trace(bool on) { g_hang_for_test.store(on, std::memory_order_relaxed); }

void install_crash_handler(const std::filesystem::path& data_dir) {
    const std::filesystem::path dir = data_dir / "crashes";
    std::error_code ignored;
    std::filesystem::create_directories(dir, ignored);
    const std::string text = dir.string();
    if (text.size() >= sizeof g_dir) {
        return; // too long a path to keep: no reports, nothing else breaks
    }
    std::memcpy(g_dir, text.c_str(), text.size() + 1);

    // The first call of backtrace loads the unwinder, which allocates: do it now, with its
    // symbol writer, into nowhere.
    void* warm[8];
    const int count = ::backtrace(warm, 8);
    const int sink = ::open("/dev/null", O_WRONLY);
    if (sink >= 0) {
        ::backtrace_symbols_fd(warm, count, sink);
        ::close(sink);
    }

    crash_thread_init();
    struct sigaction action{};
    action.sa_sigaction = etude_crash_signal;
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND;
    sigemptyset(&action.sa_mask);
    for (const int signo : kFatalNumbers) {
        ::sigaction(signo, &action, nullptr);
    }
    std::set_terminate(on_terminate);
}

bool crash_on_purpose(std::string_view how) {
    if (how == "segv") {
        volatile int* nothing = nullptr;
        *nothing = 1;
    } else if (how == "bus") {
        ::raise(SIGBUS);
    } else if (how == "ill") {
        ::raise(SIGILL);
    } else if (how == "fpe") {
        ::raise(SIGFPE);
    } else if (how == "abort") {
        std::abort();
    } else if (how == "throw") {
        throw std::runtime_error("crash test");
    } else {
        return false;
    }
    return true;
}

} // namespace etude::app
