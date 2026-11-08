// SPDX-License-Identifier: GPL-3.0-or-later
// The root update helper's sandboxed `count`, `crashed` and `idle` (helper/sandbox): the properties it
// reads, the command it builds, and what the unit ends with for each way the transient run can end, with a
// stand-in systemd-run and systemctl run as the current user. The real thing, under systemd, is
// scripts/update-helper-test.sh.
#include "../helper/context.hpp"
#include "../helper/interrupt.hpp"
#include "../helper/sandbox.hpp"
#include "../helper/settle.hpp"
#include "upload_test_support.hpp"

#include <doctest/doctest.h>

#include <sys/stat.h>
#include <unistd.h>

#include <chrono>
#include <fstream>

using namespace etude;
using namespace etude::helper;
using namespace upload_test;

namespace {

void make_executable(const fs::path& path, const std::string& body) {
    write_file(path, body);
    ::chmod(path.c_str(), 0755);
}

// A stand-in systemd-run that ends with `status` (and says something on the way), and a systemctl that
// logs its calls.
struct Rig {
    TempDir root;
    Context c;
    Rig() {
        c.conf.systemctl = (root.path / "systemctl").string();
        make_executable(c.conf.systemctl, "#!/bin/sh\necho \"$*\" >>\"" + (root.path / "calls").string() + "\"\n");
        reset_interrupt();
    }
    // `body` is what the stand-in systemd-run does after the program it starts has said it began (the
    // marker), unless `started` is false: systemd-run that never got as far as starting it.
    SandboxRun run(const std::string& mode, const std::string& body, bool started = true) {
        const std::string marker = started ? "echo '" + std::string(kSandboxStartMarker) + "'\n" : std::string();
        make_executable(root.path / "systemd-run", "#!/bin/sh\n" + marker + body + "\n");
        SandboxRun r;
        r.systemd_run = (root.path / "systemd-run").string();
        r.helper = "/usr/local/lib/etude/etude-update-helper";
        r.mode = mode;
        r.unit = "etude-helper-" + mode + "-1";
        r.limit_s = 30;
        r.properties = {"ProtectSystem=strict", "NoNewPrivileges=yes"};
        return r;
    }
    std::string calls() { return fs::exists(root.path / "calls") ? read_file(root.path / "calls") : std::string(); }
};

} // namespace

TEST_CASE("sandbox: the properties are the lines of a drop-in, and nothing else") {
    TempDir t;
    std::vector<std::string> properties;
    std::string error;
    write_file(t.path / "s.conf",
               "# comment\n[Service]\nProtectSystem=strict\n\n  ReadWritePaths=/opt/etude /var/lib/x -/home/a b\r\n"
               "CapabilityBoundingSet=CAP_KILL\n");
    ::chmod((t.path / "s.conf").c_str(), 0644);
    REQUIRE(load_sandbox((t.path / "s.conf").string(), properties, error, ::geteuid()));
    CHECK(properties == std::vector<std::string>{"ProtectSystem=strict", "ReadWritePaths=/opt/etude /var/lib/x -/home/a b",
                                                 "CapabilityBoundingSet=CAP_KILL"});
}

TEST_CASE("sandbox: a file that is not plain data, or not root's alone, is refused") {
    TempDir t;
    std::vector<std::string> properties;
    std::string error;
    const auto load = [&](const std::string& text, mode_t mode = 0644, uid_t owner = ::geteuid()) {
        write_file(t.path / "s.conf", text);
        ::chmod((t.path / "s.conf").c_str(), mode);
        return load_sandbox((t.path / "s.conf").string(), properties, error, owner);
    };
    CHECK(load("ProtectSystem=strict\n"));
    CHECK_FALSE(load("ProtectSystem=strict\nrm -rf /\n"));
    CHECK_FALSE(load("ProtectSystem=strict\n$(id)=1\n"));
    CHECK_FALSE(load("=strict\n"));
    CHECK_FALSE(load("[Unit]\nProtectSystem=strict\n"));
    CHECK_FALSE(load("# nothing but a comment\n[Service]\n"));
    CHECK_FALSE(load("ProtectSystem=str\x01ict\n"));
    CHECK_FALSE(load("ProtectSystem=strict\n", 0666));                  // anyone may write it
    CHECK_FALSE(load("ProtectSystem=strict\n", 0664));                  // its group may
    CHECK_FALSE(load("ProtectSystem=strict\n", 0644, ::geteuid() + 1)); // another user's
    CHECK_FALSE(load_sandbox((t.path / "missing.conf").string(), properties, error, ::geteuid()));
    std::string many;
    for (int i = 0; i < 100; ++i) {
        many += "Prop" + std::to_string(i) + "=yes\n";
    }
    CHECK_FALSE(load(many));
}

TEST_CASE("sandbox: the command is systemd-run with the properties, the limit and this program again") {
    SandboxRun r;
    r.helper = "/usr/local/lib/etude/etude-update-helper";
    r.mode = "count";
    r.unit = "etude-helper-count-7";
    r.limit_s = 525;
    r.properties = {"ProtectSystem=strict", "ReadWritePaths=/opt/etude /var/lib/etude-update"};
    const std::vector<std::string> argv = sandbox_argv(r);
    CHECK(argv ==
          std::vector<std::string>{"/usr/bin/systemd-run", "--pipe", "--wait", "--collect", "--quiet",
                                   "--unit=etude-helper-count-7", "--property=RuntimeMaxSec=525",
                                   "--property=TimeoutStopSec=10", "--property=ProtectSystem=strict",
                                   "--property=ReadWritePaths=/opt/etude /var/lib/etude-update", "--",
                                   "/usr/local/lib/etude/etude-update-helper", "--report-start", "count"});
    r.mode = "idle";
    r.config = "/etc/x.conf";
    const std::vector<std::string> idle = sandbox_argv(r);
    CHECK(std::vector<std::string>(idle.end() - 6, idle.end()) ==
          std::vector<std::string>{"--config", "/etc/x.conf", "--report-start", "--busy-status", "10", "idle"});
}

