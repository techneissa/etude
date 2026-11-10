// SPDX-License-Identifier: GPL-3.0-or-later
// Crash reports: a real crash writes one, and it holds no home path and no profile name.
#include "crash_handler.hpp"
#include "crash_notice.hpp"
#include "crash_thread.hpp"
#include "log_ring.hpp"
#include "upload_test_support.hpp"

#include "etude/crash_report.hpp"

#include <doctest/doctest.h>

#include <csignal>
#include <cstdint>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <sys/resource.h>
#include <sys/wait.h>
#include <thread>
#include <unistd.h>
#include <vector>

using namespace etude;
using namespace etude::app;
using namespace upload_test;

namespace {

ScrubRules rules_for(const fs::path& data) {
    ScrubRules r;
    r.home = "/home/pat";
    r.app_dirs = {data.string(), "/opt/app"};
    r.names = {"Mia"};
    return r;
}

std::string name_at(std::int64_t t) {
    char out[kCrashNameChars];
    crash_report_name(t, out);
    return out;
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
    set_log_scrub(rules_for("/data/etude"));
    log_ring_clear();
    set_crash_build("1.2.0", "Linux 6.12 aarch64");
    set_crash_screen("playing a song");
    set_crash_song("Moonlight Sonata");
}

// A child that installs the handler and crashes; its wait status.
int crash_in_child(const fs::path& dir, const std::function<void()>& crash) {
    const pid_t pid = ::fork();
    if (pid == 0) {
        rlimit none{0, 0};
        ::setrlimit(RLIMIT_CORE, &none);
        fill_buffers();
        install_crash_handler(dir.parent_path());
        crash();
        ::_exit(0);
    }
    int status = 0;
    ::waitpid(pid, &status, 0);
    return status;
}

volatile int g_depth_limit = 1 << 30; // never reached: the stack is gone first

[[gnu::noinline]] int recurse(int depth) {
    volatile char frame[4096];
    frame[0] = static_cast<char>(depth);
    return depth < g_depth_limit ? frame[0] + recurse(depth + 1) + frame[1] : 0;
}

const char* kText =
    "etude crash report\nVersion: 1.2.0\nOS: Linux 6.12 aarch64\nScreen: free play\nSong: \n"
    "Signal: SIGSEGV (11)\nTime (UTC): 20261020-153012\n\nStack trace:\n"
    "/home/pat/build/etude(+0x1a2b) [0x55aa]\n"
    "/opt/app/bin/etude(_ZN5etude3app5Scene4drawEv+0x14) [0x55ab]\n\n"
    "Log (the last lines, the newest last):\nopened /home/pat/Music/x.mid\nswitched to Mia\n";

void write_report(const fs::path& dir, const std::string& name) {
    fs::create_directories(dir / kCrashFolder);
    std::ofstream(dir / kCrashFolder / name, std::ios::binary) << kText;
}

} // namespace

TEST_CASE("crash: the log keeps the last 200 lines with paths and names scrubbed") {
    log_ring_clear();
    set_log_scrub(rules_for("/data/etude"));
    for (int i = 0; i < 250; ++i) {
        log_ring_add("line " + std::to_string(i) + " read /home/pat/Music/x.mid for Mia\n");
    }
    const std::vector<std::string> lines = log_ring_lines();
    REQUIRE(lines.size() == kLogRingLines);
    CHECK(lines.front() == "line 50 read <path> for <profile>");
    CHECK(lines.back() == "line 249 read <path> for <profile>");
}

TEST_CASE("crash: a real signal, an uncaught exception and a stack overflow each write a report") {
    struct Crash {
        const char* name;
        std::function<void()> run;
        int signo; // 0: either overflow signal
    };
    const Crash crashes[] = {
        {"signal", [] { ::raise(SIGSEGV); }, SIGSEGV},
        {"exception", [] { std::thread([] { throw std::runtime_error("secret message"); }).join(); }, SIGABRT},
        {"overflow", [] { spawn_thread([] { recurse(0); }).join(); }, 0},
    };
    for (const Crash& c : crashes) {
        CAPTURE(c.name);
        TempDir t;
        const fs::path dir = t.path / "crashes";
        const int status = crash_in_child(dir, c.run);
        REQUIRE(WIFSIGNALED(status)); // still dies of it, so systemd counts the crash
        if (c.signo != 0) {
            CHECK(WTERMSIG(status) == c.signo);
        }
        const std::string text = only_report(dir);
        REQUIRE_FALSE(text.empty());
        CHECK(text.find("Version: 1.2.0\n") != std::string::npos);
        CHECK(text.find("secret message") == std::string::npos); // the type, never the message
    }
    TempDir t;
    fill_buffers();
    CHECK_FALSE(write_crash_report((t.path / "missing").c_str(), SIGSEGV, nullptr)); // no folder, no report
}

TEST_CASE("crash: the home folder becomes ~, other paths <path>, profile names <profile>") {
    ScrubRules r;
    r.home = "/home/pat";
    r.app_dirs = {"/home/pat/.local/share/etude", "/opt/etude/versions/1.2.0"};
    r.names = {"Mia"};
    CHECK(scrub_line("saved /home/pat/.local/share/etude/a.mid ok", r) == "saved ~/.local/share/etude/a.mid ok");
    CHECK(scrub_line("/opt/etude/versions/1.2.0/x.png", r) == "/opt/etude/versions/1.2.0/x.png");
    CHECK(scrub_line("open /home/pat/Music/x.mid failed", r) == "open <path> failed");
    CHECK(scrub_line("/opt/etude/versions/1.2.0-old/x", r) == "<path>"); // only looks like an app folder
    CHECK(scrub_line("open /Volumes/My Stick/x.mid failed", r) == "open <path>"); // a space ends nothing
    CHECK(scrub_line("see https://github.com/x/y and a/b", r) == "see https://github.com/x/y and a/b");
    CHECK(scrub_line("Mia's run, Miami", r) == "<profile>'s run, Miami"); // whole word only
}

TEST_CASE("crash: the newest ten reports are kept, and the one told holds no outside path") {
    TempDir t;
    for (int i = 0; i < 14; ++i) {
        write_report(t.path, name_at(1000 + i));
    }
    const auto notice = start_crash_notice(t.path, rules_for(t.path));
    REQUIRE(notice.has_value());
    CHECK(notice->name == name_at(1013));
    int left = 0;
    for (const auto& entry : fs::directory_iterator(t.path / kCrashFolder)) {
        left += is_crash_report_name(entry.path().filename().string()) ? 1 : 0;
    }
    CHECK(left == 10);
    CHECK(notice->text.find("/home/pat") == std::string::npos);
    CHECK(notice->text.find("/opt/app/bin/etude") != std::string::npos); // the app's own folder stays
    CHECK(read_file(t.path / kCrashFolder / name_at(1013)) == notice->text); // the file is clean too
}

TEST_CASE("crash: profile names are scrubbed only once the history has loaded") {
    TempDir t;
    write_report(t.path, name_at(1000));
    ScrubRules rules = rules_for(t.path);
    rules.names.clear();
    CrashNoticeStart start;
    start.defer(t.path, false);
    CHECK_FALSE(start.poll(false, rules).has_value());
    CHECK_FALSE(fs::exists(t.path / kCrashFolder / kCrashSeenFile)); // not marked told yet
    rules.names = {"Mia"};
    const auto notice = start.poll(true, rules);
    REQUIRE(notice.has_value());
    CHECK(notice->text.find("Mia") == std::string::npos);
    CHECK(read_file(t.path / kCrashFolder / name_at(1000)).find("Mia") == std::string::npos);
}
