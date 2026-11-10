// SPDX-License-Identifier: GPL-3.0-or-later
// The crash handler: the report writer fills one file from the buffers the app keeps and the log
// ring, with no signal; a real signal in a child process writes it too and the child still dies of
// that signal; the ring keeps the last 200 lines with their paths scrubbed.
#include "crash_handler.hpp"
#include "crash_thread.hpp"
#include "log_ring.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <csignal>
#include <stdexcept>
#include <string>
#include <thread>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

ScrubRules test_rules() {
    ScrubRules r;
    r.home = "/home/pat";
    r.app_dirs = {"/data/etude"};
    r.names = {"Mia"};
    return r;
}

// The one report in `dir`, empty when there is none.
std::string only_report(const fs::path& dir) {
    std::string found;
    int n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir, ec), end; !ec && it != end; it.increment(ec)) {
        if (is_crash_report_name(it->path().filename().string())) {
            found = read_file(it->path());
            ++n;
        }
    }
    return n == 1 ? found : std::string();
}

void fill_buffers() {
    set_log_scrub(test_rules());
    log_ring_clear();
    set_crash_build("1.2.0", "Linux 6.12 aarch64");
    set_crash_screen("playing a song");
    set_crash_song("Moonlight Sonata");
}

} // namespace

TEST_CASE("crash handler: the log ring keeps the last 200 lines, their paths scrubbed on the way in") {
    log_ring_clear();
    set_log_scrub(test_rules());
    for (int i = 0; i < 250; ++i) {
        log_ring_add("line " + std::to_string(i) + " read /home/pat/Music/x.mid for Mia\n");
    }
    const std::vector<std::string> lines = log_ring_lines();
    REQUIRE(lines.size() == kLogRingLines);
    CHECK(lines.front() == "line 50 read <path> for <profile>");
    CHECK(lines.back() == "line 249 read <path> for <profile>");
    log_ring_add(std::string(1000, 'x')); // a long line is cut, not overrun
    CHECK(log_ring_lines().back().size() == kLogRingLineChars - 1);
    log_ring_clear();
    CHECK(log_ring_lines().empty());
}

TEST_CASE("crash handler: the report writer fills the file from the buffers and the ring") {
    TempDir t;
    fill_buffers();
    set_crash_screen("settings\nwith a line end"); // one line each: the end of line becomes a space
    for (int i = 0; i < 230; ++i) {
        log_ring_add("log line " + std::to_string(i));
    }
    REQUIRE(write_crash_report(t.path.c_str(), SIGSEGV, nullptr));
    const std::string text = only_report(t.path);
    REQUIRE_FALSE(text.empty());
    CHECK(text.rfind("etude crash report\n", 0) == 0);
    CHECK(text.find("\nVersion: 1.2.0\n") != std::string::npos);
    CHECK(text.find("\nOS: Linux 6.12 aarch64\n") != std::string::npos);
    CHECK(text.find("\nScreen: settings with a line end\n") != std::string::npos);
    CHECK(text.find("\nSong: Moonlight Sonata\n") != std::string::npos);
    CHECK(text.find("\nSignal: SIGSEGV (11)\n") != std::string::npos);
    CHECK(text.find("\nStack trace:\n") != std::string::npos);
    // The newest 200 lines of the log, in order, the older ones gone.
    CHECK(text.find("\nlog line 30\n") != std::string::npos);
    CHECK(text.find("\nlog line 229\n") != std::string::npos);
    CHECK(text.find("\nlog line 29\n") == std::string::npos);
    CHECK(text.find("log line 30\n") < text.find("log line 229\n"));
    // What the notice reads back.
    const CrashSummary s = summarize_crash(text);
    CHECK(s.version == "1.2.0");
    CHECK(s.signal == "SIGSEGV (11)");
    CHECK(s.screen == "settings with a line end");
    CHECK_FALSE(s.frames.empty());
}

TEST_CASE("crash handler: an uncaught exception is told by its type, no message") {
    TempDir t;
    fill_buffers();
    REQUIRE(write_crash_report(t.path.c_str(), 0, "std::runtime_error"));
    const std::string text = only_report(t.path);
    CHECK(text.find("\nSignal: uncaught C++ exception (std::runtime_error)\n") != std::string::npos);
}

