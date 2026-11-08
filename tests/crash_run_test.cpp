// SPDX-License-Identifier: GPL-3.0-or-later
// The whole path, with the built app: `--crash-test segv` crashes it on purpose; the process dies of
// the signal (so a supervisor sees it), the report is in the data folder, the next normal start
// tells of it once, and a start after "Restart and send report" opens the report's sheet at once.
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#ifdef ETUDE_APP_PATH
#include <fcntl.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

#include <chrono>
#include <fstream>
#include <cstdlib>
#include <string>
#include <vector>

using namespace upload_test;

namespace {

// What a start says once it has settled the notice, and what it says when the crash screen asked for
// the report's sheet.
constexpr const char* kTold = "the last run closed unexpectedly: report ";
constexpr const char* kNothing = "no crash report to tell";
constexpr const char* kSheet = "the crash report's sheet opens";

bool has_line(const fs::path& log, const std::vector<std::string>& marks) {
    const std::string text = read_file(log);
    for (const std::string& mark : marks) {
        if (text.find(mark) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// The app on dummy drivers (a machine with no screen or sound card runs it too), its standard error
// in `log`. It runs until it ends by itself (a crash) or, once it says one of `until` in its log,
// is asked to quit: its own words, never a count of frames, say it got far enough. The wait status.
int run_app(const fs::path& dir, const std::vector<std::string>& flags, const fs::path& log,
            const std::vector<std::string>& until = {}) {
    // Emptied here, before the child runs, so the last run's words are never taken for this one's.
    std::ofstream(log, std::ios::trunc).close();
    const pid_t pid = ::fork();
    if (pid == 0) {
        ::setenv("SDL_VIDEODRIVER", "dummy", 1);
        ::setenv("SDL_AUDIODRIVER", "dummy", 1);
        const int out = ::open("/dev/null", O_WRONLY);
        const int err = ::open(log.c_str(), O_WRONLY | O_APPEND, 0644);
        ::dup2(out, 1);
        ::dup2(err, 2);
        std::vector<std::string> args = {ETUDE_APP_PATH, "--headless", "--window", "800x480",
                                         "--settings-dir", dir.string()};
        args.insert(args.end(), flags.begin(), flags.end());
        std::vector<char*> argv;
        for (std::string& a : args) {
            argv.push_back(a.data());
        }
        argv.push_back(nullptr);
        ::execv(ETUDE_APP_PATH, argv.data());
        ::_exit(127);
    }
    using Clock = std::chrono::steady_clock;
    const auto began = Clock::now();
    const auto since = [&] { return std::chrono::duration<double>(Clock::now() - began).count(); };
    int status = 0;
    bool asked = false;
    double asked_at = 0.0;
    while (::waitpid(pid, &status, WNOHANG) == 0) {
        if (!asked && !until.empty() && has_line(log, until)) {
            ::kill(pid, SIGTERM);
            asked = true;
            asked_at = since();
        }
        if (since() > 60.0 || (asked && since() - asked_at > 10.0)) {
            ::kill(pid, SIGKILL); // never settled: the checks below say so
        }
        ::usleep(20'000);
    }
    return status;
}

bool ended_well(int status) { return WIFEXITED(status) ? WEXITSTATUS(status) == 0 : WTERMSIG(status) == SIGTERM; }

std::string only_report_name(const fs::path& dir) {
    std::string found;
    int n = 0;
    std::error_code ec;
    for (fs::directory_iterator it(dir / "crashes", ec), end; !ec && it != end; it.increment(ec)) {
        const std::string name = it->path().filename().string();
        if (name.size() > 4 && name.substr(name.size() - 4) == ".txt") {
            found = name;
            ++n;
        }
    }
    return n == 1 ? found : std::string();
}

} // namespace

TEST_CASE("crash run: the app dies of the signal, leaves a report, and the next start tells of it once") {
    TempDir t;
    const fs::path dir = t.path / "data";
    const fs::path log = t.path / "stderr.txt";

    const int status = run_app(dir, {"--crash-test", "segv"}, log);
    REQUIRE(WIFSIGNALED(status));
    CHECK(WTERMSIG(status) == SIGSEGV); // the default action ran again
    const std::string name = only_report_name(dir);
    REQUIRE_FALSE(name.empty());
    const std::string report = read_file(dir / "crashes" / name);
    CHECK(report.rfind("etude crash report\n", 0) == 0);
    CHECK(report.find("\nSignal: SIGSEGV (11)\n") != std::string::npos);
    CHECK(report.find("\nVersion: " ETUDE_PROJECT_VERSION "\n") != std::string::npos);
    CHECK(report.find("\nStack trace:\n") != std::string::npos);
    CHECK(report.find("crashing on purpose (segv)") != std::string::npos); // the log ring is in it
    CHECK(report.find(dir.string()) == std::string::npos); // no folder of this machine, only `~` or <path>

    // The next normal start says so, and names the report.
    const int next = run_app(dir, {}, log, {kTold, kNothing});
    const std::string told = read_file(log);
    INFO("stderr of the next start:\n" << told);
    CHECK(ended_well(next));
    CHECK(told.find(kTold + name) != std::string::npos);

    // And only once.
    const int again = run_app(dir, {}, log, {kTold, kNothing});
    INFO("stderr of the start after that:\n" << read_file(log));
    CHECK(ended_well(again));
    CHECK(read_file(log).find(kNothing) != std::string::npos); // it looked, and had nothing to tell
    CHECK(read_file(log).find("closed unexpectedly") == std::string::npos);
}

TEST_CASE("crash run: a start after Restart and send report opens the report's sheet, once") {
    TempDir t;
    const fs::path dir = t.path / "data";
    const fs::path log = t.path / "stderr.txt";
    const int crashed = run_app(dir, {"--crash-test", "abort"}, log);
    REQUIRE(WIFSIGNALED(crashed));
    const std::string name = only_report_name(dir);
    REQUIRE_FALSE(name.empty());

    // What the crash screen's second button leaves.
    write_file(dir / "crashes" / "send-report", "send\n");
    const int sent = run_app(dir, {}, log, {kSheet});
    INFO("stderr of the start:\n" << read_file(log));
    CHECK(ended_well(sent));
    CHECK(read_file(log).find(kTold + name) != std::string::npos);
    CHECK(read_file(log).find(kSheet) != std::string::npos);
    CHECK_FALSE(fs::exists(dir / "crashes" / "send-report")); // taken

    // The start after that is plain.
    const int plain = run_app(dir, {}, log, {kTold, kNothing});
    CHECK(ended_well(plain));
    CHECK(read_file(log).find(kSheet) == std::string::npos);
}
#endif