TEST_CASE("sandbox: the unit ends with the status of the run: 0, the restore's 75, idle's busy") {
    Rig rig;
    CHECK(run_sandboxed(rig.c, rig.run("count", "echo ran; exit 0")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("count", "exit 75")) == kRestoreUnfinished);
    CHECK(run_sandboxed(rig.c, rig.run("crashed", "exit 0")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("idle", "exit 0")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("idle", "exit 10")) == 1);
    // A status the helper never ends with is its own trouble, and never keeps the app from starting.
    CHECK(run_sandboxed(rig.c, rig.run("count", "exit 5")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("count", "exit 10")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("count", "exit 76")) == 0); // gone: there is no giving up
    CHECK(run_sandboxed(rig.c, rig.run("crashed", "exit 75")) == 0); // the screen shows whatever happens
    CHECK(run_sandboxed(rig.c, rig.run("idle", "exit 75")) == 0);
    CHECK(run_sandboxed(rig.c, rig.run("idle", "kill -9 $$")) == 0);
}

TEST_CASE("sandbox: a systemd-run that did not start the unit sends the caller to do what needs no sandbox") {
    Rig rig;
    SandboxRun gone = rig.run("count", "exit 0");
    gone.systemd_run = (rig.root.path / "no-such-systemd-run").string();
    CHECK_FALSE(run_sandboxed(rig.c, gone).has_value());
    // Not executable at all: the exec fails (127), and the program never said it began.
    write_file(rig.root.path / "not-a-program", "x");
    gone.systemd_run = (rig.root.path / "not-a-program").string();
    CHECK_FALSE(run_sandboxed(rig.c, gone).has_value());
    // The unit would not start: an unknown property or a mount that is refused (1), the set-up of the
    // sandbox failing (226, 203): the program never said it began, so none of it ran.
    for (const char* mode : {"count", "crashed", "idle"}) {
        CAPTURE(mode);
        CHECK_FALSE(run_sandboxed(rig.c, rig.run(mode, "echo 'Unknown assignment: Bogus=1' >&2; exit 1", false)).has_value());
        CHECK_FALSE(run_sandboxed(rig.c, rig.run(mode, "exit 226", false)).has_value());
        CHECK_FALSE(run_sandboxed(rig.c, rig.run(mode, "exit 203", false)).has_value());
        CHECK_FALSE(run_sandboxed(rig.c, rig.run(mode, "echo 'Unit already exists' >&2; exit 1", false)).has_value());
    }
}

TEST_CASE("sandbox: a run that started and then went wrong is not run again in place") {
    Rig rig;
    // systemd-run ends 1 when the unit ran and then crashed, hit RuntimeMaxSec or ran out of memory: the
    // program said it began, so the mode is not repeated without the sandbox, whatever the app left to
    // make it fail. 0, logged, and the next start tries again.
    for (const char* mode : {"count", "crashed", "idle"}) {
        CAPTURE(mode);
        CHECK(run_sandboxed(rig.c, rig.run(mode, "echo 'Process terminated with signal SEGV' >&2; exit 1")) == 0);
        CHECK(run_sandboxed(rig.c, rig.run(mode, "echo 'Failed with result timeout' >&2; exit 1")) == 0);
        CHECK(run_sandboxed(rig.c, rig.run(mode, "exit 137")) == 0);
    }
    // The statuses of the work itself still pass when the run said it began.
    CHECK(run_sandboxed(rig.c, rig.run("count", "exit 75")) == kRestoreUnfinished);
    CHECK(run_sandboxed(rig.c, rig.run("idle", "exit 10")) == 1);
}

TEST_CASE("sandbox: a run that outlasts its limit is stopped by name and counts as the helper's own trouble") {
    Rig rig;
    SandboxRun slow = rig.run("count", "sleep 20");
    slow.limit_s = 0;
    slow.launch_margin_s = 1; // the stand-in outlasts a 1 s wait
    const auto started = std::chrono::steady_clock::now();
    CHECK(run_sandboxed(rig.c, slow) == 0);
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds(15));
    CHECK(rig.calls().find("stop etude-helper-count-1") != std::string::npos);
}

TEST_CASE("sandbox: a stop of the unit while the run goes on stops the transient unit too") {
    Rig rig;
    raise_interrupt();
    CHECK(run_sandboxed(rig.c, rig.run("count", "sleep 20")) == 0);
    CHECK(rig.calls().find("stop etude-helper-count-1") != std::string::npos);
    reset_interrupt();
}

TEST_CASE("sandbox: the limits are the helper's worst case for the mode, and the unit's start time covers them") {
    Context c;
    CHECK(sandbox_limit_seconds(c, "count") == worst_case_count_seconds(c) + 15);
    CHECK(sandbox_limit_seconds(c, "crashed") == sandbox_limit_seconds(c, "count"));
    CHECK(sandbox_limit_seconds(c, "idle") < 120);
    Context maxima;
    maxima.service_timeout_s = 90;
    // The run and the stand-by for systemd-run (20 s) end before the app unit's TimeoutStartSec.
    std::ifstream in(std::string(ETUDE_SOURCE_DIR) + "/deploy/pi/etude.service");
    std::string line;
    int start_s = -1;
    while (std::getline(in, line)) {
        if (line.rfind("TimeoutStartSec=", 0) == 0) {
            start_s = std::stoi(line.substr(16));
        }
    }
    CHECK(start_s >= sandbox_limit_seconds(maxima, "count") + 20 + 30);
}