TEST_CASE("crash handler: no folder, no report, no crash of its own") {
    TempDir t;
    fill_buffers();
    CHECK_FALSE(write_crash_report((t.path / "missing").c_str(), SIGSEGV, nullptr));
}

namespace {

enum class Kind { Signal, Throw };

// A child that installs the handler and crashes; its status, and the report it left.
int crash_in_child(const fs::path& dir, Kind kind, int signo) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        rlimit none{0, 0};
        ::setrlimit(RLIMIT_CORE, &none); // no core file for a crash made on purpose
        fill_buffers();
        install_crash_handler(dir.parent_path());
        if (kind == Kind::Throw) {
            // Out of a thread's function nothing can catch it (doctest would): std::terminate runs.
            std::thread([] { throw std::runtime_error("secret message"); }).join();
        }
        ::raise(signo);
        ::_exit(0); // not reached: the signal ends the child
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return status;
}

} // namespace

TEST_CASE("crash handler: a real signal writes the report and the process still dies of it") {
    for (const int signo : {SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT}) {
        CAPTURE(signo);
        TempDir t;
        const fs::path dir = t.path / "crashes";
        const int status = crash_in_child(dir, Kind::Signal, signo);
        // Default action again: systemd sees the signal and counts the crash.
        REQUIRE(WIFSIGNALED(status));
        CHECK(WTERMSIG(status) == signo);
        const std::string text = only_report(dir);
        REQUIRE_FALSE(text.empty());
        CHECK(text.find("Signal: " + std::string(crash_signal_name(signo)) + " (" +
                        std::to_string(signo) + ")\n") != std::string::npos);
        CHECK(text.find("Version: 1.2.0\n") != std::string::npos);
        CHECK(text.find("Song: Moonlight Sonata\n") != std::string::npos);
    }
}

TEST_CASE("crash handler: an uncaught exception writes the report and ends in abort") {
    TempDir t;
    const fs::path dir = t.path / "crashes";
    const int status = crash_in_child(dir, Kind::Throw, 0);
    REQUIRE(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGABRT);
    const std::string text = only_report(dir);
    REQUIRE_FALSE(text.empty());
    CHECK(text.find("Signal: uncaught C++ exception (std::runtime_error)\n") != std::string::npos);
    CHECK(text.find("secret message") == std::string::npos); // the type, never the message
}

namespace {

bool has_alt_stack() {
    stack_t now{};
    return ::sigaltstack(nullptr, &now) == 0 && (now.ss_flags & SS_DISABLE) == 0 && now.ss_sp != nullptr;
}

volatile int g_depth_limit = 1 << 30; // never reached: the stack is gone first

[[gnu::noinline]] int recurse(int depth) {
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    return depth < g_depth_limit ? frame[0] + recurse(depth + 1) + frame[1] : 0;
}

} // namespace

TEST_CASE("crash handler: a thread has an alternate stack when it asks for one, and not before") {
    bool plain = true;
    bool asked = false;
    bool started = false;
    std::thread([&] { plain = has_alt_stack(); }).join();
    std::thread([&] {
        crash_thread_init();
        crash_thread_init(); // twice is the same stack
        asked = has_alt_stack();
    }).join();
    spawn_thread([&] { started = has_alt_stack(); }).join();
    CHECK_FALSE(plain);
    CHECK(asked);
    CHECK(started);
}

TEST_CASE("crash handler: a stack overflow on a worker thread still leaves a report") {
    TempDir t;
    const fs::path dir = t.path / "crashes";
    const pid_t pid = ::fork();
    if (pid == 0) {
        rlimit none{0, 0};
        ::setrlimit(RLIMIT_CORE, &none);
        fill_buffers();
        install_crash_handler(t.path);
        spawn_thread([] { recurse(0); }).join();
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    REQUIRE(WIFSIGNALED(status));
    CHECK((WTERMSIG(status) == SIGSEGV || WTERMSIG(status) == SIGBUS));
    const std::string text = only_report(dir);
    REQUIRE_FALSE(text.empty());
    CHECK(text.find("Version: 1.2.0\n") != std::string::npos);
    CHECK(text.find("Stack trace:\n") != std::string::npos);
}
